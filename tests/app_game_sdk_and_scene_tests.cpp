#include "realscript/AbiGovernance.h"
#include "realscript/bytecode/Bytecode.h"
#include "realscript/game/DeterministicMath.h"
#include "realscript/game/GameBindings.h"
#include "realscript/game/GameProductization.h"
#include "realscript/game/GameScriptCompiler.h"
#include <chrono>
#include <filesystem>
#include "realscript/game/GameScripting.h"
#include "realscript/game/GameplayMetadata.h"
#include "realscript/game/GameplayPrimitives.h"
#include "realscript/game/GameplayScripting.h"
#include "realscript/game/GameplayStateCodec.h"
#include "realscript/game/ResourceHandle.h"
#include "realscript/game/RsBindParser.h"
#include "realscript/runtime/AsyncRuntime.h"
#include "realscript/text/AssetGuid.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace realscript;

[[noreturn]] void failCheck(const char* expr, int line) noexcept {
    std::cerr << "Check failed at app_game_sdk_and_scene_tests.cpp:" << line << ": " << expr << std::endl;
    std::exit(1);
}

#define RS_CHECK(...)                                                                              \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            failCheck(#__VA_ARGS__, __LINE__);                                                     \
        }                                                                                          \
    } while (false)

struct NativeDamageSpec {
    std::int32_t baseDamage = 0;
    std::int32_t bonus = 0;
};

struct NativeWeapon {
    std::int32_t damage = 15;
    bool destroyed = false;

    explicit NativeWeapon(std::int32_t initial = 15) : damage(initial) {}

    std::int32_t strike(std::int32_t mult) const {
        return damage * mult;
    }

    std::int32_t getDamage() const { return damage; }
    void setDamage(std::int32_t v) { damage = v; }
};

class DemoTexture final : public game::HostResource {
public:
    explicit DemoTexture(std::atomic<int>& disposedCounter)
        : disposedCounter_(disposedCounter) {}

protected:
    void onDispose() override {
        disposedCounter_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<int>& disposedCounter_;
};

game::GameCompileResult compileGameScript(
    const game::GameApi& api,
    const std::vector<compiler::SourceFile>& sources) {
    game::GameScriptCompiler compiler(api);
    auto res = compiler.compile(sources);
    if (!res.succeeded()) {
        for (const auto& d : res.diagnostics.items()) {
            std::cerr << "GameSdk compile error [" << d.code << "]: " << d.message << "\n";
        }
    }
    RS_CHECK(res.succeeded());
    return res;
}

void testGameApiBindingsAndResourceHandles() {
    game::GameApi api;
    api.structType<NativeDamageSpec>("Game.Native", "DamageSpec")
        .field<&NativeDamageSpec::baseDamage>("baseDamage")
        .field<&NativeDamageSpec::bonus>("bonus");

    api.type<NativeWeapon>("Game.Native", "Weapon")
        .constructor<std::int32_t>("Create")
        .method<&NativeWeapon::strike>("strike")
        .property<&NativeWeapon::getDamage, &NativeWeapon::setDamage>("damage");

    api.function("Game.Native", "scaleDamage", [](NativeDamageSpec spec, std::int32_t factor) {
        return (spec.baseDamage + spec.bonus) * factor;
    });

    RS_CHECK(api.valid());
    auto stubs = api.exportDeclarationStubs();
    RS_CHECK(!stubs.empty());

    std::vector<compiler::SourceFile> sources;
    sources.push_back({
        "combat.rscript",
        R"(module Game.Combat;
import Game.Native;

public class CombatRunner
{
    public static int runAttack(int baseVal)
    {
        var spec = new DamageSpec();
        spec.baseDamage = baseVal;
        spec.bonus = 5;
        int scaled = scaleDamage(spec, 2);

        var blade = Weapon.Create(20);
        blade.damage = blade.damage + scaled;
        return blade.strike(2);
    }
}
)",
    });

    const auto compiled = compileGameScript(api, sources);
    game::ScriptRuntime rt(compiled.program);
    const auto runnerType = rt.findType("Game.Combat::CombatRunner");
    RS_CHECK(runnerType.has_value());
    const auto fn = rt.findMethod(*runnerType, "runAttack", 1);
    RS_CHECK(fn.has_value());
    const auto res = rt.invokeStatic(*fn, {runtime::Value{std::int64_t{10}}});
    RS_CHECK(res.succeeded);
    // (10 + 5) * 2 = 30; blade.damage = 20 + 30 = 50; strike(2) = 100
    RS_CHECK(std::get<std::int64_t>(res.value) == 100);

    // HostResource & ResourceHandle<T> deterministic refcounting and release.
    std::atomic<int> disposedCount{0};
    game::ResourceRegistry reg;
    {
        auto h1 = reg.emplace<DemoTexture>(
            "res://Textures/Hero.png",
            text::AssetGuid::fromAssetPath("res://Textures/Hero.png").toString(),
            disposedCount);
        RS_CHECK(h1.isValid());
        RS_CHECK(h1.useCount() == 1);
        auto h2 = h1.retain();
        RS_CHECK(h2.isValid());
        RS_CHECK(h1.useCount() == 2);
        h2.dispose();
        RS_CHECK(!h2.isValid());
        RS_CHECK(disposedCount.load() == 0);
        RS_CHECK(h1.useCount() == 1);
    }
    RS_CHECK(disposedCount.load() == 1);

    std::cout << "[PASS] GameApi typed bindings & ResourceHandle refcount lifecycle\n";
}

void testRsBindParserPreprocessorAndParity() {
    const std::string header = R"(
#pragma once

RS_EXPORT_ENUM("Game.SdkBind")
enum class CombatMode {
    Melee = 0,
    Ranged = 1,
};

RS_EXPORT_STRUCT("Game.SdkBind")
struct HitInfo {
    RS_EXPORT_FIELD int amount;
    RS_EXPORT_FIELD double knockback;
};

#ifdef ENABLE_EXPERIMENTAL_ABILITY
RS_EXPORT_CLASS("Game.SdkBind")
class ExperimentalAbility {
public:
    RS_EXPORT_CONSTRUCTOR() ExperimentalAbility();
    RS_EXPORT_METHOD int power() const;
};
#else
RS_EXPORT_CLASS("Game.SdkBind")
class StandardAbility {
public:
    RS_EXPORT_CONSTRUCTOR() StandardAbility();
    RS_EXPORT_METHOD int power() const;
};
#endif

#if 0
RS_EXPORT_FUNCTION("Game.SdkBind")
int deadBranchHelper(int x);
#endif

RS_EXPORT_FUNCTION("Game.SdkBind")
int liveBranchHelper(int x);
)";

    const auto offMacro = game::GameApi::generateBindingsFromCppHeader(header, "Game.SdkBind", {});
    const auto offParity = game::rsbind_detail::generateFromParsedHeader(header, "Game.SdkBind", {});
    RS_CHECK(offMacro.scriptDeclarationStub == offParity.scriptDeclarationStub);
    RS_CHECK(offMacro.cppRegistrationSource == offParity.cppRegistrationSource);
    RS_CHECK(offMacro.enumsExported == 1);
    RS_CHECK(offMacro.structsExported == 1);
    RS_CHECK(offMacro.classesExported == 1);
    RS_CHECK(offMacro.functionsExported == 1);
    RS_CHECK(offMacro.scriptDeclarationStub.find("StandardAbility") != std::string::npos);
    RS_CHECK(offMacro.scriptDeclarationStub.find("ExperimentalAbility") == std::string::npos);
    RS_CHECK(offMacro.scriptDeclarationStub.find("deadBranchHelper") == std::string::npos);
    RS_CHECK(offMacro.scriptDeclarationStub.find("liveBranchHelper") != std::string::npos);

    const std::unordered_map<std::string, std::string> macros{{"ENABLE_EXPERIMENTAL_ABILITY", "1"}};
    const auto onMacro = game::GameApi::generateBindingsFromCppHeader(header, "Game.SdkBind", macros);
    RS_CHECK(onMacro.classesExported == 1);
    RS_CHECK(onMacro.scriptDeclarationStub.find("ExperimentalAbility") != std::string::npos);
    RS_CHECK(onMacro.scriptDeclarationStub.find("StandardAbility") == std::string::npos);

    std::cout << "[PASS] RsBindParser C++ header binding generation, #if filtering & parity\n";
}

