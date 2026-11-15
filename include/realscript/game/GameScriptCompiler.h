#pragma once

#include "realscript/compiler/Compilation.h"
#include "realscript/compiler/ModuleBuildCache.h"
#include "realscript/compiler/StandardMacros.h"
#include "realscript/debug/DapServer.h"
#include "realscript/game/GameScripting.h"
#include "realscript/jit/Jit.h"
#include "realscript/optimization/Optimizer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace realscript::game {

enum class GameOptimizationLevel : std::uint8_t {
    O0 = 0,
    O1 = 1,
    O2 = 2,
};

struct GameCompileOptions {
    GameOptimizationLevel optimizationLevel = GameOptimizationLevel::O0;
    bool enableStandardReactiveMacros = true;
    std::vector<std::shared_ptr<const compiler::ICompilerMacroPlugin>> extraMacroPlugins;
    std::shared_ptr<compiler::CompilerSession> session;
    std::shared_ptr<compiler::ModuleBuildCache> buildCache;
    std::filesystem::path buildCacheDirectory;
    std::uint64_t buildCacheFlags = compiler::kBuildCacheFlagPreserveDebugInfo;
    bool preferBuildCache = true;
    bool enableIncrementalBuildSnapshot = true;
    std::filesystem::path ideMetadataOutputDirectory;
    std::string projectName = "RealScriptGame";
    std::vector<std::string> extraProjectSourcePaths;
};

struct GameCompileMetadataExport {
    std::vector<compiler::SourceFile> declarationStubs;
    std::string projectConfigJson;
    std::vector<std::filesystem::path> writtenFiles;
};

struct GameCompileResult {
    GameProgram program;
    std::vector<bytecode::Module> modules;
    diagnostics::DiagnosticBag diagnostics;
    GameLanguageMetadata languageMetadata;
    optimization::Statistics optimizationStatistics;
    compiler::BuildCacheStatistics buildCacheStatistics;
    bool usedBuildCache = false;
    std::size_t sessionParsedUnitCacheSize = 0;
    std::size_t sessionModuleFrontendCacheSize = 0;
    GameCompileMetadataExport ideMetadata;

    [[nodiscard]] bool succeeded() const noexcept {
        return program.valid() && !diagnostics.hasErrors();
    }
};

class GameScriptCompiler {
public:
    explicit GameScriptCompiler(const GameApi& api);
    GameScriptCompiler(
        const GameApi& api,
        std::shared_ptr<compiler::CompilerSession> session,
        std::shared_ptr<compiler::ModuleBuildCache> buildCache = nullptr);

    void setSession(std::shared_ptr<compiler::CompilerSession> session);
    [[nodiscard]] std::shared_ptr<compiler::CompilerSession> session() const noexcept {
        return session_;
    }

    void setBuildCache(std::shared_ptr<compiler::ModuleBuildCache> buildCache);
    [[nodiscard]] std::shared_ptr<compiler::ModuleBuildCache> buildCache() const noexcept {
        return buildCache_;
    }

    void resetBuildSnapshot() noexcept;

    [[nodiscard]] GameCompileResult compile(
        const std::vector<compiler::SourceFile>& sources) const;

    [[nodiscard]] GameCompileResult compile(
        const std::vector<compiler::SourceFile>& sources,
        const GameCompileOptions& options) const;

    [[nodiscard]] GameCompileMetadataExport exportIdeMetadata(
        const std::string& projectName = "RealScriptGame",
        const std::vector<std::string>& sourcePaths = {},
        const std::filesystem::path& outputDirectory = {}) const;

private:
    const GameApi& api_;
    mutable std::shared_ptr<compiler::CompilerSession> session_;
    mutable std::shared_ptr<compiler::ModuleBuildCache> buildCache_;
    mutable std::shared_ptr<compiler::CompilerSession> lastSnapshotSession_;
    mutable compiler::BuildSnapshot previousSnapshot_;
    mutable bool hasPreviousSnapshot_ = false;
    mutable std::uint64_t previousMacroFingerprint_ = 0;
};

} // namespace realscript::game
