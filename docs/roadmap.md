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

## 当前基线（v0.1）

当前实现是一个可工作的“时钟树 + 多阶段”运行时。

| 领域 | 状态 |
| --- | --- |
| 时钟树 | `clock_tree` / `clock_node` 提供斜杠路径的层次时钟；每个节点有本地暂停和 `[0,100]` 时间缩放，有效状态继承祖先链，记录本帧有效 delta 和累计有效时间。 |
| 时钟生命周期 | 支持 `lookup`、`ensure`、`contains`、`erase`、`clear`；根节点固定为 `"root"`，擦除/清空会 detach 子节点，外部 shared_ptr 仍可作为无父时钟使用。 |
| 渲染限速 | `frame_schedule` 保留 `target_render_hz`、无限模式、`should_throttle` 和自动帧率稳定；`target_frame_time` 与 `should_throttle` 给出等待策略。 |
| 固定步长 | `frame_schedule::fixed_step_scheduler` 独立持有累加器和逻辑配置拷贝，返回 `logic_steps`、`alpha`、`dropped_backlog`；每个 fixed-step stage 独享一个 scheduler。 |
| 死亡螺旋防护 | scheduler 钳制 `max_frame_time`，限制 `max_logic_steps_per_frame`，超过时丢弃多余整步并保留小数相位。 |
| 帧阶段 | `frame_stage` 区分 fixed-step 与 variable-step；fixed 每帧零到多次 `on_update` 加一次 `on_render`，variable 每帧一次 `on_update` 和一次 `on_render(1.0, dt)`。 |
| 阶段排序与过滤 | `frame_stage_options` 提供独立的 update/render 优先级；同优先级按注册顺序。`receive_update`/`receive_render` 分别门控更新和渲染，不删除 stage。 |
| 帧生命周期 | pipeline 与 stage 提供 `on_frame_begin`、`on_render_frame_begin`、`on_render_frame_end`；生命周期钩子对所有 stage 执行，不受 receive 标志影响。 |
| 多时钟阶段 | `frame_pipeline` 每帧更新时钟树；每个 stage 绑定一个 `clock_node`，因此游戏时钟暂停/慢放时 UI 时钟可继续运行。 |
| 帧循环 | `frame_pipeline` 负责 `tick_clock`、poll、生命周期和 present；stage 负责 update/render。先按 update 顺序更新全部 stage，再按 render 顺序渲染全部 stage，最后 present。 |
| 退出语义 | poll 退出跳过本帧；frame-begin/update 退出跳过剩余 update/render/present；render 退出停止剩余 render，仍执行 render-end 钩子、present 并记录统计。 |
| 线程与生命周期 | `request_quit`/`should_quit` 使用原子标志，可从其他线程调用；其余状态单线程；默认 `on_throttle_wait` 使用 `sleep_for`，可覆写。 |
| 统计 | `frame_stats` 维护聚合的帧/逻辑/渲染时间 EMA、FPS、预算利用率、抖动和连续超预算标志；首个样本只建立基线。 |
| 验证 | 66 个 Catch2 测试覆盖时钟继承/lifetime、阶段调度、优先级、receive 标志、生命周期、独立 scheduler、退出路径、pacing、自适应和统计；14 组 Google Benchmark 覆盖 scheduler、时钟树、统计、优先级和过滤后的多阶段整帧开销。 |
| 依赖与打包 | 仅 C++ 标准库与 `Threads::Threads`；尚未提供 install/export 包、版本头和 CI。 |

### 已知限制

- 时钟树只有“更新/查询/暂停/缩放”基础能力：没有 reparent、clone、序列化、调试导出、逐节点
  回调、时间缩放曲线或全局暂停 API。
- 所有 stage 共享同一个逻辑频率（`target_logic_hz`）；时钟缩放改变有效 delta，但不能让某个
  stage 使用不同的固定 Hz。不同 fixed 频率仍需要独立配置。
- `tick_clock()` 是纯虚函数，运行时要求调用方实现真实 delta；仓库内没有现成的
  `steady_clock` 辅助类，也没有 sleep-until、过睡补偿或 vsync 相位对齐。
- 限速仍使用普通 `sleep_for`，没有截止时间累计误差补偿；在高负载系统上目标 60 Hz 可能稳定
  在 58–59 Hz。
- 没有暂停/恢复、时间缩放、单步执行或录制/回放的统一运行时 API；这些能力目前通过时钟节点和
  调用方代码组合实现。
