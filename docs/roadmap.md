# Runtime 路线图

- **状态：** 持续更新的文档
- **最后更新：** 2026-10-23
- **范围：** [`cyberverse-engine/runtime`](../README.md)

本文档描述帧运行时的预期演进方向。它列出的是工作项，而不是承诺；随着渲染器、平台层和并行
任务需求逐渐明确，优先级、API 细节和排期可能发生变化。

## 愿景

`runtime` 是 Cyberverse Engine 的帧运行时，负责把“一帧真实时间”转换为不同逻辑部分的时钟、
可执行更新、渲染插值系数、呈现节奏和可观测的统计值。它应当：

- 用层次化时钟表达游戏、UI、音频、过场等不同时间域，让暂停和慢动作可以只影响其中一部分；
- 用固定步长推进需要确定性的逻辑，用可变步长驱动 UI 等不需要固定步长的流程；
- 把帧调度、限速、自动帧率稳定和统计集中在同一层，避免平台层、渲染器和游戏循环各自维护
  一套帧策略；
- 保持标准库依赖：不依赖 GLM、`platform`、渲染器或场景类型，窗口、输入、GPU、真实时钟和
  实际绘制由调用方通过钩子注入；
- 提供确定性的单帧原语 `step_frame(dt)`，使时钟继承、阶段调度、统计和退出协议可以在无窗口、
  无 sleep 的环境中测试；
- 在热路径上保持零分配、无隐藏线程、无无界阻塞，并为未来的并行任务框架保留清晰的扩展点；
- 让性能数据（帧时间、各阶段耗时、预算、抖动、超预算、时钟增量）成为一等公民，为渲染器的
  动态降载和工具链的回归门禁提供稳定信号。

## 当前基线（v0.2）

当前实现是一个可工作的“时钟树 + 多阶段 + 可观测统计”运行时。

| 领域 | 状态 |
| --- | --- |
| 时钟树 | `clock_tree` / `clock_node` 提供斜杠路径层次时钟；本地暂停和 `[0,100]` 缩放继承；支持路径校验、全局暂停、`reparent`、`clone`、`snapshot`、`to_text`、`to_json`。 |
| 时钟生命周期 | `lookup`/`ensure`/`contains`/`erase`/`clear` 已实现；根固定为 `"root"`；擦除/清空 detach 子节点；reparent/clone 拒绝环和名称冲突。 |
| 时钟语义 | 有效 delta = `real_delta × 有效缩放`，暂停时为零；`total_time` 积分有效 delta，缩放变化不重置；暂停阶段仍可渲染冻结/零 delta，除非 disabled 或 `receive_render=false`。 |
| 渲染限速 | `frame_schedule` 保留 `target_render_hz`、无限模式、`should_throttle` 和自动帧率稳定；`target_frame_time` 与 `should_throttle` 给出等待策略。 |
| 固定步长 | `frame_schedule::fixed_step_scheduler` 独立持有累加器，默认实时继承 `frame_schedule` 的逻辑配置；`fixed_step_overrides` 只固定显式覆盖的 `target_logic_hz`、`max_frame_time`、`max_logic_steps_per_frame`，其余字段继续跟随 schedule；每个 fixed-step stage 独享 scheduler。 |
| 死亡螺旋防护 | scheduler 钳制 `max_frame_time`，限制 `max_logic_steps_per_frame`，超过时丢弃多余整步并保留小数相位。 |
| 帧阶段 | `frame_stage` 区分 fixed/variable；fixed 每帧零到多次 `on_update` 加一次 `on_render`，variable 每帧一次 `on_update` 和一次 `on_render(1.0, dt)`。 |
| 阶段生命周期 | 稳定且不复用的 stage id；`update_order`/`render_order` 独立优先级；`receive_update`/`receive_render` 门控；`enabled` 整体启停；`group` 批量启停；帧内修改返回 `operation_in_progress`。 |
| 帧生命周期 | pipeline 与 stage 提供 `on_frame_begin`、`on_render_frame_begin`、`on_render_frame_end`；仅 enabled stage 执行生命周期钩子。 |
| 阶段统计 | `stage_stats()` 以注册顺序返回零分配只读快照：id、group、enabled、receive、update/render 时间、fixed steps、alpha、clock delta、tick。 |
| 多时钟阶段 | `frame_pipeline` 每帧更新时钟树；每个 stage 绑定一个 `clock_node`，游戏时钟暂停/慢放时 UI 时钟可继续。 |
| 帧循环 | `frame_pipeline` 负责 `tick_clock`、poll、生命周期和 present；stage 负责 update/render；按 update/render 顺序调度后 present。 |
| 退出语义 | poll 退出跳过整帧；frame-begin/update 退出跳过剩余 update/render/present；render 退出停止剩余 render，仍执行 render-end、present 并记录统计。 |
| 确定性 pacing | `frame_clock` + `steady_frame_clock`；`run()` 维护绝对 deadline，默认 sleep-until，小过睡缩短下一帧等待，大停顿后重新同步；测试可注入 manual clock。 |
| 统计 | `frame_stats` 维护聚合 EMA、FPS、预算利用率、抖动/连续超预算；固定滚动窗口（默认 120，最大 256）提供 min/max/percentile/budget margin，查询不分配堆内存。 |
| 验证 | 87 个 Catch2 测试覆盖时钟继承/操作/调试、阶段 id/options/groups/reentrancy、每 stage 频率、scheduler 实时继承/部分覆盖、阶段统计、生命周期、独立 scheduler、退出、pacing/过睡、自适应、统计窗口和 EMA；17 组 Google Benchmark 覆盖 scheduler、时钟树、统计、阶段查询、优先级、过滤和多阶段整帧开销。 |
| 依赖与打包 | 仅 C++ 标准库与 `Threads::Threads`；尚未提供 install/export 包、版本头和 CI。 |

