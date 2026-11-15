# 游戏脚本 SDK（`RealScript-GamePack`）

[GamePack README](../../README.md) | [确定性游戏运行时](GAMEPLAY_RUNTIME.md) | [English](../en/GAME_SCRIPTING_SDK.md)

配套仓库 **`RealScript-GamePack`** 将 `RealScript` L0/L1 编译器与运行时连接至 C++17 游戏对象、ECS 与场景系统，提供以下 CMake 目标：

- `RealScript::GameSdk`：完整游戏引擎 SDK（编译器 + 运行时 + 场景 + 产品化）；
- `RealScript::GameRuntime`：纯运行时裁剪目标，支持 `-DREALSCRIPT_SLIM_RUNTIME=ON` 无编译器/无 JIT 打包；
- `RealScript::GameCompiler`：游戏端 API 注册与编译前端（`GameApi` / `GameScriptCompiler`）。

核心能力：

- C++ 自由函数、类成员方法和属性的类型化绑定，以及 `Fixed`/`Fixed16`/`Vec2`/`Vec3`/`Quat` 数学原语直接封送与零分配快路径（`getMemberVec2` / `setMemberVec2`）；
- 原生快速路径（Native Fast-Path）与对象标识缓存（Identity Cache，提供 O(1) 跨语言对象句柄映射）；
- 自动化 C++ 绑定生成工具 `rsbind`，支持自动解析 C++ 头文件生成类型化绑定代码、Thunk 以及 ECS 连续列辅助函数；
- C++ 宿主连续结构体数组零拷贝 ECS 列绑定（`bindEcsRawColumn` / `bindEcsRawColumnAuto<HostStruct>`，支持每帧原位重绑定零堆分配、解释器与 AOT 零拷贝读写、AOT Trusted Release 模式下 AVX2 向量化循环，以及稀疏组件与 `EntityCommandBuffer` 延迟队列）；
- 编译器可见的宿主 API 声明与 `.d.rscript` 声明桩导出（`exportDeclarationStub`）；
- 使用 `NativeHandle` 的安全原生对象包装、脚本类直接继承 C++ `NativeType`、多态解包与显式生命周期释放（`releaseNative` / `rebindNative`）；
- C++ 创建、持有和调用脚本对象的 Host API，以及 Interpreter / JIT / AOT 分层执行策略（`TieredExecutionPolicy`）；
- 单实体多组件挂载（`attachComponent` / `getComponent`）、代际实体句柄（`spawnGenerationalEntity`）、延迟销毁队列（`queueDestroyEntity` / `flushDestroyedEntities`）、实体命名与分组（`setEntityName` / `addEntityToGroup`）、Editor 与 Play 双执行模式（`[Tool]` / `[ExecuteInEditMode]`）；
- 确定性 `RealScript.Input` 输入快照与 2D/3D 定点物理碰撞系统（`stepPhysics2D` / `stepPhysics3D`、`raycast2D` / `raycast3D`、碰撞与触发器回调）；
- `OnCreate`、`OnStart`、`OnEnable`、`OnDisable`、`OnValidate`、`OnUpdate`、`OnFixedUpdate`、`OnLateUpdate`、`OnDestroy` 完整场景生命周期；
- 深度 `InspectorTypeSchema` 反射（覆盖嵌套 Struct、Enum、泛型容器、`[ExportGroup]`/`[ExportSubgroup]`/`[ExportCategory]`、`[EntityRef]`/`[ComponentRef]`/`[AssetRef]` 与 `event` 信号签名）、`setInspectorMember` 自动触发 `OnValidate`、宿主跨实体信号连接（`connectSignal`）；
- 带状态结构化热重载（`SceneScriptRuntime::hotReload`，支持增删字段、`[FormerlySerializedAs]` 字段重命名、协程/事件状态保留及跨组件引用自动修补）；
- 定向或广播事件、事件队列、条件触发器、执行预算、确定性策略以及结构化错误；
- 对接 L0 `StencilJit`（`x86_64` / `ARM64`）/ `AotRuntime` 原生 `ParallelJobScheduler` 批处理执行，以及 `ModuleBuildCache` 宏插件/局部泛型增量失效一致性。

完整接口和示例见英文主文档 [GAME_SCRIPTING_SDK.md](../en/GAME_SCRIPTING_SDK.md)。
