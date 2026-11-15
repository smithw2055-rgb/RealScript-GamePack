#include "realscript/game/GameBindings.h"
#include "realscript/game/GameProductization.h"
#include "realscript/game/GameScripting.h"
#include "realscript/game/Gameplay.h"
#include "realscript/game/GameplayStateCodec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace realscript;

#define RS_SLIM_CHECK(cond)                                                     \
    do {                                                                        \
        if (!(cond)) {                                                          \
            throw std::runtime_error(                                           \
                std::string("Slim runtime test check failed: ") + #cond);       \
        }                                                                       \
    } while (false)

struct SlimHealthColumn {
    std::int64_t hp = 0;
};
static_assert(std::is_standard_layout_v<SlimHealthColumn>);

std::shared_ptr<const runtime::ProgramImage> buildSlimTestImage(
    std::int64_t lhs,
    std::int64_t rhs) {
    bytecode::Module mod;
    mod.name = "GameSlim.Math";
    mod.version = bytecode::Version{};

    {
        bytecode::Function fn;
        fn.symbolId = semantic::stableTypeId("GameSlim.Math::Counter.Compute");
        fn.accessibility = semantic::Accessibility::Public;
        fn.name = "Counter.Compute";
        fn.returnType = semantic::PrimitiveType::Int;
        fn.registerTypes = {
            semantic::PrimitiveType::Int,
            semantic::PrimitiveType::Int,
            semantic::PrimitiveType::Int,
        };
        fn.registerTypeIds = {0u, 0u, 0u};

        bytecode::BasicBlock bb;
        bb.id = 0;

        bytecode::Instruction i0;
        i0.opcode = bytecode::Opcode::ConstantInt;
        i0.result = 0;
        i0.integerImmediate = lhs;

        bytecode::Instruction i1;
        i1.opcode = bytecode::Opcode::ConstantInt;
        i1.result = 1;
        i1.integerImmediate = rhs;

        bytecode::Instruction i2;
        i2.opcode = bytecode::Opcode::AddInt;
        i2.result = 2;
        i2.operands = {0u, 1u};

        bb.instructions = {i0, i1, i2};
        bb.terminator.kind = bytecode::TerminatorKind::ReturnValue;
        bb.terminator.value = 2;
        fn.blocks.push_back(std::move(bb));
        mod.functions.push_back(std::move(fn));
    }

    {
        // Pure scalar/raw-column parallel element kernel: void StepBatch(int index, HealthCol[] cols)
        bytecode::Function kernel;
        kernel.symbolId = semantic::stableTypeId("GameSlim.Math::StepBatch");
        kernel.accessibility = semantic::Accessibility::Public;
        kernel.name = "StepBatch";
        kernel.returnType = semantic::PrimitiveType::Void;
        kernel.parameterTypes = {semantic::PrimitiveType::Int, semantic::PrimitiveType::Array};
        kernel.parameterTypeIds = {
            0u,
            semantic::stableTypeId(
                semantic::arrayTypeName(semantic::PrimitiveType::Struct, "GameSlim.Math::HealthCol")),
        };
        kernel.registerTypes = kernel.parameterTypes;
        kernel.registerTypeIds = kernel.parameterTypeIds;

        bytecode::BasicBlock bb;
        bb.id = 0;
        bytecode::Instruction p0;
        p0.opcode = bytecode::Opcode::LoadParameter;
        p0.result = 0;
        p0.index = 0;
        bytecode::Instruction p1;
        p1.opcode = bytecode::Opcode::LoadParameter;
        p1.result = 1;
        p1.index = 1;
        bb.instructions = {p0, p1};
        bb.terminator.kind = bytecode::TerminatorKind::ReturnVoid;
        kernel.blocks.push_back(std::move(bb));
        mod.functions.push_back(std::move(kernel));
    }

    {
        semantic::TypeSymbol comp;
        comp.id = semantic::stableTypeId("GameSlim.Math::Counter");
        comp.kind = semantic::TypeKind::Class;
        comp.accessibility = semantic::Accessibility::Public;
        comp.moduleName = "GameSlim.Math";
        comp.name = "Counter";

        semantic::FieldSymbol hpField;
        hpField.name = "hp";
        hpField.accessibility = semantic::Accessibility::Public;
        hpField.declaringTypeId = comp.id;
        hpField.declaringTypeName = "GameSlim.Math::Counter";
        hpField.type = semantic::PrimitiveType::Int;
        hpField.index = 0;
        comp.fields.push_back(hpField);

        semantic::FunctionSymbol computeSym;
        computeSym.id = semantic::stableTypeId("GameSlim.Math::Counter.Compute");
        computeSym.name = "Compute";
        computeSym.moduleName = "GameSlim.Math";
        computeSym.ownerTypeId = comp.id;
        computeSym.ownerTypeName = "Counter";
        computeSym.accessibility = semantic::Accessibility::Public;
        computeSym.returnType = semantic::PrimitiveType::Int;
        computeSym.returnTypeName = "int";
        computeSym.method = true;
        computeSym.staticMethod = true;
        comp.methods.push_back(computeSym);

        semantic::FunctionSymbol ctorSym;
        ctorSym.id = semantic::stableTypeId("GameSlim.Math::Counter..ctor");
        ctorSym.name = ".ctor";
        ctorSym.moduleName = "GameSlim.Math";
        ctorSym.ownerTypeId = comp.id;
        ctorSym.ownerTypeName = "Counter";
        ctorSym.accessibility = semantic::Accessibility::Public;
        ctorSym.returnType = semantic::PrimitiveType::Void;
        ctorSym.constructor = true;
        ctorSym.method = true;
        semantic::VariableSymbol selfParam;
        selfParam.name = "this";
        selfParam.type = semantic::PrimitiveType::Object;
        selfParam.typeName = "GameSlim.Math::Counter";
        selfParam.parameter = true;
        ctorSym.parameters.push_back(selfParam);
        comp.constructors.push_back(ctorSym);

        bytecode::Function ctorFn;
        ctorFn.symbolId = ctorSym.id;
        ctorFn.accessibility = semantic::Accessibility::Public;
        ctorFn.name = "Counter..ctor";
        ctorFn.returnType = semantic::PrimitiveType::Void;
        ctorFn.parameterTypes = {semantic::PrimitiveType::Object};
        ctorFn.parameterTypeIds = {comp.id};
        ctorFn.registerTypes = {semantic::PrimitiveType::Object};
        ctorFn.registerTypeIds = {comp.id};
        bytecode::BasicBlock bb;
        bb.id = 0;
        bytecode::Instruction p0;
        p0.opcode = bytecode::Opcode::LoadParameter;
        p0.result = 0;
        p0.index = 0;
        bb.instructions = {p0};
        bb.terminator.kind = bytecode::TerminatorKind::ReturnVoid;
        ctorFn.blocks.push_back(std::move(bb));
        mod.functions.push_back(std::move(ctorFn));
        mod.types.push_back(std::move(comp));
    }

    {
        semantic::TypeSymbol healthCol;
        healthCol.id = semantic::stableTypeId("GameSlim.Math::HealthCol");
        healthCol.kind = semantic::TypeKind::Struct;
        healthCol.accessibility = semantic::Accessibility::Public;
        healthCol.moduleName = "GameSlim.Math";
        healthCol.name = "HealthCol";
        healthCol.blittable = true;

        semantic::FieldSymbol hpField;
        hpField.name = "hp";
        hpField.accessibility = semantic::Accessibility::Public;
        hpField.declaringTypeId = healthCol.id;
        hpField.declaringTypeName = "GameSlim.Math::HealthCol";
        hpField.type = semantic::PrimitiveType::Int;
        hpField.index = 0;
        healthCol.fields.push_back(hpField);
        mod.types.push_back(std::move(healthCol));
    }

    runtime::RuntimeError linkErr;
    auto linked = runtime::ProgramImage::link({std::move(mod)}, linkErr);
    if (!linked.has_value() || linkErr.code != runtime::ErrorCode::None) {
        throw std::runtime_error("ProgramImage::link failed: " + linkErr.message);
    }
    return std::make_shared<const runtime::ProgramImage>(std::move(*linked));
}

void testSlimRuntimeStencilJitHotReloadDomainIsolationRsbsAndParallel() {
    auto imgV1 = buildSlimTestImage(20, 22);
    auto progV1 = game::GameProgram::fromProgramImage(imgV1);

    // 1. Per-runtime ValuePoolDomain isolation + StencilJit + VM fallback + hot-reload
    auto rt1 = game::ScriptRuntime::createIsolated(progV1);
    auto rt2 = game::ScriptRuntime::createIsolated(progV1);
    RS_SLIM_CHECK(rt1.valuePoolDomain() != nullptr);
    RS_SLIM_CHECK(rt2.valuePoolDomain() != nullptr);
    RS_SLIM_CHECK(rt1.valuePoolDomain()->domainId() != 0u);
    RS_SLIM_CHECK(rt2.valuePoolDomain()->domainId() != 0u);
    RS_SLIM_CHECK(rt1.valuePoolDomain()->domainId() != rt2.valuePoolDomain()->domainId());
    RS_SLIM_CHECK(rt1.heap().get() != rt2.heap().get());

    const auto isolatedApi = game::createIsolatedGameApi();
    RS_SLIM_CHECK(isolatedApi.valid() && isolatedApi.heap() != nullptr);
    RS_SLIM_CHECK(isolatedApi.heap()->valuePoolDomain() != nullptr);
    RS_SLIM_CHECK(isolatedApi.heap()->valuePoolDomain()->domainId() != rt1.valuePoolDomain()->domainId());

    const auto invokeCompute = [](const game::ScriptRuntime& rt) {
        const auto type = rt.findType("GameSlim.Math::Counter");
        RS_SLIM_CHECK(type.has_value());
        const auto fn = rt.findMethod(*type, "Compute", 0);
        RS_SLIM_CHECK(fn.has_value());
        return rt.invokeStatic(*fn, {});
    };

    RS_SLIM_CHECK(rt1.enableStencilJit(true));
    RS_SLIM_CHECK(rt1.stencilJitEnabled());
    RS_SLIM_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::StencilJit);

    const auto rStencilV1 = invokeCompute(rt1);
    RS_SLIM_CHECK(rStencilV1.succeeded);
    RS_SLIM_CHECK(std::get<std::int64_t>(rStencilV1.value) == 42);

    // Disable StencilJit -> clean fallback to VM
    RS_SLIM_CHECK(rt1.enableStencilJit(false));
    RS_SLIM_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::Interpreter);
    const auto rVmV1 = invokeCompute(rt1);
    RS_SLIM_CHECK(rVmV1.succeeded && std::get<std::int64_t>(rVmV1.value) == 42);

    // Re-enable StencilJit and hot-reload program -> re-jits cleanly
    RS_SLIM_CHECK(rt1.enableStencilJit(true));
    auto imgV2 = buildSlimTestImage(50, 49);
    auto progV2 = game::GameProgram::fromProgramImage(imgV2);
    rt1.replaceProgram(progV2);
    RS_SLIM_CHECK(rt1.stencilJitEnabled());
    RS_SLIM_CHECK(rt1.activeBackend() == game::ScriptExecutionBackend::StencilJit);
    const auto rStencilV2 = invokeCompute(rt1);
    RS_SLIM_CHECK(rStencilV2.succeeded && std::get<std::int64_t>(rStencilV2.value) == 99);

    // 2. RSBS Binary Scene Rollback Snapshot round-trip & corruption check
    game::SceneScriptRuntime scene(rt2);
    const auto ent = scene.spawnGenerationalEntity("HeroSlim");
    RS_SLIM_CHECK(scene.attachComponent(ent, "GameSlim.Math::Counter"));
    auto* comp = scene.getComponent(ent, "GameSlim.Math::Counter");
    RS_SLIM_CHECK(comp != nullptr);
    runtime::RuntimeError err;
    RS_SLIM_CHECK(rt2.setMember(*comp, "hp", runtime::Value{std::int64_t{777}}, err));
    scene.setLocalTransform(ent, game::Transform3D{
        game::Vec3{game::Fixed::fromInt(3), game::Fixed::fromInt(4), game::Fixed::fromInt(5)},
        game::Quat::identity(),
        game::Vec3::fromInts(1, 1, 1)});

    game::GameplayHost host(60);
    (void)host.core().clock().step();

    const auto snap = game::captureRollbackFrame(scene, host, err);
    RS_SLIM_CHECK(snap.has_value() && err.code == runtime::ErrorCode::None);
    RS_SLIM_CHECK(!snap->sceneBinary.empty());
    RS_SLIM_CHECK(snap->sceneJson.empty());
    RS_SLIM_CHECK(snap->sceneBinary.size() >= 16u);
    RS_SLIM_CHECK(snap->sceneBinary[0] == 'R' && snap->sceneBinary[1] == 'S' &&
                  snap->sceneBinary[2] == 'B' && snap->sceneBinary[3] == 'S');

    RS_SLIM_CHECK(rt2.setMember(*comp, "hp", runtime::Value{std::int64_t{1}}, err));
    (void)host.core().clock().step();
    RS_SLIM_CHECK(game::restoreRollbackFrame(scene, host, *snap, err));

    auto* restoredComp = scene.getComponent(ent, "GameSlim.Math::Counter");
    RS_SLIM_CHECK(restoredComp != nullptr);
    const auto restoredHp = rt2.getMember(*restoredComp, "hp", err);
    RS_SLIM_CHECK(restoredHp.has_value() && std::get<std::int64_t>(*restoredHp) == 777);
    RS_SLIM_CHECK(scene.getLocalTransform(ent).position.x.toIntFloor() == 3);

    auto corrupted = *snap;
    corrupted.sceneBinary[corrupted.sceneBinary.size() - 10u] ^= 0x5Au;
    RS_SLIM_CHECK(!game::restoreRollbackFrame(scene, host, corrupted, err));

    // 3. ECS Raw-Column + ParallelNativeBatchKernel dispatch in SlimRuntime
    std::vector<SlimHealthColumn> healthRows(64);
    for (std::size_t i = 0; i < healthRows.size(); ++i) {
        healthRows[i].hp = static_cast<std::int64_t>(i + 1u);
    }
    RS_SLIM_CHECK(rt2.bindEcsRawColumnStandardLayout(
        "GameSlim.Math::HealthCol", healthRows.data(), healthRows.size(), sizeof(SlimHealthColumn), err));

    game::GameEcsParallelOptions denyRawOpts;
    denyRawOpts.allowRawColumnArrays = false;
    denyRawOpts.enableNativeBatchKernel = false;
    const auto denyRes = rt2.dispatchEcsParallelColumns(
        "GameSlim.Math::StepBatch",
        healthRows.size(),
        {"GameSlim.Math::HealthCol"},
        {},
        denyRawOpts);
    RS_SLIM_CHECK(!denyRes.succeeded && denyRes.error.code != runtime::ErrorCode::None);

    game::GameEcsParallelOptions parOpts;
    parOpts.workerCount = 2;
    parOpts.batchSize = 16;
    parOpts.allowRawColumnArrays = true;
    parOpts.allowReadOnlyStaticColumnLoads = true;
    parOpts.enableNativeBatchKernel = true;
    parOpts.nativeBatchKernel = [&](runtime::ParallelNativeBatchContext& ctx) -> bool {
        RS_SLIM_CHECK(ctx.columns != nullptr && ctx.columns->size() == 1u);
        auto* rows = static_cast<SlimHealthColumn*>((*ctx.columns)[0].basePtr);
        for (std::size_t i = 0; i < ctx.count; ++i) {
            rows[ctx.start + i].hp += 100;
        }
        return true;
    };
    const auto parRes = rt2.dispatchEcsParallelColumns(
        "GameSlim.Math::StepBatch",
        healthRows.size(),
        {"GameSlim.Math::HealthCol"},
        {},
        parOpts);
    RS_SLIM_CHECK(parRes.succeeded);
    RS_SLIM_CHECK(parRes.usedNativeBatchKernel);
    RS_SLIM_CHECK(parRes.totalItemsProcessed == healthRows.size());
    for (std::size_t i = 0; i < healthRows.size(); ++i) {
        RS_SLIM_CHECK(healthRows[i].hp == static_cast<std::int64_t>(i + 101u));
    }

    std::cout << "[PASS] Slim GameRuntime (zero compiler headers, StencilJit + hot-reload, ValuePoolDomain isolation, RSBS binary rollback, ECS parallel native batch)\n";
}

} // namespace

int main() {
    try {
        testSlimRuntimeStencilJitHotReloadDomainIsolationRsbsAndParallel();
        std::cout << "All game_runtime_slim_tests passed!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
