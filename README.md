# RealScript-GamePack

Standalone L2 Game Engine SDK, Scene Runtime, ECS/Gameplay Runtime, Resource Registry, and `RSGS` State Codec built on top of `RealScript` L0 + L1 (`RealScript::BindGen`).

## CMake Targets

- `RealScript::GameSdk` (`realscript_app_game_sdk`)
- `RealScript::GameRuntime`
- `RealScript::GameCompiler`
- `rsbench_product` (benchmark driver)

## Build

```powershell
cmake -S C:\Projects\RealScript-GamePack -B C:\Projects\RealScript-GamePack\build -DCMAKE_PREFIX_PATH="c:/Projects/RealScript/build/sdk"
cmake --build C:\Projects\RealScript-GamePack\build --config Release --parallel 8
ctest --test-dir C:\Projects\RealScript-GamePack\build -C Release --output-on-failure --parallel 8
```