void testPackageLoaderAbiAndGameplayStateCodec() {
    game::GameApi api;
    api.function("Game.Abi", "hostTickMul", [](std::int32_t x) { return x * 4; });

    std::vector<compiler::SourceFile> sources;
    sources.push_back({
        "pkg_entry.rscript",
        R"(module Game.Pkg;
import Game.Abi;
public class PkgRunner {
    public static int runPkg(int v) { return hostTickMul(v) + 1; }
}
)",
    });

    const auto compiled = compileGameScript(api, sources);
    std::vector<std::vector<std::uint8_t>> encodedModules;
    encodedModules.reserve(compiled.modules.size());
    for (const auto& m : compiled.modules) {
        encodedModules.push_back(bytecode::encodeModule(m));
    }

    const auto abiManifest = game::buildPackageAbiManifest(compiled.modules);
    const auto preflight = game::preflightBytecodePackageHeaders(encodedModules);
    RS_CHECK(preflight.canLoad());

    game::GameProgramLoader loader(api);
    const auto loaded = loader.loadBytecodeModules(encodedModules, abiManifest);
    RS_CHECK(loaded.succeeded());
    RS_CHECK(loaded.package.valid());

    const auto abiReport = game::verifyPackageAbiCompatibility(loaded.package);
    RS_CHECK(abiReport.canLoad());

    const auto envelope = game::buildPackageContractEnvelope(loaded.package);
    const auto envJson = envelope.toJson();
    const auto parsedEnv = game::GamePackageContractEnvelope::fromJson(envJson);
    RS_CHECK(parsedEnv.has_value());
    const auto envCheck = game::verifyPackageContractEnvelope(
        *parsedEnv, &api, loaded.package.programContentHash);
    RS_CHECK(envCheck.canLoad());

    auto loadedRt = loaded.package.createRuntime();
    const auto pkgType = loadedRt.findType("Game.Pkg::PkgRunner");
    RS_CHECK(pkgType.has_value());
    const auto fn = loadedRt.findMethod(*pkgType, "runPkg", 1);
    RS_CHECK(fn.has_value());
    const auto inv = loadedRt.invokeStatic(*fn, {runtime::Value{std::int64_t{7}}});
    RS_CHECK(inv.succeeded);
    RS_CHECK(std::get<std::int64_t>(inv.value) == 29);

    // Deterministic entities, clocks, RNG, scheduler, events + .rsgs GameplayStateCodec.
    game::GameplayHost hostA(60, 0x1234ULL, 0x5678ULL);
    const auto e1 = hostA.core().entities().create();
    RS_CHECK(hostA.core().entities().alive(e1));
    (void)hostA.core().random().nextInt(1, 100);
    (void)hostA.scheduleCall(10u, "onBeat", 3u, {game::GameplayValue{std::int64_t{42}}});
    (void)hostA.subscribe("quest.updated", 10u, "onQuest");
    (void)hostA.publish("quest.updated", {game::GameplayValue{std::int64_t{99}}}, 3u);

    const std::uint64_t digestBefore = hostA.core().stableHash();
    game::GameplayStateError codecErr;
    const auto rsgsBytes = game::encodeGameplayHostState(hostA, codecErr);
    RS_CHECK(!codecErr.failed() && !rsgsBytes.empty());

    game::GameplayHost hostB(60, 1ULL, 2ULL);
    RS_CHECK(game::restoreGameplayHostState(hostB, rsgsBytes, codecErr));
    RS_CHECK(!codecErr.failed());
    RS_CHECK(hostB.core().stableHash() == digestBefore);

    const auto batchA = hostA.drainDue(3u);
    const auto batchB = hostB.drainDue(3u);
    RS_CHECK(batchA.calls.size() == batchB.calls.size());
    RS_CHECK(!batchA.calls.empty());
    RS_CHECK(hostA.core().random().nextInt(0, 100000) == hostB.core().random().nextInt(0, 100000));

    // Corrupt / truncated .rsgs stream must be cleanly rejected.
    std::vector<std::uint8_t> corrupt(rsgsBytes.begin(), rsgsBytes.begin() + 8);
    game::GameplayStateError badErr;
    RS_CHECK(!game::decodeGameplayHostState(corrupt, badErr).has_value());
    RS_CHECK(badErr.failed());

    std::cout << "[PASS] GameProgramLoader, ABI envelope & .rsgs GameplayStateCodec\n";
}

void testSceneLifecycleEventsTriggersAndAsyncCancellation() {
    game::GameApi api;
    const std::string script = R"(module Game.SceneFlow;

public delegate void PingHandler(int bonus);

public class AgentComponent {
    public event PingHandler ping;
    public int starts = 0;
    public int updates = 0;
    public int fixedUpdates = 0;
    public int signals = 0;
    public int triggers = 0;

    public void OnStart() { starts = starts + 1; }
    public void OnFixedUpdate(double dt) { fixedUpdates = fixedUpdates + 1; }
    public void OnUpdate(double dt) { updates = updates + 1; }
    public void onPing(int bonus) { signals = signals + bonus; }
    public void onTriggered() { triggers = triggers + 1; }
}
)";

    const auto compiled = compileGameScript(api, {{"scene_flow.rscript", script}});
    game::ScriptRuntime rt(compiled.program);
    rt.setAsyncRuntime(std::make_shared<runtime::AsyncRuntimeContext>());
    game::SceneScriptRuntime scene(rt);

    const auto e1 = scene.spawnGenerationalEntity("Hero");
    RS_CHECK(scene.isEntityAlive(e1));
    RS_CHECK(!scene.isStaleGenerationalEntity(e1));
    RS_CHECK(scene.attachComponent(e1, "Game.SceneFlow::AgentComponent"));

    RS_CHECK(scene.connectSignal(e1, "Game.SceneFlow::AgentComponent", "ping",
                               e1, "Game.SceneFlow::AgentComponent", "onPing"));

    bool conditionArmed = false;
    RS_CHECK(scene.addTrigger(game::ScriptTrigger{
        "low_hp",
        e1,
        "onTriggered",
        {},
        [&conditionArmed]() { return conditionArmed; },
        true,
        true,
        false,
    }));

    scene.start();
    scene.fixedUpdate(1.0 / 60.0);
    scene.update(0.016);
    scene.lateUpdate(0.016);
    RS_CHECK(scene.evaluateTriggers() == 0);

    conditionArmed = true;
    RS_CHECK(scene.evaluateTriggers() == 1);
    RS_CHECK(scene.emitSignal(e1, "ping", {runtime::Value{std::int64_t{5}}}) == 1);

    const auto* obj = scene.getComponent(e1, "Game.SceneFlow::AgentComponent");
    RS_CHECK(obj != nullptr);
    runtime::RuntimeError memErr;
    const auto starts = rt.getMember(*obj, "starts", memErr);
    const auto fixed = rt.getMember(*obj, "fixedUpdates", memErr);
    const auto updates = rt.getMember(*obj, "updates", memErr);
    const auto signals = rt.getMember(*obj, "signals", memErr);
    const auto triggers = rt.getMember(*obj, "triggers", memErr);
    RS_CHECK(std::get<std::int64_t>(*starts) == 1);
    RS_CHECK(std::get<std::int64_t>(*fixed) == 1);
    RS_CHECK(std::get<std::int64_t>(*updates) == 1);
    RS_CHECK(std::get<std::int64_t>(*signals) == 5);
    RS_CHECK(std::get<std::int64_t>(*triggers) == 1);

    // Async request completion + entityDestroyToken automatic cancellation upon entity destruction.
    const auto reqId = scene.createAsyncRequest("load_save");
    RS_CHECK(scene.waitAsyncRequest(reqId, e1, "onTriggered", "Game.SceneFlow::AgentComponent"));
    RS_CHECK(scene.completeAsyncRequest(reqId, runtime::Value{std::int64_t{3}}));
    RS_CHECK(scene.flushCompletedAsyncRequests() == 1);
    const auto triggersAfterAsync = rt.getMember(*obj, "triggers", memErr);
    RS_CHECK(std::get<std::int64_t>(*triggersAfterAsync) == 2);

    const auto token = scene.entityDestroyToken(e1);
    RS_CHECK(scene.hasEntityDestroyToken(e1));
    RS_CHECK(!rt.asyncRuntime()->cancellations().isCancellationRequested(token));

    scene.queueDestroyEntity(e1);
    RS_CHECK(scene.pendingDestroyCount() == 1);
    RS_CHECK(scene.flushDestroyedEntities() >= 1);
    RS_CHECK(scene.isStaleGenerationalEntity(e1));
    RS_CHECK(rt.asyncRuntime()->cancellations().isCancellationRequested(token));

    std::cout << "[PASS] SceneScriptRuntime lifecycle, triggers, deferred destroy & async token cancel\n";
}