### 已知限制

- 没有时间缩放曲线类型或逐节点暂停/缩放回调；调用方通过每帧 `set_time_scale` 组合曲线。
  `total_time` 的积分连续性已保证，但 UI 时间缩放混合仍需上层策略。
- 时钟树 `snapshot`/`to_text`/`to_json` 会分配内存；它们用于调试/遥测，不属于逐帧零分配路径。
  路径查找/擦除和阶段统计查询为零分配。
- 所有 stage 仍共享同一个渲染 pacing 策略；每 stage 只覆盖固定逻辑配置，不覆盖 `target_render_hz`
  或自动帧率稳定参数。
- fixed-stage 的 render 在全部 update 之后；没有跨 stage 依赖、update/render 交错或渲染阶段分组。
- variable-step stage 的 `alpha` 固定为 1.0，没有状态插值或双缓冲。
- 没有输入到呈现的延迟测量，也没有与 GPU 时间戳、交换链或 vsync 的关联；`frame_time` 是
  帧起始到下一帧起始的墙钟间隔。
- 没有逐 stage 的预算告警或降载信号；`stage_stats` 只提供耗时快照，自动帧率稳定仍是全局策略。
- 统计窗口是固定容量的原始样本窗口；没有直方图、指数衰减分位数或长期持久化。
- 没有任务系统、工作线程、帧内依赖图或并行阶段；所有 stage 在循环线程串行执行。
- 尚未与 `platform` 的事件泵/计时器和 `render-core` 的呈现路径做示例级集成。
- 没有安装/导出 CMake 包、版本头、示例程序、CI 矩阵或生成的 API 文档。

## v0.2 完成情况

v0.2 的 P0/P1 工作已落地并通过测试：

