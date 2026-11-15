#include "realscript/game/GameScriptCompiler.h"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace realscript::game {
namespace {

optimization::Level toOptimizationLevel(GameOptimizationLevel level) noexcept {
    switch (level) {
    case GameOptimizationLevel::O0: return optimization::Level::None;
    case GameOptimizationLevel::O1: return optimization::Level::Basic;
    case GameOptimizationLevel::O2: return optimization::Level::Aggressive;
    }
    return optimization::Level::None;
}

bool containsToken(std::string_view text, std::string_view needle) noexcept {
    return !needle.empty() && text.find(needle) != std::string_view::npos;
}

bool hasMacroAttributeUsage(
    const std::vector<compiler::SourceFile>& sources,
    const std::vector<std::shared_ptr<const compiler::ICompilerMacroPlugin>>& plugins) {
    if (plugins.empty()) return false;
    std::vector<std::string> macroNames;
    for (const auto& plugin : plugins) {
        if (!plugin) continue;
        const auto names = plugin->macroNames();
        macroNames.insert(macroNames.end(), names.begin(), names.end());
    }
    if (macroNames.empty()) return true;
    for (const auto& src : sources) {
        for (const auto& macro : macroNames) {
            if (containsToken(src.content, "[" + macro)) {
                return true;
            }
        }
    }
    return false;
}

std::uint64_t computeMacroFingerprint(
    const std::vector<std::shared_ptr<const compiler::ICompilerMacroPlugin>>& plugins) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& plugin : plugins) {
        if (!plugin) continue;
        const auto fp = plugin->fingerprint() ^ 0x9e3779b97f4a7c15ULL;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            hash ^= static_cast<std::uint8_t>((fp >> shift) & 0xffu);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

std::filesystem::path safeRelativeExportPath(const std::string& rawPath) {
    std::filesystem::path clean;
    for (const auto& part : std::filesystem::path(rawPath).relative_path()) {
        const auto s = part.string();
        if (s.empty() || s == "." || s == "..") continue;
        clean /= part;
    }
    if (clean.empty()) {
        clean = "bindings.d.rscript";
    }
    return clean;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

void stripHostNativeStubs(
    std::vector<bytecode::Module>& modules,
    const GameApi& api) {
    const auto nativeModules = api.nativeModules();
    const auto& nativeBindings = api.nativeBindingNames();
    for (auto& module : modules) {
        if (nativeModules.find(module.name) != nativeModules.end()) {
            module.functions.erase(
                std::remove_if(
                    module.functions.begin(),
                    module.functions.end(),
                    [&](const bytecode::Function& function) {
                        return nativeBindings.find(
                            module.name + "::" + function.name) != nativeBindings.end();
                    }),
                module.functions.end());
        }
    }
}

void populateLanguageMetadata(
    const compiler::BuildResult& build,
    GameLanguageMetadata& out) {
    out.genericInstantiations.insert(
        out.genericInstantiations.end(),
        build.nativeGenericInstantiations.begin(),
        build.nativeGenericInstantiations.end());
    std::sort(
        out.genericInstantiations.begin(),
        out.genericInstantiations.end(),
        [](const auto& left, const auto& right) {
            if (left.generatedName != right.generatedName) {
                return left.generatedName < right.generatedName;
            }
            if (left.genericName != right.genericName) {
                return left.genericName < right.genericName;
            }
            return left.arguments < right.arguments;
        });
    out.genericInstantiations.erase(
        std::unique(
            out.genericInstantiations.begin(),
            out.genericInstantiations.end(),
            [](const auto& left, const auto& right) {
                return left.generatedName == right.generatedName &&
                    left.genericName == right.genericName &&
                    left.arguments == right.arguments;
            }),
        out.genericInstantiations.end());
    out.attributes.insert(
        out.attributes.end(),
        build.nativeAttributes.begin(),
        build.nativeAttributes.end());
    std::stable_sort(
        out.attributes.begin(),
        out.attributes.end(),
        [](const auto& left, const auto& right) {
            if (left.target != right.target) {
                return left.target < right.target;
            }
            if (left.name != right.name) {
                return left.name < right.name;
            }
            if (left.sourceName != right.sourceName) {
                return left.sourceName < right.sourceName;
            }
            return left.offset < right.offset;
        });
    out.sequences.insert(
        out.sequences.end(),
        build.nativeSequences.begin(),
        build.nativeSequences.end());
    std::stable_sort(
        out.sequences.begin(),
        out.sequences.end(),
        [](const auto& left, const auto& right) {
            if (left.typeName != right.typeName) {
                return left.typeName < right.typeName;
            }
            return left.name < right.name;
        });
    out.interfaces.insert(
        out.interfaces.end(),
        build.nativeInterfaces.begin(),
        build.nativeInterfaces.end());
    std::stable_sort(
        out.interfaces.begin(),
        out.interfaces.end(),
        [](const auto& left, const auto& right) {
            return left.typeName < right.typeName;
        });
}

} // namespace

GameScriptCompiler::GameScriptCompiler(const GameApi& api)
    : api_(api),
      session_(std::make_shared<compiler::CompilerSession>()) {}

GameScriptCompiler::GameScriptCompiler(
    const GameApi& api,
    std::shared_ptr<compiler::CompilerSession> session,
    std::shared_ptr<compiler::ModuleBuildCache> buildCache)
    : api_(api),
      session_(session ? std::move(session) : std::make_shared<compiler::CompilerSession>()),
      buildCache_(std::move(buildCache)) {}

void GameScriptCompiler::setSession(std::shared_ptr<compiler::CompilerSession> session) {
    session_ = session ? std::move(session) : std::make_shared<compiler::CompilerSession>();
    resetBuildSnapshot();
}

void GameScriptCompiler::setBuildCache(std::shared_ptr<compiler::ModuleBuildCache> buildCache) {
    buildCache_ = std::move(buildCache);
}

void GameScriptCompiler::resetBuildSnapshot() noexcept {
    hasPreviousSnapshot_ = false;
    previousSnapshot_ = {};
    lastSnapshotSession_.reset();
    previousMacroFingerprint_ = 0;
}

GameCompileMetadataExport GameScriptCompiler::exportIdeMetadata(
    const std::string& projectName,
    const std::vector<std::string>& sourcePaths,
    const std::filesystem::path& outputDirectory) const {
    GameCompileMetadataExport meta;
    meta.declarationStubs = api_.exportDeclarationStubs();
    meta.projectConfigJson = api_.exportProjectConfigJson(
        projectName.empty() ? std::string("RealScriptGame") : projectName,
        sourcePaths);

    if (!outputDirectory.empty()) {
        for (const auto& stub : meta.declarationStubs) {
            const auto targetPath = outputDirectory / safeRelativeExportPath(stub.path);
            if (writeTextFile(targetPath, stub.content)) {
                meta.writtenFiles.push_back(targetPath);
            }
        }
        const auto configPath = outputDirectory / "realscript.project.json";
        if (writeTextFile(configPath, meta.projectConfigJson)) {
            meta.writtenFiles.push_back(configPath);
        }
    }
    return meta;
}

GameCompileResult GameScriptCompiler::compile(
    const std::vector<compiler::SourceFile>& sources) const {
    return compile(sources, GameCompileOptions{});
}

GameCompileResult GameScriptCompiler::compile(
    const std::vector<compiler::SourceFile>& sources,
    const GameCompileOptions& options) const {
    GameCompileResult result;
    for (const auto& message : api_.errors()) {
        result.diagnostics.report("RS7000", message, {});
    }
    if (result.diagnostics.hasErrors()) return result;

    if (!session_) {
        session_ = std::make_shared<compiler::CompilerSession>();
    }
    auto activeSession = options.session ? options.session : session_;

    std::shared_ptr<compiler::ModuleBuildCache> activeCache = options.buildCache;
    if (!activeCache && !options.buildCacheDirectory.empty()) {
        if (!buildCache_ || buildCache_->cacheDirectory() != options.buildCacheDirectory) {
            buildCache_ = std::make_shared<compiler::ModuleBuildCache>(options.buildCacheDirectory);
        }
        activeCache = buildCache_;
    } else if (!activeCache) {
        activeCache = buildCache_;
    }

    std::vector<std::shared_ptr<const compiler::ICompilerMacroPlugin>> macroPlugins;
    if (options.enableStandardReactiveMacros) {
        macroPlugins.push_back(compiler::createStandardReactiveMacroPlugin());
    }
    for (const auto& plugin : options.extraMacroPlugins) {
        if (plugin) macroPlugins.push_back(plugin);
    }

    std::vector<compiler::SourceFile> allSources = api_.generatedSources();
    allSources.insert(allSources.end(), sources.begin(), sources.end());

    const bool macrosTriggered =
        !options.extraMacroPlugins.empty() || hasMacroAttributeUsage(sources, macroPlugins);
    const bool useCachePath =
        options.preferBuildCache && activeCache != nullptr && !macrosTriggered;

    std::vector<bytecode::Module> modules;
    if (useCachePath) {
        compiler::ModuleBuildCacheOptions cacheOpts;
        cacheOpts.cacheDirectory = !options.buildCacheDirectory.empty()
            ? options.buildCacheDirectory
            : activeCache->cacheDirectory();
        cacheOpts.optimizationOptions.level = toOptimizationLevel(options.optimizationLevel);
        cacheOpts.flags = options.buildCacheFlags;
        auto cached = activeCache->build(allSources, cacheOpts);
        result.buildCacheStatistics = cached.statistics;
        result.optimizationStatistics = cached.optimizationStatistics;
        result.diagnostics.append(cached.diagnostics);
        result.usedBuildCache = true;
        if (result.diagnostics.hasErrors()) return result;

        modules = std::move(cached.bytecodeModules);
        stripHostNativeStubs(modules, api_);
    } else {
        compiler::CompilationOptions compOpts;
        compOpts.session = activeSession;
        compOpts.macroPlugins = macroPlugins;

        compiler::Compilation compilation(allSources, std::move(compOpts));
        const std::uint64_t macroFp = computeMacroFingerprint(macroPlugins);
        const bool canUsePrevSnapshot =
            options.enableIncrementalBuildSnapshot &&
            hasPreviousSnapshot_ &&
            lastSnapshotSession_ == activeSession &&
            previousMacroFingerprint_ == macroFp;

        auto build = compilation.build(canUsePrevSnapshot ? &previousSnapshot_ : nullptr);
        populateLanguageMetadata(build, result.languageMetadata);
        if (activeSession) {
            result.sessionParsedUnitCacheSize = activeSession->parsedUnitCacheSize();
            result.sessionModuleFrontendCacheSize = activeSession->moduleFrontendCacheSize();
        }
        result.diagnostics.append(build.diagnostics);
        if (result.diagnostics.hasErrors()) return result;

        if (options.enableIncrementalBuildSnapshot) {
            previousSnapshot_ = build.snapshot;
            lastSnapshotSession_ = activeSession;
            previousMacroFingerprint_ = macroFp;
            hasPreviousSnapshot_ = true;
        }

        std::vector<mir::Module> mirModules = std::move(build.modules);
        if (options.optimizationLevel != GameOptimizationLevel::O0) {
            optimization::Options optOpts;
            optOpts.level = toOptimizationLevel(options.optimizationLevel);
            diagnostics::DiagnosticBag optDiag;
            auto optResult = optimization::Optimizer{}.optimize(
                std::move(mirModules), optDiag, optOpts);
            result.optimizationStatistics = optResult.statistics;
            result.diagnostics.append(optDiag);
            if (result.diagnostics.hasErrors()) return result;
            mirModules = std::move(optResult.modules);
        }

        bytecode::Lowerer lowerer;
        modules.reserve(mirModules.size());
        for (const auto& mirModule : mirModules) {
            modules.push_back(lowerer.lower(mirModule));
        }
        stripHostNativeStubs(modules, api_);
    }

    runtime::RuntimeError linkError;
    auto image = runtime::ProgramImage::link(modules, linkError);
    if (!image) {
        result.diagnostics.report("RS7001", linkError.message, {});
        return result;
    }

    result.modules = std::move(modules);
    result.program.program_ = std::make_shared<runtime::ProgramImage>(std::move(*image));
    result.program.bindings_ = api_.bindings();

    // Keep the API's ManagedHeap when host bindings capture host heap references or when
    // the caller already isolated it; otherwise leave null so each ScriptRuntime allocates
    // an independent ValuePoolDomain::create() heap.
    const bool hasNativeBindings =
        !api_.nativeModules().empty() || !api_.nativeBindingNames().empty();
    const bool apiHeapCustomDomain =
        api_.heap() && api_.heap()->valuePoolDomain() != runtime::ValuePoolDomain::defaultDomain();
    if (hasNativeBindings || apiHeapCustomDomain) {
        result.program.heap_ = api_.heap();
    } else {
        result.program.heap_.reset();
    }
    result.program.nativeHandles_ = api_.nativeHandles();
    result.program.languageMetadata_ = result.languageMetadata;

    if (!options.ideMetadataOutputDirectory.empty()) {
        std::vector<std::string> sourcePaths = options.extraProjectSourcePaths;
        sourcePaths.reserve(sourcePaths.size() + sources.size());
        for (const auto& src : sources) {
            if (!src.path.empty()) sourcePaths.push_back(src.path);
        }
        result.ideMetadata = exportIdeMetadata(
            options.projectName,
            sourcePaths,
            options.ideMetadataOutputDirectory);
    }

    return result;
}

void ScriptRuntime::setJitBackend(std::shared_ptr<jit::Module> jitModule) {
    if (!jitModule || !jitModule->valid() || !jitModule->descriptor()) {
        clearNativeBackend();
        return;
    }
    stencilJitEnabled_ = false;
    stencilModule_.reset();
    jitModule_ = std::move(jitModule);
    jitModule_->setBindings(bindings_);
    jitModule_->setHeap(heap_);
    nativeProgram_ = std::make_shared<aot::Program>(*jitModule_->descriptor());
    nativeProgram_->setBindings(bindings_);
    nativeProgram_->setHeap(heap_);
    nativeProgram_->setAsyncRuntime(asyncRuntime_);
    backend_ = ScriptExecutionBackend::Jit;
}

bool ScriptRuntime::startRemoteDebugger(
    std::uint16_t port,
    const std::string& bindAddress) {
#if !defined(REALSCRIPT_HAS_DEBUG_SERVICES) || REALSCRIPT_HAS_DEBUG_SERVICES
    if (!remoteDebugger_) {
        remoteDebugger_ = std::make_shared<debug::EmbeddedDapServer>(program_, heap_);
    } else {
        remoteDebugger_->rebindProgram(program_);
    }
    remoteDebuggerRebind_ = [](
        const std::shared_ptr<debug::EmbeddedDapServer>& srv,
        const std::shared_ptr<const runtime::ProgramImage>& img) {
        if (srv) srv->rebindProgram(img);
    };
    if (!remoteDebugger_->start(port, bindAddress)) {
        return false;
    }
    debugger_ = remoteDebugger_->controller();
    return true;
#else
    (void)port;
    (void)bindAddress;
    return false;
#endif
}

void ScriptRuntime::stopRemoteDebugger() noexcept {
#if !defined(REALSCRIPT_HAS_DEBUG_SERVICES) || REALSCRIPT_HAS_DEBUG_SERVICES
    if (remoteDebugger_) {
        if (debugger_ == remoteDebugger_->controller()) {
            debugger_.reset();
        }
        remoteDebugger_->stop();
        remoteDebugger_.reset();
    }
    remoteDebuggerRebind_ = nullptr;
#endif
}

std::uint16_t ScriptRuntime::remoteDebuggerPort() const noexcept {
#if !defined(REALSCRIPT_HAS_DEBUG_SERVICES) || REALSCRIPT_HAS_DEBUG_SERVICES
    return remoteDebugger_ ? remoteDebugger_->boundPort() : 0;
#else
    return 0;
#endif
}

} // namespace realscript::game