void testHotReloadGuidsSerializationAndRemoteDebug() {
    game::GameApi api;
    const std::string v1Src = R"(module Game.Reload;

public class PlayerStats {
    [Range(min = 0, max = 500)]
    public int oldHp = 100;
    public int level = 1;
}
)";
    const std::string v2Src = R"(module Game.Reload;

public class PlayerStats {
    [FormerlySerializedAs(name = "oldHp")]
    [Range(min = 0, max = 500)]
    public int health = 50;
    public int level = 1;
    public int shield = 25;
}
)";

    const auto c1 = compileGameScript(api, {{"stats_v1.rscript", v1Src}});
    game::ScriptRuntime rt(c1.program);
    game::SceneScriptRuntime scene(rt);

    const auto e1 = scene.spawnGenerationalEntity("StatsHolder");
    RS_CHECK(scene.attachComponent(e1, "Game.Reload::PlayerStats"));
    RS_CHECK(scene.setInspectorMember(
        e1, "Game.Reload::PlayerStats", "oldHp", runtime::Value{std::int64_t{275}}));

    game::ScriptMetadataRegistry metaV1;
    (void)metaV1.populateFromLanguageMetadata(c1.languageMetadata);
    const auto schemaV1 = game::inspectTypeSchema(rt, metaV1, "Game.Reload::PlayerStats");
    RS_CHECK(schemaV1.has_value());
    RS_CHECK(!schemaV1->fields.empty());

    // AssetGuidRegistry + Scene text round-trip.
    game::AssetGuidRegistry guidReg;
    const auto scriptGuid = guidReg.registerType("Game.Reload::PlayerStats");
    const auto resGuid = guidReg.registerAssetPath("res://Prefabs/Hero.prefab");
    RS_CHECK(!scriptGuid.toString().empty() && !resGuid.toString().empty());

    runtime::RuntimeError serErr;
    const std::string sceneText = game::serializeSceneText(scene, serErr, &guidReg);
    RS_CHECK(serErr.code == runtime::ErrorCode::None);
    RS_CHECK(sceneText.find(scriptGuid.toString()) != std::string::npos);

    game::SceneScriptRuntime restoredScene(rt);
    RS_CHECK(game::deserializeSceneText(restoredScene, sceneText, serErr, &guidReg));

    // Hot-reload v1 -> v2 with [FormerlySerializedAs("oldHp")] preserving oldHp -> health = 275.
    const auto c2 = compileGameScript(api, {{"stats_v2.rscript", v2Src}});
    game::ScriptMetadataRegistry metaV2;
    (void)metaV2.populateFromLanguageMetadata(c2.languageMetadata);
    const auto reloadReport = scene.hotReload(c2.program, &metaV2);
    RS_CHECK(reloadReport.succeeded);

    const auto* migrated = scene.getComponent(e1, "Game.Reload::PlayerStats");
    RS_CHECK(migrated != nullptr);
    runtime::RuntimeError readErr;
    const auto healthVal = rt.getMember(*migrated, "health", readErr);
    RS_CHECK(healthVal.has_value() && std::get<std::int64_t>(*healthVal) == 275);
    const auto shieldVal = rt.getMember(*migrated, "shield", readErr);
    RS_CHECK(shieldVal.has_value() && std::get<std::int64_t>(*shieldVal) == 25);

    // Remote debugger build toggle contract (#if REALSCRIPT_HAS_DEBUG_SERVICES).
#if REALSCRIPT_HAS_DEBUG_SERVICES
    RS_CHECK(rt.startRemoteDebugger(0, "127.0.0.1"));
    RS_CHECK(rt.remoteDebuggerPort() > 0);
    rt.stopRemoteDebugger();
    RS_CHECK(rt.remoteDebuggerPort() == 0);
#else
    RS_CHECK(!rt.startRemoteDebugger(0, "127.0.0.1"));
    RS_CHECK(rt.remoteDebuggerPort() == 0);
#endif

    std::cout << "[PASS] Hot-reload [FormerlySerializedAs], GUID serialization, Inspector & Debug toggle\n";
}

void testSceneHotReloadFieldTagsAndGameplayCodecMigration() {
    game::GameApi api;
    const std::string v1Src = R"(module Game.SchemaEvol;

public class HeroStats {
    [FieldTag(11)]
    public int hp = 100;

    public int legacyMana = 40;
}
)";
    const std::string v2Src = R"(module Game.SchemaEvol;

public class HeroStats {
    [FormerlySerializedAs("legacyMana")]
    [FormerlySerializedAs("manaPool")]
    public int mp = 5;

    [FieldTag(11)]
    public int currentHp = 10;