| 项目 | 优先级 | 状态 |
| --- | --- | --- |
| 逐 stage 统计：update/render 时间、fixed steps、alpha、clock delta、tick、只读快照、零分配查询 | P0 | 完成 |
| 稳定 stage id、运行时启停/修改顺序、分组、帧内防重入、disabled 语义 | P0 | 完成 |
| 每 stage 固定频率/最大步数/最大帧时间覆盖 | P0 | 完成 |
| scheduler 默认实时继承 schedule；`fixed_step_overrides` 只固定显式覆盖项 | P0 | 完成 |
| 可复用 steady-clock、可注入 pacing clock、绝对 deadline、sleep-until、过睡补偿 | P0 | 完成 |
| 时钟树 reparent/clone、路径校验、全局暂停、全树快照、文本/JSON 导出 | P1 | 完成 |
| 时间缩放曲线/逐节点回调评估、暂停渲染语义、`total_time` 连续性 | P1 | 已评估并记录决策 |
| 滚动窗口、最大/最小帧、分位数、逐 stage 耗时、预算余量、零分配查询 | P1 | 完成 |
| 合成时钟测试覆盖 30/60/144/无限、暂停、缩放、追帧、退出 | P0 | 完成 |

### 设计决策

- **逐节点回调：** v0.2 不引入暂停/缩放回调。回调会进入热路径并带来重入/生命周期问题；
  当前采用只读快照、文本/JSON 导出和调用方主动轮询。若后续需要事件驱动，使用显式事件队列
  而不是在 `clock_node::update` 内直接调用用户代码。
- **时间缩放曲线：** v0.2 不提供内置曲线类型。调用方可以每帧计算并调用 `set_time_scale`；
  `total_time` 按有效 delta 积分，缩放变化不重置。内置缓动/混合曲线留到有明确编辑器或过场
  需求时再评估，避免把动画曲线类型带进时钟树。
- **暂停与渲染：** 暂停只冻结有效 delta；stage 仍执行 `on_render` 并使用冻结/零 delta，除非
  stage 被禁用或 `receive_render=false`。这允许暂停时继续绘制 UI/菜单。
- **阶段统计：** pipeline 维护与 `stages_` 平行的快照向量，每帧原地更新；查询返回 const 引用，
  不分配。名称是 `std::string_view`，在 add/erase/clear 后失效。
- **pacing 时钟：** `tick_clock()` 仍是帧 delta 的唯一来源；`frame_clock` 只负责绝对 deadline
  和等待。这样测试可以分别注入合成 delta 和手动 pacing 时钟，不把两者耦合。
- **统计窗口：** 使用固定 `std::array` 环形缓冲；`percentile` 在栈上复制并排序，查询无堆分配，
  代价是 O(window log window)。默认 120 帧，容量上限 256。
- **scheduler 配置继承：** scheduler 默认只保存 schedule 指针与累加器，在每次 `step`/
  `logic_step` 时解析当前逻辑配置，因此 `target_logic_hz`、`max_frame_time`、
  `max_logic_steps_per_frame` 或 `set_config` 的变更会自然同步，且不重置累加器。需要固定部分
  参数时使用 `fixed_step_overrides` 的 `std::optional` 字段，只有被赋值的字段停止跟随；
  显式 `schedule_config` 构造的 scheduler 保持独立。代价是 scheduler 必须由调用方保证
  schedule 生命周期长于自身。
- **帧内修改：** `stepping_` guard 拒绝 add/erase/options/enable 修改；`clear_stages` 在帧内为
  no-op。阶段列表只在帧之间变化，避免迭代器/索引失效。

## 里程碑

| 里程碑 | 主题 | 退出标准 |
| --- | --- | --- |
| v0.2 | 时钟树与阶段能力完善 | 已完成：逐 stage 统计、稳定 id/运行时选项/分组、每 stage 固定频率、确定性 pacing、时钟树操作/调试、统计窗口。 |
| v0.3 | 并行任务框架 | 支持 stage/任务依赖、可配置 worker 池、主线程同步和单线程回退；确定性、取消、异常和 TSAN 验收通过。 |
| v0.4 | 集成、遥测与质量 | 与 `platform`/`render-core` 的示例集成、逐 stage 遥测、预算信号、确定性回放、CI 和 sanitizer 门禁就绪。 |
| v1.0 | 稳定 API 与引擎集成 | 公共 API 冻结；安装/导出包、主循环集成示例、性能预算和长期文档完成。 |

### v0.3 — 并行任务框架

