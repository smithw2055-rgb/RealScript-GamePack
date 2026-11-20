# RealScript-GamePack

Standalone **L2** Game Engine SDK, Scene Script Runtime, Deterministic Gameplay Runtime, Resource Registry, `ReflectionRegistry`, `ModHost`, and `RSGS` State Codec built on top of **`RealScript`** L0 + L1 (`RealScript::BindGen`).

> **Layering boundary:** Core language frontend, bytecode VM, incremental/generational GC, `StencilJit` (`x86_64` & `ARM64`), C++17 AOT runtime, `HotReload`, `Debug` (DAP), `Tooling` (LSP/Formatter/Governance Linter), and `rsbind` live in **`RealScript`**. Engine-specific scene components, zero-copy ECS column bindings, physics/input helpers, fixed-tick gameplay primitives, and save/productization SDKs live here in **`RealScript-GamePack`**.

## Documentation

- English: [Game Scripting SDK](docs/en/GAME_SCRIPTING_SDK.md) | [Deterministic Gameplay Runtime](docs/en/GAMEPLAY_RUNTIME.md)
- 中文: [游戏脚本 SDK](docs/zh-CN/GAME_SCRIPTING_SDK.md) | [确定性游戏运行时](docs/zh-CN/GAMEPLAY_RUNTIME.md)

## CMake Targets

- `RealScript::GameRuntime` — Runtime-only game scripting SDK (`ScriptRuntime`, `SceneScriptRuntime`, `GameplayHost`, `GameplayStateCodec`, `ResourceRegistry`, `GameProductization`, `ReflectionRegistry`, `ModHost`). Compatible with `-DREALSCRIPT_SLIM_RUNTIME=ON`.
- `RealScript::GameCompiler` — Game-side compiler integration (`GameApi`, `GameScriptCompiler`, `.d.rscript` host declarations).
- `RealScript::GameSdk` (alias target `realscript_app_game_sdk`) — Combined compiler + runtime + scene + gameplay umbrella target (available when `REALSCRIPT_SLIM_RUNTIME=OFF`).
- `rsbench_product` — Product-scale coroutine/rollback benchmark driver (built when `REALSCRIPT_GAMEPACK_BUILD_BENCHMARKS=ON`).

## Build

### 1. Against a prebuilt `RealScript` build output (recommended default)

When `RealScript` sits at the sibling path `../RealScript/build`, `RealScript-GamePack` discovers and links its prebuilt libraries directly without recompiling `RealScript`:

```powershell
cmake -S C:\Projects\RealScript-GamePack -B C:\Projects\RealScript-GamePack\build -DREALSCRIPT_BUILD_DIR="C:\Projects\RealScript\build"
cmake --build C:\Projects\RealScript-GamePack\build --config Release --parallel 8
ctest --test-dir C:\Projects\RealScript-GamePack\build -C Release --output-on-failure --parallel 8
```

### 2. Against an installed `RealScript` SDK

```powershell
cmake -S C:\Projects\RealScript-GamePack -B C:\Projects\RealScript-GamePack\build -DCMAKE_PREFIX_PATH="C:\Projects\RealScript\build\sdk"
cmake --build C:\Projects\RealScript-GamePack\build --config Release --parallel 8
ctest --test-dir C:\Projects\RealScript-GamePack\build -C Release --output-on-failure --parallel 8
```

### 3. Headless / Slim Runtime Configuration

When shipping a dedicated client/server without an embedded compiler, JIT, or hot-reload host, first build a slim `RealScript` output tree (`-DREALSCRIPT_SLIM_RUNTIME=ON`) and point `RealScript-GamePack` directly to that build directory:

```powershell
cmake -S C:\Projects\RealScript -B C:\Projects\RealScript\build-slim -DREALSCRIPT_SLIM_RUNTIME=ON
cmake --build C:\Projects\RealScript\build-slim --config Release --parallel 8

cmake -S C:\Projects\RealScript-GamePack -B C:\Projects\RealScript-GamePack\build-slim `
  -DREALSCRIPT_BUILD_DIR="C:\Projects\RealScript\build-slim"
cmake --build C:\Projects\RealScript-GamePack\build-slim --config Release --parallel 8
ctest --test-dir C:\Projects\RealScript-GamePack\build-slim -C Release --output-on-failure
```

This builds `RealScript::GameRuntime` and verifies `realscript.gamepack.runtime-slim`.