- 阶段列表只在帧之间安全修改；`add_stage`/`erase_stage` 从 stage 回调中调用没有防重入保护。
  `frame_stage_options` 在注册后不可变，没有稳定 stage id、分组或运行时启停接口；`receive_*`
  只能通过重新注册调整。
- 统计只有聚合值，没有逐 stage 的 update/render 时间、logic steps、alpha、时钟 delta 和
  队列/等待时间。
- 没有逐 stage 的预算或超预算信号；自动帧率稳定只改变呈现节奏，不降低逻辑或渲染负载。
- fixed-step 与 variable-step stage 的 render 都在全部 update 之后执行；虽然有独立的 render
  优先级，但没有跨 stage 的依赖、update/render 交错或渲染阶段分组。
- variable-step stage 的 `alpha` 固定为 1.0，没有状态插值或双缓冲；fixed stage 的 `alpha`
  来自 scheduler。
- 没有输入到呈现的延迟测量，也没有与 GPU 时间戳、交换链或 vsync 的关联；`frame_time` 是
  帧起始到下一帧起始的墙钟间隔，当前帧的等待会体现在下一个样本中。
- 没有任务系统、工作线程、帧内依赖图或并行阶段；所有 stage 在循环线程串行执行。
- 没有帧预算告警回调、遥测导出、可视化 overlay 或进程内性能回归门禁。
- 尚未与 `platform` 的事件泵/计时器和 `render-core` 的呈现路径做示例级集成。
- 没有安装/导出 CMake 包、版本头、示例程序、CI 矩阵或生成的 API 文档。

## 里程碑

| 里程碑 | 主题 | 退出标准 |
| --- | --- | --- |
| v0.2 | 时钟树与阶段能力完善 | 逐 stage 统计、阶段启用/排序、每 stage 固定频率、时钟树调试/操作 API 可用；确定性时钟与 pacing 测试就绪。 |
| v0.3 | 并行任务框架 | 支持 stage/任务依赖、可配置 worker 池、主线程同步和单线程回退；确定性、取消、异常和 TSAN 验收通过。 |
| v0.4 | 集成、遥测与质量 | 与 `platform`/`render-core` 的示例集成、逐 stage 遥测、预算信号、确定性回放、CI 和 sanitizer 门禁就绪。 |
| v1.0 | 稳定 API 与引擎集成 | 公共 API 冻结；安装/导出包、主循环集成示例、性能预算和长期文档完成。 |

### v0.2 — 时钟树与阶段能力完善

**目标：** 把当前“能按不同时钟跑 stage”的基线提升为可配置、可观测、可确定性验证的帧调度
层，同时保持现有 `step_frame`、stage 和时钟继承契约。

- **逐 stage 统计（P0）。** 为每个 stage 记录 update 时间、render 时间、fixed steps、alpha、
  时钟 delta 和累计 tick；pipeline 保留聚合统计，同时提供只读的 stage 快照。查询路径零分配，
  不改变现有 `frame_stats` 的聚合语义。
- **阶段生命周期与顺序（P0）。** 增加稳定 stage id、运行时启停/修改顺序选项、分组，以及从回调
  中误调用 add/erase 时的防重入错误。明确禁用 stage 是否仍 poll/render、是否消费 tick；保留
  现有独立 update/render 优先级和 receive 标志语义。
- **每 stage 固定频率（P0）。** 允许 fixed-step stage 覆盖 `target_logic_hz`、最大步数和最大
  catch-up 步数，而不是所有 stage 共享同一固定 Hz；scheduler 创建时使用 stage 级配置。UI 可以
  使用 variable step，游戏使用 60 Hz，过场或物理使用不同频率。
- **时钟树操作与调试（P1）。** 增加 reparent/clone、路径校验、根节点查询、全树只读快照和文本/
  JSON 调试导出；定义节点被擦除、父节点销毁和时间缩放变化时的行为。评估逐节点暂停/缩放回调。
- **时钟语义扩展（P1）。** 评估时间缩放曲线（线性/缓动/混合）、全局暂停、暂停时是否继续渲染
  以及 `total_time` 在缩放变化后的连续性。任何扩展必须保持“有效 delta = real delta × 有效
  缩放，暂停时为零”的基本契约，或明确版本化。