    public int stamina = 55;
}
)";

    const auto c1 = compileGameScript(api, {{"evol_v1.rscript", v1Src}});
    game::ScriptRuntime rt(c1.program);
    game::SceneScriptRuntime scene(rt);

    const auto hero = scene.spawnGenerationalEntity("Hero");
    RS_CHECK(scene.attachComponent(hero, "Game.SchemaEvol::HeroStats"));
    RS_CHECK(scene.setInspectorMember(
        hero, "Game.SchemaEvol::HeroStats", "hp", runtime::Value{std::int64_t{350}}));
    RS_CHECK(scene.setInspectorMember(
        hero, "Game.SchemaEvol::HeroStats", "legacyMana", runtime::Value{std::int64_t{75}}));

    const auto c2 = compileGameScript(api, {{"evol_v2.rscript", v2Src}});
    game::ScriptMetadataRegistry metaV2;
    (void)metaV2.populateFromLanguageMetadata(c2.languageMetadata);
    // Do NOT need FormerlySerializedAs for currentHp: [FieldTag(11)] in decoded .rsbc preserves it.
    const auto reloadReport = scene.hotReload(c2.program, &metaV2);
    RS_CHECK(reloadReport.succeeded);

    const auto* migratedComp = scene.getComponent(hero, "Game.SchemaEvol::HeroStats");
    RS_CHECK(migratedComp != nullptr);
    runtime::RuntimeError readErr;
    const auto hpVal = rt.getMember(*migratedComp, "currentHp", readErr);
    RS_CHECK(hpVal.has_value() && std::get<std::int64_t>(*hpVal) == 350);
    const auto mpVal = rt.getMember(*migratedComp, "mp", readErr);
    RS_CHECK(mpVal.has_value() && std::get<std::int64_t>(*mpVal) == 75);
    const auto stamVal = rt.getMember(*migratedComp, "stamina", readErr);
    RS_CHECK(stamVal.has_value() && std::get<std::int64_t>(*stamVal) == 55);

    // Also test GameplayStateCodec::migrateComponentFieldValues directly with FieldTag + multiple formerNames.
    const std::vector<game::FieldSchemaDescriptor> oldSchema{
        {"oldHp", std::optional<std::int32_t>{11}, {}},
        {"legacyMana", std::nullopt, {}},
    };
    const std::vector<game::FieldSchemaDescriptor> newSchema{
        {"currentHp", std::optional<std::int32_t>{11}, {}},
        {"mp", std::nullopt, {"legacyMana", "manaPool"}},
        {"stamina", std::nullopt, {}},
    };
    const std::map<std::string, game::GameplayValue> savedFields{
        {"oldHp", game::GameplayValue{std::int64_t{350}}},
        {"legacyMana", game::GameplayValue{std::int64_t{75}}},
    };
    const std::map<std::string, game::GameplayValue> defaultValues{
        {"currentHp", game::GameplayValue{std::int64_t{10}}},
        {"mp", game::GameplayValue{std::int64_t{5}}},
        {"stamina", game::GameplayValue{std::int64_t{55}}},
    };

    const auto migratedTwoSchema = game::migrateComponentFieldValues(
        savedFields, oldSchema, newSchema, defaultValues);
    RS_CHECK(std::get<std::int64_t>(migratedTwoSchema.at("currentHp")) == 350);
    RS_CHECK(std::get<std::int64_t>(migratedTwoSchema.at("mp")) == 75);
    RS_CHECK(std::get<std::int64_t>(migratedTwoSchema.at("stamina")) == 55);

    const std::map<std::string, game::GameplayValue> v2Saved{
        {"manaPool", game::GameplayValue{std::int64_t{90}}},
    };
    const auto migratedAliasOnly = game::migrateComponentFieldValues(
        v2Saved, newSchema, defaultValues);
    RS_CHECK(std::get<std::int64_t>(migratedAliasOnly.at("mp")) == 90);
    RS_CHECK(std::get<std::int64_t>(migratedAliasOnly.at("currentHp")) == 10);

    std::cout << "[PASS] Pillar 6: SceneLifecycleRuntime & GameplayStateCodec [FieldTag]/multi-[FormerlySerializedAs] migration\n";
}

struct HostBodyPos {
    std::int64_t x = 0;
    std::int64_t y = 0;
};
static_assert(std::is_standard_layout_v<HostBodyPos>);

struct HostBodyVel {
    std::int64_t dx = 0;
    std::int64_t dy = 0;
};
static_assert(std::is_standard_layout_v<HostBodyVel>);