**目标：** 在不破坏固定步长、时钟语义、确定性帧协议和单线程回退的前提下，把 stage/帧内工作
拆分为可依赖的任务图。该里程碑目前不实现，只定义边界、语义和验收方式。

- **任务模型（P0）。** 引入 `task`/`job` 的值语义描述、稳定句柄、依赖边和取消状态；调用方
  提交可调用对象，运行时不解释业务类型、不内建 ECS 或场景概念。任务节点应支持预分配或
  帧内复用，避免热路径分配。
- **工作线程池（P0）。** 支持可配置线程数、主线程参与策略、帧末 join 和关闭流程；禁止在
  `step_frame` 热路径创建/销毁线程。线程池必须可显式关闭，默认单线程回退与 v0.2 行为一致。
- **阶段映射（P0）。** 明确 poll、clock tree update、stage update、stage render、present 的
  同步点；时钟树更新先于读取时钟 delta 的任务，present 在所有依赖任务完成后执行。stage id
  和 update/render 优先级作为任务图提交顺序的稳定输入。
- **依赖与调度（P0）。** 支持 DAG 依赖、提交时环检测、任务取消和错误传播。执行可以并行，
  但提交顺序、完成 join 顺序和可见副作用必须有文档化保证；同一 stage 的多次 fixed update
  必须串行执行，不能并发修改同一逻辑状态。
- **确定性（P0）。** 相同任务图、输入、时钟状态和线程数产生相同的最终状态或明确的提交顺序；
  如果需要逐帧完全可重放，提供确定性模式（固定 worker 数或单 worker 顺序执行）。
- **可观测性（P1）。** 任务排队/执行/依赖等待、worker 利用率和主线程阻塞时间进入独立探针，
  不污染默认热路径；逐 stage 统计仍由 v0.2 的快照提供。
- **内存与异常（P1）。** 定义任务分配器/arena 接口、异常或错误码策略，以及“任务抛异常后
  帧如何结束”的语义。stage 钩子不是 `noexcept`，任务框架不得悄悄改变该契约。
- **验收标准。** 单线程/多线程最终状态一致性、环检测、取消和异常路径、TSAN、1/2/4/8 worker
  扩展性基准；同一 stage 的 fixed update 顺序和时钟 delta 在并行模式下保持不变。

### v0.4 — 集成、遥测与质量

**目标：** 让运行时可以接入真实窗口和渲染器，并具备可运维的性能数据。

- **平台适配示例（P0）。** 在示例中把 `platform` 的事件泵、计时器和窗口关闭状态接到
  `frame_pipeline`；适配层放在示例或 `render-core`，不把 `platform` 链接进 `runtime`。
- **渲染器适配契约（P0）。** 明确 stage `on_render(alpha, dt)` 与 `on_present()` 的职责、
  GPU 同步点、帧号传递、逐 stage 渲染顺序和动态分辨率/降载信号；渲染器不反向驱动时钟树。
- **逐 stage 遥测（P1）。** 基于 `stage_stats()` 提供只读快照和可选 CSV/JSON 导出；帧时间、
  预算、FPS、各 stage 耗时、时钟 delta、暂停/缩放状态可供 overlay 使用。
- **确定性回放（P1）。** 固定输入、时钟树状态、调度参数和任务图，支持逐帧对比和性能回归。
- **CI 与门禁（P0）。** macOS/Windows/Linux 构建与测试、ASan/UBSan/TSan、基准回归阈值和
  无警告构建。
- **验收标准。** 示例在三个平台启动并稳定运行；逐 stage 统计与外部计时器一致；sanitizer 干净；
  基准有可比较的基线。

### v1.0 — 稳定 API 与引擎集成

**目标：** 冻结帧运行时接口，使其成为引擎主循环的稳定基础。

- **API 冻结（P0）。** 确定时钟树、stage、调度、统计、帧循环、pacing clock 和（若已实现）
  任务框架的公共接口；破坏性变更需要弃用周期和迁移说明。