- **确定性时钟与 pacing（P0）。** 提供可复用的 `steady_clock` 实现/适配器，并保持
  `tick_clock()` 可注入；用截止时间 + sleep-until 代替简单 `sleep_for`，记录过睡并补偿下一帧。
  测试使用合成时钟覆盖 30/60/144/无限、暂停、缩放、追帧和退出路径，不依赖 sleep。
- **统计窗口（P1）。** 增加滚动窗口、最大/最小帧、分位数、按阶段耗时和预算余量；保持查询
  路径 O(1) 或固定内存，并记录哪些操作可能分配。
- **验收标准。** 逐 stage 快照与外部计时一致；固定频率覆盖和禁用 stage 行为有测试；时钟树
  操作和 detach 语义有测试；合成时钟下 `run()` 逐帧可断言；pacing 误差在固定序列上精确可测；
  新增基准不回退，现有 66 个测试保持通过。

### v0.3 — 并行任务框架

**目标：** 在不破坏固定步长、时钟语义、确定性帧协议和单线程回退的前提下，把 stage/帧内工作
拆分为可依赖的任务图。该里程碑目前不实现，只定义边界、语义和验收方式。

- **任务模型（P0）。** 引入 `task`/`job` 的值语义描述、稳定句柄、依赖边和取消状态；调用方
  提交可调用对象，运行时不解释业务类型、不内建 ECS 或场景概念。任务节点应支持预分配或
  帧内复用，避免热路径分配。
- **工作线程池（P0）。** 支持可配置线程数、主线程参与策略、帧末 join 和关闭流程；禁止在
  `step_frame` 热路径创建/销毁线程。线程池必须可显式关闭，默认单线程回退与 v0.2 行为一致。
- **阶段映射（P0）。** 明确 poll、clock tree update、stage update、stage render、present 的
  同步点。时钟树更新必须先于任何读取时钟 delta 的任务；窗口/输入相关调用仍留在主线程；
  present 必须在所有依赖它的任务完成后执行。
- **依赖与调度（P0）。** 支持 DAG 依赖、提交时环检测、任务取消和错误传播。执行顺序可以并行，
  但提交顺序、完成 join 顺序和可见副作用必须有文档化保证；同一 stage 的多次 fixed update
  必须串行执行，不能并发修改同一逻辑状态。
- **确定性（P0）。** 相同任务图、相同输入、相同时钟状态和相同线程数下应产生相同的最终状态；
  如果需要逐帧完全可重放，提供确定性模式（例如固定 worker 数或单 worker 顺序执行），并明确
  其性能代价。
- **可观测性（P1）。** 每个任务的排队时间、执行时间、依赖等待、取消原因，以及 worker 利用率
  和主线程阻塞时间可进入统计探针；默认热路径不采集或只采集低成本计数。
- **内存与异常（P1）。** 定义任务分配器/arena 接口、异常或错误码策略，以及“任务抛异常后
  帧如何结束”的语义。stage 钩子目前不是 `noexcept`，任务框架不得悄悄改变该契约。
- **验收标准。** 单线程与多线程最终状态一致性测试；环检测、取消、异常和关闭竞态测试；
  TSAN 运行干净；1/2/4/8 worker 的扩展性基准；同一 stage 的 fixed update 顺序和时钟 delta
  在并行模式下保持不变；主线程阻塞时间可测。

### v0.4 — 集成、遥测与质量

**目标：** 让运行时可以接入真实窗口和渲染器，并具备可运维的性能数据。

- **平台适配示例（P0）。** 在示例中把 `platform` 的事件泵、计时器、窗口关闭状态和原生窗口
  生命周期接到 `frame_pipeline`。适配层放在示例或 `render-core`，不把 `platform` 链接进
  `runtime`；`runtime` 仍只接受时间、等待和退出条件。
- **渲染器适配契约（P0）。** 明确 stage `on_render(alpha, dt)` 与 pipeline `on_present()` 的
  职责、GPU 同步点、帧号传递、逐 stage 渲染顺序和动态分辨率/降载信号。渲染器只消费调度结果，
  不反向驱动时钟树或 scheduler。
- **逐 stage 遥测与调试图层（P1）。** 提供只读快照和可选的 CSV/JSON 导出；帧时间、预算、
  FPS、各 stage 耗时、时钟 delta、暂停/缩放状态和 worker 利用率可供 overlay 使用。绘制由
  示例、编辑器或调试 UI 完成，`runtime` 不依赖任何 UI 库。