void testWave2ACompilerOptMacrosCacheIdeExportAndSignals() {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tempRoot = fs::temp_directory_path() / ("rs_gp_w2a_" + std::to_string(nonce));
    const fs::path cacheDir = tempRoot / "build_cache";
    const fs::path ideDir = tempRoot / "ide_meta";
    fs::create_directories(cacheDir);
    fs::create_directories(ideDir);

    game::GameApi api;
    (void)api.function(
        "Game.Native",
        "BonusDamage",
        [](std::int64_t base) -> std::int64_t { return base + 7; });

    // 1. StandardMacros ([Signal] + [RelayCommand]) + -O2 + CompilerSession reuse + IDE metadata export + C++/Script Signal interop
    {
        const std::string reactiveSrc = R"(module Game.Wave2ASignals;

public partial class CombatEmitter {
    public int hitsTaken = 0;
    public int uiPingCount = 0;
    public bool attackReady = false;
    public int attackDamage = 0;
    public int scriptSignalSeenHp = 0;

    [Signal]
    public partial void HealthChanged(int hp, string status);

    [RelayCommand(CanExecute = "CanAttack")]
    public void Attack(int power) {
        this.attackDamage = this.attackDamage + power;
    }

    private bool CanAttack(int power) {
        return this.attackReady && power > 0;
    }

    [RelayCommand]
    public void UiPing() {
        this.uiPingCount = this.uiPingCount + 1;
    }

    public bool CheckAttackCommand(int power) {
        return this.AttackCommand.CanExecute(power);
    }

    public void ExecAttackCommand(int power) {
        this.AttackCommand.Execute(power);
    }

    public void ExecUiPingCommand() {
        this.UiPingCommand.Execute();
    }

    public void EmitFromScript(int hp, string status) {
        this.HealthChanged(hp, status);
    }

    public void BindScriptListener() {
        CombatEmitter__HealthChangedSignalHandler handler = (int hp, string status) => {
            this.scriptSignalSeenHp = this.scriptSignalSeenHp + hp;
        };
        this.HealthChangedEvent += handler;
    }
}

public class CombatListener {
    public int receivedHp = 0;
    public string lastStatus = "";

    public void OnHealthChanged(int hp, string status) {
        this.receivedHp = this.receivedHp + hp;
        this.lastStatus = status;
    }
}
)";
        game::GameScriptCompiler compiler(api);
        game::GameCompileOptions opts;
        opts.optimizationLevel = game::GameOptimizationLevel::O2;
        opts.enableStandardReactiveMacros = true;
        opts.preferBuildCache = false;
        opts.enableIncrementalBuildSnapshot = true;
        opts.ideMetadataOutputDirectory = ideDir;
        opts.projectName = "Wave2ATestProj";
        opts.extraProjectSourcePaths = {"scripts/Combat.rscript"};

        const auto c1 = compiler.compile({{"Combat.rscript", reactiveSrc}}, opts);
        RS_CHECK(c1.succeeded());
        RS_CHECK(c1.sessionParsedUnitCacheSize >= 1u);
        RS_CHECK(!c1.ideMetadata.declarationStubs.empty());
        RS_CHECK(c1.ideMetadata.projectConfigJson.find("Wave2ATestProj") != std::string::npos);
        RS_CHECK(c1.ideMetadata.writtenFiles.size() >= 2u);
        RS_CHECK(fs::exists(ideDir / "realscript.project.json"));
        bool foundStub = false;
        for (const auto& f : c1.ideMetadata.writtenFiles) {
            if (f.extension() == ".rscript" || f.filename().string().find(".d.rscript") != std::string::npos) {
                foundStub = foundStub || fs::exists(f);
            }
        }
        RS_CHECK(foundStub);

        // Incremental session reuse compile
        const auto c2 = compiler.compile({{"Combat.rscript", reactiveSrc}}, opts);
        RS_CHECK(c2.succeeded());
        RS_CHECK(c2.sessionParsedUnitCacheSize >= 1u);

        auto rt = game::ScriptRuntime::createIsolated(c1.program);
        game::SceneScriptRuntime scene(rt);
        const auto emitterEnt = scene.spawnGenerationalEntity("Emitter");
        const auto listenerEnt = scene.spawnGenerationalEntity("Listener");
        RS_CHECK(scene.attachComponent(emitterEnt, "Game.Wave2ASignals::CombatEmitter"));
        RS_CHECK(scene.attachComponent(listenerEnt, "Game.Wave2ASignals::CombatListener"));

        // Wire scene signal using bare [Signal] method name ("HealthChanged")
        RS_CHECK(scene.connectSignal(
            emitterEnt, "CombatEmitter", "HealthChanged",
            listenerEnt, "CombatListener", "OnHealthChanged"));

        auto* emitterObj = scene.getComponent(emitterEnt, "Game.Wave2ASignals::CombatEmitter");
        auto* listenerObj = scene.getComponent(listenerEnt, "Game.Wave2ASignals::CombatListener");
        RS_CHECK(emitterObj != nullptr && listenerObj != nullptr);

        // 1) Script invokes [Signal] -> C++ connected Scene signal target fires
        const auto callMethod = [&](const game::ScriptObject& obj,
                                    const std::string& methodName,
                                    std::vector<runtime::Value> args = {}) {
            const auto method = rt.findMethod(obj.type(), methodName, args.size());
            RS_CHECK(method.has_value());
            return rt.invoke(obj, *method, std::move(args));
        };
        const auto emitFromScriptRes = callMethod(
            *emitterObj,
            "EmitFromScript",
            {runtime::Value{std::int64_t{45}}, runtime::Value{std::string{"hit"}}});
        RS_CHECK(emitFromScriptRes.succeeded);
        runtime::RuntimeError err;
        auto recvHp = rt.getMember(*listenerObj, "receivedHp", err);
        auto recvStatus = rt.getMember(*listenerObj, "lastStatus", err);
        RS_CHECK(recvHp.has_value() && std::get<std::int64_t>(*recvHp) == 45);
        RS_CHECK(recvStatus.has_value() && std::get<std::string>(*recvStatus) == "hit");

        // 2) C++ scene.emitSignal notifies C++ subscriber
        const std::size_t dispatched = scene.emitSignal(
            emitterEnt, "HealthChanged",
            {runtime::Value{std::int64_t{15}}, runtime::Value{std::string{"heal"}}});
        RS_CHECK(dispatched == 1u);
        recvHp = rt.getMember(*listenerObj, "receivedHp", err);
        RS_CHECK(recvHp.has_value() && std::get<std::int64_t>(*recvHp) == 60);

        // 3) C++ scene.emitSignal falls back seamlessly when only a script-side += handler is bound on [Signal]
        const auto subEnt = scene.spawnGenerationalEntity("ScriptOnlySub");
        RS_CHECK(scene.attachComponent(subEnt, "Game.Wave2ASignals::CombatEmitter"));
        auto* subObj = scene.getComponent(subEnt, "Game.Wave2ASignals::CombatEmitter");
        RS_CHECK(subObj != nullptr);
        RS_CHECK(callMethod(*subObj, "BindScriptListener").succeeded);
        const std::size_t scriptOnlyCount = scene.emitSignal(
            subEnt, "HealthChanged",
            {runtime::Value{std::int64_t{33}}, runtime::Value{std::string{"script"}}});
        RS_CHECK(scriptOnlyCount == 1u);
        const auto scriptSeen = rt.getMember(*subObj, "scriptSignalSeenHp", err);
        RS_CHECK(scriptSeen.has_value() && std::get<std::int64_t>(*scriptSeen) == 33);

        // 4) [RelayCommand] CanExecute gating + Execute
        const auto atkCmdVal = rt.getMember(*emitterObj, "AttackCommand", err);
        RS_CHECK(atkCmdVal.has_value() && std::holds_alternative<runtime::ObjectRef>(*atkCmdVal));
        const auto can0 = callMethod(*emitterObj, "CheckAttackCommand", {runtime::Value{std::int64_t{20}}});
        RS_CHECK(can0.succeeded && !std::get<bool>(can0.value));
        RS_CHECK(callMethod(*emitterObj, "ExecAttackCommand", {runtime::Value{std::int64_t{20}}}).succeeded);
        auto dmg = rt.getMember(*emitterObj, "attackDamage", err);
        RS_CHECK(dmg.has_value() && std::get<std::int64_t>(*dmg) == 0);

        RS_CHECK(rt.setMember(*emitterObj, "attackReady", runtime::Value{true}, err));
        const auto can1 = callMethod(*emitterObj, "CheckAttackCommand", {runtime::Value{std::int64_t{20}}});
        RS_CHECK(can1.succeeded && std::get<bool>(can1.value));
        RS_CHECK(callMethod(*emitterObj, "ExecAttackCommand", {runtime::Value{std::int64_t{20}}}).succeeded);
        dmg = rt.getMember(*emitterObj, "attackDamage", err);
        RS_CHECK(dmg.has_value() && std::get<std::int64_t>(*dmg) == 20);
        RS_CHECK(callMethod(*emitterObj, "ExecUiPingCommand").succeeded);
        auto pingCnt = rt.getMember(*emitterObj, "uiPingCount", err);
        RS_CHECK(pingCnt.has_value() && std::get<std::int64_t>(*pingCnt) == 1);
    }

    // 2. ModuleBuildCache persistence across separate GameScriptCompiler instances (-O2)
    {
        const std::string pureSrc = R"(module Game.Wave2ACache;

public class CacheMathOps {
    public static int FoldMath(int x) {
        int a = 3 + 4;
        return x + a;
    }
}
)";
        game::GameCompileOptions cacheOpts;
        cacheOpts.optimizationLevel = game::GameOptimizationLevel::O2;
        cacheOpts.enableStandardReactiveMacros = false;
        cacheOpts.preferBuildCache = true;
        cacheOpts.buildCacheDirectory = cacheDir;

        game::GameScriptCompiler cA(api);
        const auto first = cA.compile({{"CacheMath.rscript", pureSrc}}, cacheOpts);
        RS_CHECK(first.succeeded());

        game::GameScriptCompiler cB(api);
        const auto second = cB.compile({{"CacheMath.rscript", pureSrc}}, cacheOpts);
        RS_CHECK(second.succeeded());
        RS_CHECK(second.usedBuildCache);

        auto rt = game::ScriptRuntime::createIsolated(second.program);
        const auto opsType = rt.findType("Game.Wave2ACache::CacheMathOps");
        RS_CHECK(opsType.has_value());
        const auto foldFn = rt.findMethod(*opsType, "FoldMath", 1);
        RS_CHECK(foldFn.has_value());
        const auto res = rt.invokeStatic(*foldFn, {runtime::Value{std::int64_t{10}}});
        RS_CHECK(res.succeeded && std::get<std::int64_t>(res.value) == 17);
    }

    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    std::cout << "[PASS] Wave 2A: GameScriptCompiler (-O2, Session, BuildCache, [Signal]/[RelayCommand], IDE export)\n";
}

void testWave2AStencilJitHotReloadAndDomainIsolation() {
    game::GameApi api;
    game::GameScriptCompiler compiler(api);

    const std::string v1 = R"(module Game.Wave2AJit;
public class State {
    public int counter = 10;
}
public class JitOps {
    public static int FastCompute(int x) {
        return (x * 2) + 5;
    }
}
)";
    const std::string v2 = R"(module Game.Wave2AJit;