- **安装/导出（P0）。** 提供 `find_package(cyberverse_runtime)`、版本头和 CMake 导出目标。
- **主循环集成（P0）。** 与 `platform`、`render-core`、`camera`、`animation` 形成可运行的最小
  引擎循环示例：轮询、game/UI 双时钟、固定更新、相机/动画推进、渲染、呈现和统计。
- **确定性重放（P1）。** 固定输入、固定时钟树和固定任务图的录制回放，用于回归和性能对比。
- **质量门禁（P0）。** 性能预算、sanitizer、CI 矩阵和公共 API 文档全部就绪。
- **文档（P1）。** 补齐帧协议、时钟继承、pacing、预算语义、自适应控制器、线程模型和集成契约
  的长期文档。

## 稳定性契约

以下语义属于现有公共契约，后续里程碑不得静默改变；如需调整，必须同时更新头文件 Doxygen、
README、AGENTS 和测试。

- 时间单位统一为秒。固定逻辑步长由 `target_logic_hz` 决定，默认 `1 / 60`；非法配置归一化。
- `clock_node` 的有效暂停状态包含所有祖先；有效缩放是祖先链乘积；本地缩放 clamp 到 `[0,100]`，
  NaN 回退到 1；负值/非有限 real delta 按 0 处理；暂停节点 delta 为 0 且总时间不前进。
- `clock_tree` 路径使用 `/` 分隔，空段忽略；`""` 和 `"/"` 指向根；`.`、`..` 和 NUL 非法；根名
  `"root"` 且不可擦除；擦除/清空 detach；reparent/clone 拒绝环和冲突。
- 每个 fixed-step stage 独享一个 `fixed_step_scheduler`；scheduler 不共享累加器。默认 scheduler
  实时继承 `frame_schedule` 的逻辑配置；`fixed_step_overrides` 只固定被赋值的字段，其余字段
  继续跟随；显式 `schedule_config` 构造的 scheduler 独立。继承 scheduler 要求 schedule 生命周期
  不短于 scheduler。`step` 对负值/非有限 delta 按 0 处理，超过 `max_frame_time` 钳制，超过最大
  步数丢弃整步并置 `dropped_backlog`，`alpha` 保持 `[0, 1)`。
- `target_render_hz == 0` 表示无限模式；`target_frame_time()` 返回 0。
- fixed stage 每帧执行零到多次 `on_update(logic_step, stage_tick)` 和一次
  `on_render(alpha, clock_delta)`；variable stage 每帧执行一次 `on_update(clock_delta,
  stage_tick)` 和一次 `on_render(1.0, clock_delta)`。
- `frame_stage_options` 的 update/render 顺序独立，按升序执行，同值按注册顺序；`receive_update`
  只门控 scheduler 与 `on_update`，`receive_render` 只门控 `on_render`；`enabled=false` 跳过所有
  stage 回调；`group` 用于批量启停。
- 帧顺序固定为：poll → clock tree update → pipeline `on_frame_begin` → stage `on_frame_begin`
  （update 顺序） → 全部 stage update（update 顺序） → pipeline `on_render_frame_begin` → stage
  `on_render_frame_begin`（render 顺序） → 全部 stage render（render 顺序） → stage
  `on_render_frame_end`（render 顺序） → pipeline `on_render_frame_end` → present → 统计/自适应。
- stage id 单调递增且不复用；`stage_stats()` 查询零分配，名称视图在 add/erase/clear 后失效；
  帧内 add/erase/options/enable 修改返回 `operation_in_progress`，`clear_stages` 为 no-op。
- quit 语义固定：poll 退出跳过整帧；frame-begin/update 退出跳过剩余 update/render/present；
  render 阶段退出停止剩余 render，但仍执行 render-end 钩子、present 并记录统计；present 退出在
  呈现后观察。
- `tick_clock()` 返回自上次调用以来的真实秒数；`run()` 每轮调用一次，使用 `frame_clock` 的
  绝对 deadline 进行等待；`step_frame(dt)` 不使用 pacing clock、不 sleep。
- `frame_stats` 首样本建立 EMA 基线；滚动窗口容量固定；percentile 查询无堆分配；`set_budget`
  重置超预算计数但保留 EMA。