- **确定性回放（P1）。** 结合 v0.2 的时钟和 v0.3 的任务图，支持固定输入、固定时钟、固定时钟树
  状态和固定 worker 配置的逐帧对比；记录 stage 提交顺序、任务完成顺序和可见副作用。
- **CI 与质量门禁（P0）。** macOS/Windows/Linux 构建与测试；ASan/UBSan/TSan；无警告构建；
  基准回归阈值；至少一个 headless 和一个窗口化示例在 CI 或本地脚本中运行。
- **验收标准。** 示例在三个平台启动并稳定运行；逐 stage 统计与外部计时器在固定序列上一致；
  sanitizer 干净；基准有可比较基线；文档说明哪些行为是跨平台保证、哪些是平台相关。

### v1.0 — 稳定 API 与引擎集成

**目标：** 冻结帧运行时接口，使其成为引擎主循环的稳定基础。

- **API 冻结（P0）。** 确定时钟树、stage、调度、统计、帧循环、时钟/等待和（若已实现）任务
  框架的公共接口；发布语义化版本策略和至少一个次版本的弃用过渡期。
- **安装/导出（P0）。** 提供 `find_package(cyberverse_runtime)`、CMake 导出目标、版本头和
  最小编译器/标准库要求；与其它 Cyberverse 仓库的打包方式一致。
- **主循环集成（P0）。** 与 `platform`、`render-core`、`camera`、`animation` 形成可运行的最小
  引擎循环示例：轮询、game/UI 双时钟、固定更新、相机/动画推进、渲染、呈现、统计和退出。
- **性能与稳定性预算（P0）。** 为 scheduler step、时钟树更新、stage 分发、统计记录和任务 join
  建立性能预算；CI 跟踪回归；长时间运行无累积漂移、无无界内存增长。
- **确定性与重放（P1）。** 固定输入、固定时钟树和固定任务图的录制回放可用于回归和性能对比；
  文档说明浮点、线程调度和平台计时带来的非确定性边界。
- **文档（P1）。** 补齐帧协议、时钟继承、预算语义、自适应控制器、线程模型、错误处理和集成
  契约的长期文档；README 与头文件 Doxygen 和实际行为保持一致。
- **验收标准。** 外部项目可通过 `find_package`/`add_subdirectory` 使用；公共 API 评审通过；
  主循环示例在目标平台运行；CI、sanitizer、性能门禁和文档全部就绪。

## 稳定性契约

以下语义属于现有公共契约，后续里程碑不得静默改变；如需调整，必须同时更新头文件 Doxygen、
README、AGENTS 和测试。

- 时间单位统一为秒。固定逻辑步长由 `target_logic_hz` 决定，默认为 `1 / 60`；非法配置在构造时
  归一化。
- `clock_node` 的有效暂停状态包含所有祖先；有效缩放是祖先链乘积；本地缩放 clamp 到 `[0,100]`，
  NaN 回退到 1；负值/非有限 real delta 按 0 处理；暂停节点 delta 为 0 且总时间不前进。
- `clock_tree` 路径使用 `/` 分隔，空段忽略；`""` 和 `"/"` 指向根；根名为 `"root"` 且不可擦除；
  擦除/清空会 detach 子节点。
- 每个 fixed-step stage 独享一个 `fixed_step_scheduler`；scheduler 不共享累加器。`step` 对负值/
  非有限 delta 按 0 处理，超过 `max_frame_time` 钳制，超过最大步数丢弃整步并置
  `dropped_backlog`，`alpha` 保持 `[0, 1)`。
- `target_render_hz == 0` 表示无限模式；`target_frame_time()` 返回 0，`should_throttle()` 恒为
  false。
- fixed stage 每帧执行零到多次 `on_update(logic_step, stage_tick)` 和一次
  `on_render(alpha, clock_delta)`；variable stage 每帧执行一次 `on_update(clock_delta,
  stage_tick)` 和一次 `on_render(1.0, clock_delta)`。
- `frame_stage_options` 的 update/render 顺序相互独立；按 `update_order`/`render_order` 升序执行，
  相同值保持注册顺序。`receive_update` 只门控 scheduler 与 `on_update`，`receive_render` 只门控
  `on_render`；生命周期钩子对所有 stage 执行。