public class State {
    public int counter = 10;
    public int extra = 88;
}
public class JitOps {
    public static int FastCompute(int x) {
        return (x * 3) + 100;
    }
}
)";
    const auto c1 = compiler.compile({{"jit_v1.rscript", v1}});
    RS_CHECK(c1.succeeded());
    const auto c2 = compiler.compile({{"jit_v2.rscript", v2}});
    RS_CHECK(c2.succeeded());

    auto rt1 = game::ScriptRuntime::createIsolated(c1.program);
    auto rt2 = game::ScriptRuntime::createIsolated(c1.program);
    RS_CHECK(rt1.valuePoolDomain() != nullptr && rt2.valuePoolDomain() != nullptr);
    RS_CHECK(rt1.valuePoolDomain()->domainId() != 0u && rt2.valuePoolDomain()->domainId() != 0u);
    RS_CHECK(rt1.valuePoolDomain()->domainId() != rt2.valuePoolDomain()->domainId());

    const auto invokeFastCompute = [](const game::ScriptRuntime& runtime, std::int64_t x) {
        const auto opsType = runtime.findType("Game.Wave2AJit::JitOps");
        RS_CHECK(opsType.has_value());
        const auto fn = runtime.findMethod(*opsType, "FastCompute", 1);
        RS_CHECK(fn.has_value());
        return runtime.invokeStatic(*fn, {runtime::Value{x}});
    };

    RS_CHECK(rt1.enableStencilJit(true));
    RS_CHECK(rt1.stencilJitEnabled());
    RS_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::StencilJit);

    const auto r1 = invokeFastCompute(rt1, 7);
    RS_CHECK(r1.succeeded && std::get<std::int64_t>(r1.value) == 19);

    // Graceful VM fallback when StencilJit is toggled off
    RS_CHECK(rt1.enableStencilJit(false));
    RS_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::Interpreter);
    const auto rVm = invokeFastCompute(rt1, 7);
    RS_CHECK(rVm.succeeded && std::get<std::int64_t>(rVm.value) == 19);

    // Re-enable StencilJit, then hot-reload SceneScriptRuntime -> preserves & re-jits StencilModule
    RS_CHECK(rt1.enableStencilJit(true));
    game::SceneScriptRuntime scene(rt1);
    const auto ent = scene.spawnGenerationalEntity("JitEntity");
    RS_CHECK(scene.attachComponent(ent, "Game.Wave2AJit::State"));
    const auto reload = scene.hotReload(c2.program);
    RS_CHECK(reload.succeeded);
    RS_CHECK(rt1.stencilJitEnabled());
    RS_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::StencilJit);

    const auto r2 = invokeFastCompute(rt1, 7);
    RS_CHECK(r2.succeeded && std::get<std::int64_t>(r2.value) == 121);
    const auto rOther = invokeFastCompute(rt2, 7);
    RS_CHECK(rOther.succeeded && std::get<std::int64_t>(rOther.value) == 19);

    std::cout << "[PASS] Wave 2A: StencilJit tier, VM fallback, hot-reload re-jit & ValuePoolDomain isolation\n";
}

void testWave2ARsbsRollbackAndEcsParallelDispatch() {
    game::GameApi api;
    game::GameScriptCompiler compiler(api);
    const std::string ecsSrc = R"(module Game.Wave2AEcs;

public struct BodyPos {
    public int x;
    public int y;
}

public struct BodyVel {
    public int dx;
    public int dy;
}

public class PlayerState {
    public int hp = 100;
    public string tag = "ready";
    public BodyPos pos;
}

public void StepBodies(int index, BodyPos[] positions, BodyVel[] velocities) {
    BodyPos p = positions[index];
    BodyVel v = velocities[index];
    p.x = p.x + v.dx;
    p.y = p.y + v.dy;
    positions[index] = p;
}
)";
    const auto compiled = compiler.compile({{"wave2a_ecs.rscript", ecsSrc}});
    RS_CHECK(compiled.succeeded());

    // 1. RSBS binary captureRollbackFrame / restoreRollbackFrame + eff_3 serializeSceneText preservation
    {
        auto rt = game::ScriptRuntime::createIsolated(compiled.program);
        game::SceneScriptRuntime scene(rt);
        game::GameplayHost host(60);
        (void)host.core().clock().step();

        const auto pEnt = scene.spawnGenerationalEntity("PlayerOne");
        RS_CHECK(scene.attachComponent(pEnt, "Game.Wave2AEcs::PlayerState"));
        auto* comp = scene.getComponent(pEnt, "Game.Wave2AEcs::PlayerState");
        RS_CHECK(comp != nullptr);
        runtime::RuntimeError err;
        RS_CHECK(rt.setMember(*comp, "hp", runtime::Value{std::int64_t{250}}, err));
        RS_CHECK(rt.setMember(*comp, "tag", runtime::Value{std::string{"dash"}}, err));
        scene.setLocalTransform(pEnt, game::Transform3D{
            game::Vec3{game::Fixed::fromInt(11), game::Fixed::fromInt(22), game::Fixed::fromInt(0)},
            game::Quat::identity(),
            game::Vec3::fromInts(1, 1, 1)});

        // Capture primary RSBS binary rollback frame
        const auto snap = game::captureRollbackFrame(scene, host, err);
        RS_CHECK(snap.has_value() && err.code == runtime::ErrorCode::None);
        RS_CHECK(!snap->sceneBinary.empty());
        RS_CHECK(snap->sceneJson.empty());
        RS_CHECK(snap->sceneBinary[0] == 'R' && snap->sceneBinary[1] == 'S' &&
                 snap->sceneBinary[2] == 'B' && snap->sceneBinary[3] == 'S');

        // Verify eff_3 serializeSceneText remains intact alongside RSBS
        const std::string eff3Text = game::serializeSceneText(scene, err);
        RS_CHECK(err.code == runtime::ErrorCode::None && !eff3Text.empty());

        // Advance and mutate state, then restore RSBS frame
        RS_CHECK(rt.setMember(*comp, "hp", runtime::Value{std::int64_t{1}}, err));
        RS_CHECK(rt.setMember(*comp, "tag", runtime::Value{std::string{"down"}}, err));
        (void)host.core().clock().step();
        (void)host.core().clock().step();
        RS_CHECK(host.core().clock().tick() > snap->tick);

        RS_CHECK(game::restoreRollbackFrame(scene, host, *snap, err));
        RS_CHECK(host.core().clock().tick() == snap->tick);
        auto* restored = scene.getComponent(pEnt, "Game.Wave2AEcs::PlayerState");
        RS_CHECK(restored != nullptr);
        const auto rHp = rt.getMember(*restored, "hp", err);
        const auto rTag = rt.getMember(*restored, "tag", err);
        RS_CHECK(rHp.has_value() && std::get<std::int64_t>(*rHp) == 250);
        RS_CHECK(rTag.has_value() && std::get<std::string>(*rTag) == "dash");
        RS_CHECK(scene.getLocalTransform(pEnt).position.x.toIntFloor() == 11);

        // Corrupt RSBS payload fails cleanly
        auto badSnap = *snap;
        badSnap.sceneBinary[badSnap.sceneBinary.size() - 9u] ^= 0x11u;
        RS_CHECK(!game::restoreRollbackFrame(scene, host, badSnap, err));
    }

    // 2. ECS Parallel Dispatch with raw columns + static verification + native batch kernel
    {
        auto rt = game::ScriptRuntime::createIsolated(compiled.program);
        game::SceneScriptRuntime scene(rt);

        constexpr std::size_t kCount = 96;
        std::vector<HostBodyPos> positions(kCount);
        std::vector<HostBodyVel> velocities(kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            positions[i] = {static_cast<std::int64_t>(i * 10), static_cast<std::int64_t>(i * 2)};
            velocities[i] = {3, -1};
        }

        runtime::RuntimeError bindErr;
        RS_CHECK(rt.bindEcsRawColumnStandardLayout(
            "Game.Wave2AEcs::BodyPos", positions.data(), positions.size(), sizeof(HostBodyPos), bindErr));
        RS_CHECK(rt.bindEcsRawColumnStandardLayout(
            "Game.Wave2AEcs::BodyVel", velocities.data(), velocities.size(), sizeof(HostBodyVel), bindErr, true));

        game::GameEcsParallelOptions denyOpts;
        denyOpts.allowRawColumnArrays = false;
        const auto denyVerify = game::ScriptRuntime::verifyEcsParallelFunction(
            *compiled.program.program(), "Game.Wave2AEcs::StepBodies", denyOpts);
        RS_CHECK(!denyVerify.valid);

        game::GameEcsParallelOptions allowOpts;
        allowOpts.workerCount = 4;
        allowOpts.batchSize = 24;
        allowOpts.allowRawColumnArrays = true;
        allowOpts.allowReadOnlyStaticColumnLoads = true;
        const auto allowVerify = game::ScriptRuntime::verifyEcsParallelFunction(
            *compiled.program.program(), "Game.Wave2AEcs::StepBodies", allowOpts);
        RS_CHECK(allowVerify.valid);

        const auto res = scene.dispatchEcsParallel(
            "Game.Wave2AEcs::StepBodies",
            kCount,
            {"Game.Wave2AEcs::BodyPos", "Game.Wave2AEcs::BodyVel"},
            {},
            allowOpts);
        RS_CHECK(res.succeeded && res.totalItemsProcessed == kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            RS_CHECK(positions[i].x == static_cast<std::int64_t>(i * 10) + 3);
            RS_CHECK(positions[i].y == static_cast<std::int64_t>(i * 2) - 1);
        }
    }

    std::cout << "[PASS] Wave 2A: RSBS binary rollback snapshot, eff_3 preservation & ECS parallel dispatch\n";
}