- `request_quit()`/`should_quit()` 是唯一的跨线程入口；其余状态只允许循环线程访问。
- 运行时热路径不创建线程、不分配内存、不做无界阻塞；任何会改变这一点的能力必须显式启用并
  在文档中标注。

## 非目标

- 不在 `runtime` 中实现窗口、输入、渲染、资源加载、场景或玩法逻辑。
- 不引入 GLM、`platform`、`render-core` 或 DiligentEngine 依赖；适配层放在示例或上层模块。
- 不实现动态分辨率、LOD、剔除或其他渲染降载策略；运行时只提供调度结果和统计信号。
- 不提供隐藏的全局线程池、后台线程或“自动并行化”；并行任务框架必须显式启用、可关闭，并
  保留单线程回退。
- 不在 v0.3 之前承诺任务框架的公共 API；相关实验不得泄漏到现有帧协议。
- 不定义资源/动画/场景的序列化格式；录制回放只覆盖运行时输入、时钟和调度状态。
- 不强制所有逻辑使用固定步长；variable-step stage 是给 UI、工具和不需要确定性的流程使用的。

## 待决问题

- **时钟树语义：** 有效时间缩放是否需要在某个上限再次 clamp？是否要引入内置缩放曲线/分层混合？
  逐节点回调是否值得以显式事件队列形式加入？
- **时钟节点生命周期：** reparent/clone 已实现；是否需要移动后保持旧路径别名或稳定的节点句柄？
- **阶段排序：** 已有独立 update/render 优先级和 receive/enabled/group；是否还需要跨 stage 依赖、
  分组顺序或运行时稳定排序句柄？
- **每 stage 渲染 pacing：** 是否允许 stage 覆盖 `target_render_hz`/自适应参数，还是渲染 pacing
  必须保持全局？
- **逐 stage 统计：** 快照之外是否需要历史窗口/分位数/预算告警？统计是否应支持逐 stage 的
  滚动窗口？
- **variable-step 语义：** `alpha` 固定 1.0 是否足够？UI 是否需要双缓冲/插值？暂停时是否仍调用
  `on_update(0)`？
- **pacing 时钟：** `tick_clock()` 返回 delta 与 `frame_clock` 绝对时间是否应合并为单一抽象？
  是否需要 vsync 相位对齐或 GPU 时间戳？
- **任务框架所有权：** `runtime` 自己创建 worker 池，还是接受外部 executor/线程池？stage 与
  任务图如何保证同一 stage 的 fixed update 串行？
- **异常策略：** stage 钩子不是 `noexcept`；抛异常时应终止帧、传播到主线程，还是记录错误并
  跳过依赖任务？
- **平台集成方式：** 通过示例适配 `platform::timer`，还是提供可选 `platform` 后端？后者会破坏
  standard-library-only 规则。
- **遥测格式：** 统计/时钟树快照的 CSV/JSON 字段和版本如何保持稳定？

## 完成定义与发布检查清单

一个路线图事项满足以下条件才算完成：

- 公共 API 有 Doxygen 文档，并记录非法输入、生命周期和线程契约；
- 行为有确定性单元测试覆盖；涉及平台/渲染器的部分有示例或可选集成测试；
- `clang-format`、无警告构建、完整 CTest 套件和 sanitizer 运行通过；
- 性能敏感变更附基准结果，并与上一版本比较；
- 若约定变化，同步更新 `README.md`、`AGENTS.md`、本路线图和工作区路线图。

发布检查清单：

- [ ] 公共 API 已评审并记录，破坏性变更已走弃用周期。
- [ ] Debug/Release 测试通过，ASan/UBSan/TSan 通过。
- [ ] `-Wall -Wextra -Wpedantic -Werror` 构建干净。
- [ ] 基准与上一版本对比，无未解释回退。
- [ ] 安装/导出包和 `find_package` 验证通过（v1.0）。
- [ ] 主循环示例和（如适用）并行任务示例可运行。
- [ ] 文档、路线图和版本号已更新。