- 帧顺序固定为：poll → clock tree update → pipeline `on_frame_begin` → stage `on_frame_begin`
  （update 顺序） → 全部 stage update（update 顺序） → pipeline `on_render_frame_begin` → stage
  `on_render_frame_begin`（render 顺序） → 全部 stage render（render 顺序） → stage
  `on_render_frame_end`（render 顺序） → pipeline `on_render_frame_end` → present → 统计/自适应。
  stage tick 局部递增，`frame_pipeline::tick_count()` 是实际执行的 stage update 调用总数。
- quit 语义固定：poll 退出跳过整帧；frame-begin/update 退出跳过剩余 update/render/present；
  render 阶段退出停止剩余 render，但仍执行 render-end 钩子、present 并记录统计；present 退出在
  呈现后观察。
- `tick_clock()` 返回自上次调用以来的真实秒数；`run()` 每轮调用一次，`step_frame(dt)` 不调用。
- `request_quit()`/`should_quit()` 是唯一的跨线程入口；其余状态只允许循环线程访问。
- 运行时热路径不创建线程、不分配内存、不做无界阻塞；任何会改变这一点的能力必须显式启用并
  在文档中标注。

## 非目标

- 不在 `runtime` 中实现窗口、输入、渲染、资源加载、场景或玩法逻辑。
- 不引入 GLM、`platform`、`render-core` 或 DiligentEngine 依赖；适配层放在示例或上层模块。
- 不实现动态分辨率、LOD、剔除或其他渲染降载策略；运行时只提供预算信号和调度结果。
- 不提供隐藏的全局线程池、后台线程或“自动并行化”；并行任务框架必须显式启用、可关闭，并
  保留单线程回退。
- 不在 v0.3 之前承诺任务框架的公共 API；相关实验不得泄漏到现有帧协议。
- 不定义资源/动画/场景的序列化格式；录制回放只覆盖运行时输入、时钟和调度状态。
- 不强制所有逻辑使用固定步长；variable-step stage 是给 UI、工具和不需要确定性的流程使用的。

## 待决问题

- **时钟树语义：** 有效时间缩放是祖先乘积，还是应在某个上限再次 clamp？父节点暂停时子节点是否
  允许局部强制恢复？时间缩放曲线如何与 `total_time` 连续性协调？
- **时钟节点生命周期：** 是否支持 reparent/clone？被擦除的节点是否应保留父链快照，还是像现在
  一样 detach？逐节点回调是否值得引入？
- **阶段顺序：** 已有独立 update/render 优先级和 receive 门控；是否还需要跨 stage 依赖、分组、
  稳定 id 或运行时修改选项？禁用 stage 是否仍 poll/render 生命周期钩子？
- **每 stage 频率：** 是否允许 stage 覆盖 `target_logic_hz` 和 catch-up 上限？如果允许，
  scheduler 配置应在 add_stage 时固定，还是可在帧之间修改？
- **逐 stage 统计：** 快照是每秒复制一次，还是提供增量/环形缓冲？统计是否进入 `frame_stats`
  还是独立的 `stage_stats`？
- **variable-step 语义：** `alpha` 固定 1.0 是否足够？UI 是否需要双缓冲/插值？ paused 时是否
  仍调用 `on_update(0)`？
- **时钟源形态：** `tick_clock()` 返回 delta 是否足够，还是应返回时间点由 pipeline 求差？是否
  需要内置 `steady_clock` 适配器和可注入等待器？
- **暂停语义：** 暂停时是否继续 poll/render/present？`alpha` 冻结在上一帧还是归零？时间缩放
  是否影响 `max_frame_time`、统计预算和自适应控制器？
- **自适应控制器位置：** 继续放在 `frame_schedule`，还是拆成独立的 pacing policy？阈值、步长、
  死区和恢复速率是否应可配置？
- **任务框架所有权：** `runtime` 自己创建 worker 池，还是接受外部 executor/线程池？stage 与
  任务图的映射如何保证同一 stage 的 fixed update 串行？
- **确定性级别：** 只保证最终状态，还是保证任务完成顺序、可见副作用和统计顺序？不同线程数
  下是否要求逐帧一致？
- **异常策略：** stage 钩子目前不是 `noexcept`；抛异常时应终止帧、传播到主线程，还是记录错误
  并跳过依赖任务？
- **平台集成方式：** 通过示例适配 `platform::timer`，还是让 `runtime` 提供一个可选的
  `platform` 后端？后者会破坏标准库-only 的依赖规则，需要明确取舍。
- **遥测格式：** 统计快照、CSV/JSON 或二进制环形缓冲？字段和版本如何保持稳定？

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