void testCloseoutRegressionSuite() {
    runtime::RuntimeError err;

    // 1. eff_1: suspended sequence coroutine preservation across SceneScriptRuntime::hotReload
    {
        game::GameApi seqApi;
        auto host = std::make_shared<game::GameplayHost>(60);
        RS_CHECK(game::installGameplayBindings(seqApi, host));

        const std::string seqSrcV1 = R"(module Game.CloseoutSeq;
import RealScript.Game;

public class SeqWorker {
    public int stage = 0;

    public sequence WorkFlow(long target) {
        stage = 10;
        yield wait_ticks(3);
        stage = 99;
    }
}
)";
        const std::string seqSrcV2 = R"(module Game.CloseoutSeq;
import RealScript.Game;

public class SeqWorker {
    public int stage = 0;
    public int bonus = 7;

    public sequence WorkFlow(long target) {
        stage = 10;
        yield wait_ticks(3);
        stage = 99;
    }
}
)";
        const auto v1 = compileGameScript(seqApi, {{"closeout_seq.rscript", seqSrcV1}});
        const auto v2 = compileGameScript(seqApi, {{"closeout_seq.rscript", seqSrcV2}});

        game::ScriptRuntime rt(v1.program);
        game::SceneScriptRuntime scene(rt);
        game::SceneGameplayDriver driver(scene, host);

        constexpr game::SceneEntityId kSeqEnt = 1;
        RS_CHECK(scene.attachComponent(kSeqEnt, "Game.CloseoutSeq::SeqWorker"));
        scene.start();

        auto* workerV1 = scene.getComponent(kSeqEnt, "Game.CloseoutSeq::SeqWorker");
        RS_CHECK(workerV1 != nullptr);
        const auto startWork = rt.findMethod(workerV1->type(), "WorkFlow", 1);
        RS_CHECK(startWork.has_value());
        const auto startRes = rt.invoke(*workerV1, *startWork, {runtime::Value{runtime::LongValue{1}}});
        if (!startRes.succeeded) {
            std::cerr << "WorkFlow start failed: " << startRes.error.message << '\n';
        }
        RS_CHECK(startRes.succeeded);

        const auto midStage = rt.getMember(*workerV1, "stage", err);
        RS_CHECK(midStage.has_value() && std::get<std::int64_t>(*midStage) == 10);

        const auto reloadRep = scene.hotReload(v2.program);
        RS_CHECK(reloadRep.succeeded);
        RS_CHECK(reloadRep.preservedCoroutines >= 1u);
        RS_CHECK(reloadRep.abortedCoroutines == 0u);

        (void)driver.advanceTicks(4u);
        RS_CHECK(driver.errors().empty());

        auto* workerV2 = scene.getComponent(kSeqEnt, "Game.CloseoutSeq::SeqWorker");
        RS_CHECK(workerV2 != nullptr);
        const auto stageAfter = rt.getMember(*workerV2, "stage", err);
        const auto bonusAfter = rt.getMember(*workerV2, "bonus", err);
        RS_CHECK(stageAfter.has_value() && std::get<std::int64_t>(*stageAfter) == 99);
        RS_CHECK(bonusAfter.has_value() && std::get<std::int64_t>(*bonusAfter) == 7);

        // 2. eff_3: serializePrefabText / deserializePrefabText + instantiatePrefab rootMemberOverrides merge
        game::AssetGuidRegistry guids;
        guids.populateFromProgram(v2.program);

        game::PrefabNodeTemplate prefabRoot;
        prefabRoot.name = "TurretRoot";
        prefabRoot.localTransform.position = {
            game::Fixed::fromInt(4), game::Fixed::fromInt(5), game::Fixed::fromInt(6)};
        game::PrefabComponentTemplate rootComp;
        rootComp.scriptType = "Game.CloseoutSeq::SeqWorker";
        rootComp.options.initialMembers["stage"] = runtime::Value{std::int64_t{42}};
        prefabRoot.components.push_back(std::move(rootComp));

        game::PrefabNodeTemplate childPrefab;
        childPrefab.name = "Barrel";
        childPrefab.localTransform.position = {
            game::Fixed::fromInt(1), game::Fixed::fromInt(0), game::Fixed::fromInt(0)};
        prefabRoot.children.push_back(std::move(childPrefab));

        const std::string prefabText = game::serializePrefabText(prefabRoot, err, &guids, &rt);
        RS_CHECK(!prefabText.empty());
        const auto decodedPrefab = game::deserializePrefabText(prefabText, err, &guids, &rt);
        RS_CHECK(decodedPrefab.has_value());
        RS_CHECK(decodedPrefab->name == "TurretRoot");
        RS_CHECK(decodedPrefab->children.size() == 1u && decodedPrefab->children[0].name == "Barrel");

        game::SceneEntityId nextEntityId = 100u;
        const std::map<std::string, runtime::Value> overrides{
            {"stage", runtime::Value{std::int64_t{777}}}};
        const game::SceneEntityId spawnedRoot =
            scene.instantiatePrefab(*decodedPrefab, nextEntityId, 0u, overrides);
        RS_CHECK(spawnedRoot == 100u && nextEntityId == 102u);

        auto* spawnedComp = scene.getComponent(spawnedRoot, "Game.CloseoutSeq::SeqWorker");
        RS_CHECK(spawnedComp != nullptr);
        const auto overriddenStage = rt.getMember(*spawnedComp, "stage", err);
        RS_CHECK(overriddenStage.has_value() && std::get<std::int64_t>(*overriddenStage) == 777);
        RS_CHECK(scene.getParent(101u) == spawnedRoot);
        RS_CHECK(scene.getWorldTransform(101u).position.x.toIntFloor() == 5);
    }

    // 3. perf_4: lifecycleHotPathStats, 4. Physics3D, 5. ECS deferred command buffer
    {
        const std::string perfSrc = R"(module Game.PerfHotPath;

public struct EcsTag {
    public int code;
}

public class TagFactory {
    public static EcsTag Make(int c) {
        var t = new EcsTag();
        t.code = c;
        return t;
    }
}

public class TickCounter {
    public int updates = 0;
    public int fixedUpdates = 0;
    public int collisions3D = 0;

    public void OnUpdate(double dt) {
        updates = updates + 1;
    }

    public void OnFixedUpdate(double dt) {
        fixedUpdates = fixedUpdates + 1;
    }

    public void OnCollisionEnter3D(long other) {
        collisions3D = collisions3D + 1;
    }
}
)";
        game::GameApi perfApi;
        const auto perfCompiled = compileGameScript(perfApi, {{"perf_hot.rscript", perfSrc}});
        auto perfRt = game::ScriptRuntime::createIsolated(perfCompiled.program);
        game::SceneScriptRuntime perfScene(perfRt);

        RS_CHECK(perfScene.attachComponent(1u, "Game.PerfHotPath::TickCounter"));
        RS_CHECK(perfScene.attachComponent(2u, "Game.PerfHotPath::TickCounter"));
        perfScene.start();
        perfScene.resetLifecycleHotPathStats();

        perfScene.update(0.016);
        perfScene.update(0.016);
        perfScene.fixedUpdate(0.016);

        const auto hotStats = perfScene.lifecycleHotPathStats();
        RS_CHECK(hotStats.cacheRebuilds == 1u);
        RS_CHECK(hotStats.cacheHits == 2u);
        RS_CHECK(hotStats.activeCachedComponents == 2u);
        RS_CHECK(hotStats.cachedUpdateSlots == 2u);
        RS_CHECK(hotStats.cachedFixedUpdateSlots == 2u);
        RS_CHECK((hotStats.fastDispatchedCallbacks + hotStats.fallbackDispatchedCallbacks) == 6u);

        // Physics3D: stepPhysics3D, getCollisionContact3D, raycast3D, overlapSphere3D
        game::Transform3D t10{};
        t10.position = {game::Fixed::fromInt(0), game::Fixed::fromInt(0), game::Fixed::fromInt(0)};
        game::Transform3D t11{};
        t11.position = {game::Fixed::fromInt(1), game::Fixed::fromInt(0), game::Fixed::fromInt(0)};
        perfScene.setLocalTransform(10u, t10);
        perfScene.setLocalTransform(11u, t11);

        RS_CHECK(perfScene.attachComponent(10u, "Game.PerfHotPath::TickCounter"));
        perfScene.start();

        game::SceneCollider3D sph{};
        sph.isSphere = true;
        sph.radius = game::Fixed::fromInt(1);
        perfScene.setEntityCollider3D(10u, sph);
        perfScene.setEntityCollider3D(11u, sph);

        RS_CHECK(perfScene.stepPhysics3D() >= 1u);
        const auto contact3D = perfScene.getCollisionContact3D(10u, 11u);
        RS_CHECK(contact3D.has_value() && contact3D->penetration.raw > 0);

        const auto rayHit3D = perfScene.raycast3D(
            game::Vec3{game::Fixed::fromInt(-5), game::Fixed::fromInt(0), game::Fixed::fromInt(0)},
            game::Vec3{game::Fixed::fromInt(1), game::Fixed::fromInt(0), game::Fixed::fromInt(0)},
            game::Fixed::fromInt(20));
        RS_CHECK(rayHit3D.has_value() && rayHit3D->entity == 10u);

        const auto sphereHits3D = perfScene.overlapSphere3D(
            game::Vec3{game::Fixed::fromInt(0), game::Fixed::fromInt(0), game::Fixed::fromInt(0)},
            game::Fixed::fromInt(2));
        RS_CHECK(sphereHits3D.size() == 2u);

        // ECS command buffer: queueEcsSetComponent / RemoveComponent / DestroyEntity / flushEcsCommandBuffer
        const auto factoryType = perfRt.findType("Game.PerfHotPath::TagFactory");
        RS_CHECK(factoryType.has_value());
        const auto makeFn = perfRt.findMethod(*factoryType, "Make", 1u);
        RS_CHECK(makeFn.has_value());
        const auto tagRes = perfRt.invokeStatic(*makeFn, {runtime::Value{std::int64_t{42}}});
        RS_CHECK(tagRes.succeeded);

        perfRt.queueEcsSetComponent(501u, "Game.PerfHotPath::EcsTag", tagRes.value);
        perfRt.queueEcsSetComponent(502u, "Game.PerfHotPath::EcsTag", tagRes.value);
        RS_CHECK(perfRt.pendingEcsCommandCount() == 2u);
        RS_CHECK(perfRt.flushEcsCommandBuffer(err) == 2u);
        RS_CHECK(perfRt.pendingEcsCommandCount() == 0u);
        RS_CHECK(perfRt.hasEcsEntityComponent(501u, "Game.PerfHotPath::EcsTag"));
        RS_CHECK(perfRt.hasEcsEntityComponent(502u, "Game.PerfHotPath::EcsTag"));

        perfRt.queueEcsRemoveComponent(501u, "Game.PerfHotPath::EcsTag");
        perfRt.queueEcsDestroyEntity(502u);
        RS_CHECK(perfRt.pendingEcsCommandCount() == 2u);
        RS_CHECK(perfRt.flushEcsCommandBuffer(err) == 2u);
        RS_CHECK(!perfRt.hasEcsEntityComponent(501u, "Game.PerfHotPath::EcsTag"));
        RS_CHECK(!perfRt.hasEcsEntityComponent(502u, "Game.PerfHotPath::EcsTag"));
    }

    // 6. [ExportCategory] / [ExportGroup] / [ExportSubgroup] sticky Inspector grouping
    {
        const std::string groupingSrc = R"(module Game.InspectorGrouping;

public class GroupedActor {
    [ExportCategory("Combat")]
    [ExportGroup("PrimaryWeapon")]
    [ExportSubgroup("Ammo")]
    [Export]
    public int clipSize = 30;

    [Export]
    public int reserveAmmo = 120;

    [ExportGroup("SecondaryWeapon")]
    [Export]
    public int sidearmClip = 12;

    [ExportCategory("Movement")]
    [Export]
    public int walkSpeed = 5;
}
)";
        game::GameApi grpApi;
        const auto grpCompiled = compileGameScript(grpApi, {{"grouping.rscript", groupingSrc}});
        game::ScriptMetadataRegistry grpMeta;
        (void)grpMeta.populateFromProgram(grpCompiled.program);
        auto grpRt = game::ScriptRuntime::createIsolated(grpCompiled.program);

        const auto schema = game::inspectTypeSchema(
            grpRt, grpMeta, "Game.InspectorGrouping::GroupedActor");
        RS_CHECK(schema.has_value());

        const auto findField = [&](std::string_view fieldName) -> const game::InspectorFieldSchema* {
            for (const auto& f : schema->fields) {
                if (f.name == fieldName) return &f;
            }
            return nullptr;
        };

        const auto* clipSize = findField("clipSize");
        const auto* reserveAmmo = findField("reserveAmmo");
        const auto* sidearmClip = findField("sidearmClip");
        const auto* walkSpeed = findField("walkSpeed");
        RS_CHECK(clipSize != nullptr && reserveAmmo != nullptr &&
                 sidearmClip != nullptr && walkSpeed != nullptr);

        RS_CHECK(clipSize->exportCategory == "Combat");
        RS_CHECK(clipSize->exportGroup == "PrimaryWeapon");
        RS_CHECK(clipSize->exportSubgroup == "Ammo");

        RS_CHECK(reserveAmmo->exportCategory == "Combat");
        RS_CHECK(reserveAmmo->exportGroup == "PrimaryWeapon");
        RS_CHECK(reserveAmmo->exportSubgroup == "Ammo");

        RS_CHECK(sidearmClip->exportCategory == "Combat");
        RS_CHECK(sidearmClip->exportGroup == "SecondaryWeapon");
        RS_CHECK(sidearmClip->exportSubgroup.empty());

        RS_CHECK(walkSpeed->exportCategory == "Movement");
        RS_CHECK(walkSpeed->exportGroup.empty());
        RS_CHECK(walkSpeed->exportSubgroup.empty());
    }

    std::cout << "[PASS] Closeout regression suite (eff_1 sequence hot-reload, eff_3 prefab text, perf_4, Physics3D, ECS cmd buffer & Inspector grouping)\n";
}

} // namespace

int main() {
    testGameApiBindingsAndResourceHandles();
    testRsBindParserPreprocessorAndParity();
    testPackageLoaderAbiAndGameplayStateCodec();
    testSceneLifecycleEventsTriggersAndAsyncCancellation();
    testHotReloadGuidsSerializationAndRemoteDebug();
    testSceneHotReloadFieldTagsAndGameplayCodecMigration();
    testWave2ACompilerOptMacrosCacheIdeExportAndSignals();
    testWave2AStencilJitHotReloadAndDomainIsolation();
    testWave2ARsbsRollbackAndEcsParallelDispatch();
    testCloseoutRegressionSuite();
    std::cout << "All app_game_sdk_and_scene_tests passed!\n";
    return 0;
}
