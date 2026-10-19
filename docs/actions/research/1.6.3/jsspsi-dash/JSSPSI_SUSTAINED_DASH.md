# 祀持续特殊冲刺的实现依据（2026-10-18）

本篇记祀（`chr_0036_jsspsi`）持续特殊冲刺的运行机制、各项固定取值的依据、日志与验收、游戏更新后要核的项和已知边界。机制分析、资源解包和第一份原版录制在 Windows 客户端 1.6.3 上做，2026-10-18 在 1.6.4 上实机验证。1.6.4 只更新了资源（VFS 和配置 JSON），GameAssembly 与 1.6.3 是同一份文件，静态反汇编的结论照用；冲刺动画也没变（两版右侧原版录制逐骨比局部姿态，最大差 0.5°）。资源没有在 1.6.4 上重新解包，下文由资源解包得出的布料参数、控制器层和 timeRef 曲线都是 1.6.3 的结论；特效的寿命取自 1.6.3 的日志，在 1.6.4 上没有重新量过。头发、尾巴每圈甩一下的成因和附属件平稳循环见 [祀附属件平稳循环](JSSPSI_ACCESSORY_LOOP.md)。

祀的特殊冲刺骑一只墨兽、两侧各跟一只，整段剪辑 5.667 秒，起冲后约 3.3 秒身下墨兽跳开化成墨。持续冲刺沿用洁尔佩塔、梨诺共用的 Perform 保持和第 0 层 Animator 回绕（v9 路径），另外做四件祀专有的事：三只墨兽的 Animator 跟着回绕；按 actor 编号处理特效，保持一部分的寿命、不生成剪辑结尾的水花，第一次回绕时停掉起冲音效；身体用运行时自录的身体接缝差表做接缝修正；尾巴、后发用离线做好的附属件循环表按圈内相位整圈覆盖。祀没有 BEPOSE12 姿态数据。

下文 u 指祀第 0 层 `SpDash_L/R` 状态的归一化时间（normalizedTime）：0 是起冲，1 是剪辑结尾，时间 t = u × 5.667 秒。

**依据怎么读。** 正文括号里和表格「依据」列里的标签含义如下：

- 资源解包：离线解出的 1.6.3 Windows 客户端资源，包括动画控制器、剪辑参数轨道、动画配置、布料和挂件的预制体参数。
- 静态反汇编：1.6.3 Windows 客户端 GameAssembly 的机器码；结论里写 IL2CPP 的类名.方法名。
- 日志：动作模块的诊断日志，来自 1.6.3 和 1.6.4 上的实机运行。
- 实机观察：同这些实机运行中在游戏里看到的画面。
- 模块代码：读本模块源码得出；没有在游戏里验证过的，正文另外注明。
- 原版录制：录制模式（§7）录下的原版冲刺骨骼局部姿态（BECAP001）。1.6.3 右侧一份；1.6.4 右侧一份、左侧三份。
- 原版录屏：原版冲刺的画面录像，逐帧看。录屏与日志对时指把实机录屏和日志按时间对齐。
- 离线算、离线重放：用原版录制做正向运动学。口径是固定角色根、局部缩放按 1、取布料求解之前的骨骼原点或链末端点，不含网格顶点、物理、碰撞和角色在世界里的移动；厘米数是这些点在角色根坐标里的位移，不是网格顶点或世界轨迹。时间按 t = u × 5.667 算。角速度、评分、逐骨比较这类对多根骨的统计只算 375 根蒙皮骨；附属件表的解析和重采样误差核对按表里的全部骨算；链末端的位置按各节注明的定位点算。
- 数学推导：直接从公式推出。
- 推断：有依据，没直接测到。「推断，高把握」指依据充分、只差直接测量。

**实机验证的范围。** 1.6.4 Windows 客户端在 Linux 上经 Proton 运行。宿主是 Better Endfield 3.5.4，动作模块是本仓库这份逻辑按 3.5.4 的宿主接口重编的构建（模块标识、INI 节名和环境变量前缀换成 3.5.4 的），附属件表就是随包发布的 `accessory_jsspsi.bin`。左右两侧都冲了多次，观感正常，与附属件文档 §4.6 里全组表那次实机观察相同；日志见 §8。这个构建在正常冲刺路径上与本仓库代码相同：用同一组离线输入（首圈、三次回绕、0.4 秒淡出、两侧状态 hash、接缝修正开和关、三种 mask）重放，两者的写骨、回绕和资源计数轨迹逐字节一致。失效、异常、重入、退出和配置变化这些路径只做过离线测试。Better Endfield Next 本身没有进游戏验证；MSVC、NDK 和 Android 设备、WinUI、Java 都没有编译或运行。模块的方法、字段契约对照过 1.6.4 的游戏元数据：96 个方法、17 个字段里只缺已知可选的 `AnimatorStateInfo.get_loop`。

仓库内容能复现的是录制（录制模式）、身体接缝差表（模块自录）和蒙皮骨目录（`tools/CustomModel/catalog/chr_0036_jsspsi.json`）；上面说的日志、录屏和录制文件本身没有公开。

## 1. 冲刺结构

### 1.1 左右两个版本

| 项目 | 左 | 右 |
| --- | --- | --- |
| Perform | `CharIntPerform_Jsspsi_SpdashL` | `CharIntPerform_Jsspsi_SpdashR` |
| 第 0 层状态 | `SpDash_L`，hash `528430122` | `SpDash_R`，hash `-445642423` |
| 剪辑 | `A_actor_jsspsi_sprint_dash_sp_l` | `A_actor_jsspsi_sprint_dash_sp_r` |
| 时长 / 状态速度 | 5.667 秒（资源值 5.6666669846）/ 1 | 同左 |
| 状态 loop / 剪辑 LoopTime | 1 / 1 | 0 / 0 |
| timeRef 曲线在 u 0–0.8675054312 | 恒为 0 | 恒为 0.5 |

左右 loop 不同，对模块没有影响：模块在 0.315 回绕、u≥0.85 就放弃保持，会话里第 0 层到不了 1（模块代码）。

timeRef 是角色动画配置 `anim_cfg_chr_0036_jsspsi.json` 的 `timeRefCurves` 里按第 0 层状态给出的一条曲线。黑板按缓存的第 0 层状态和归一化时间求值，写进 Animator 参数 timeRefA 或 timeRefB（所选状态变了就换另一个）；叠加层 LoopClothAddLayer、LoopBodyAddLayer 的 Walk/Run/Sprint 状态拿它当播放时间，Idle 状态自己走时钟。祀两侧的曲线在 u 0–0.8675054312 是常数，所以这两层停在以所选通道为时间源的 Walk/Run/Sprint 状态、过渡已经稳定时，在循环区间里取固定相位，回绕不会让它跳变（资源解包；静态反汇编）。两层实际停在哪个状态，另一个 A/B 通道和过渡期间是什么情况，没有读运行数据。

解出的 `ac_chr_0036_jsspsi_optnew.controller` 带 ClothCombine 层，共 11 层；剪辑自带参数轨道 `DisableClothOverride=1`、`LoopClothAddWeight=1`、`LoopBodyAddWeight=1`、`MagicaClothWeight≈0.01`（资源解包）。运行时用的是这套 optnew 控制器（推断，高把握）：日志里黑板的 lean、leanRev 始终为 0，而普通版控制器 `ac_chr_0036_jsspsi.controller` 对应的黑板旧分支下 leanRev 恒等于 1−lean。运行实例的控制器指针没有读过。

- `module.cpp` 的 `CharacterProfile` 条目（下称档案）里，祀的 Perform 名是 `CharIntPerform_Jsspsi_Spdash`；`side_suffixed_perform` 让比对时允许末尾多一个 `L` 或 `R`，只对祀开，洁尔佩塔、梨诺仍要完全相同。
- 左右由游戏在起冲那一刻决定：`CharacterAnimationComponent.Dash` 读一个 Animator float 参数，大于 0.5 就给 `CharacterSpecialDashBrain.TryStartSpDash(bool)` 传 true，`CalculateIdToPlay(ids, flag)` 在 false 时取 `ids[0]`、true 时取 `ids[1]`（静态反汇编）。这个参数大概对应跑步时哪只脚在前（推断）。日志里两侧都常见。模块不重抽，会话中途第 0 层状态 hash 变了就结束保持。
- 左右不是镜像，数据要各做一份。骨架本身不对称：`L_sleeveI_01`–`03` 只在左边；上臂下的飘带骨左边是 ribbonB/F/I 共 13 根，右边是 ribbonC/E/H 共 12 根。左右两张身体接缝差表逐骨比同名骨的旋转差 D（定义见 §5），身体骨两侧中位只差 0.42°，飘带、袖子、尾巴、裙子中位相差 14.5°、10.8°、13.8°、5.5°（离线算），一侧的数据不能给另一侧用。

### 1.2 墨兽是三个 Perform 临时物体

- 三只墨兽不归祀的 Animator 管。它们是起冲时 ShowObject（logic 27，index 0–2）显示的 Perform 临时物体（`TmpObjectInst`），在 `CharPerformHandleBase.m_tmpObjects` 里，节点 `Attachment/Att_widget_jsspsi_inkbiter01_postmodel` 到 `…inkbiter03_postmodel`。0 号 inkbiter01 在祀身下，1、2 号在两侧（日志）。
- 每只有自己的 Animator，由 ObjectPlayAnim（logic 29）播 `Spdash_01`–`03`，片段同为 5.667 秒、与祀同步。跳开、化墨都在墨兽自己的片段里，起冲之后 Perform 不再发出动作（日志）。所以只回绕祀时，墨兽照样在 0.58 前后跳开（实机观察）。档案里两个字段管墨兽：`rewind_mount` 让身下墨兽和祀在同一帧回绕，`side_loop` 让两侧墨兽按自己的回绕区间（`kJsspsiSideLoop`）各自回绕，见 §3。
- 每只墨兽 63 个 Transform，身上有 LODGroup、Animator、PerformModelData、EntityRenderHelper 和 8 个 SkinnedMeshRenderer（日志）。墨兽的状态 hash 左右冲刺相同（日志）。

### 1.3 三步和片段里的时间点

原版冲刺按身下墨兽的步子分三段，墨兽约 1 秒走一步。第一步到 u 0.126（起冲后 0.71 秒），是起步和祀骑上墨兽；第二步 0.126–0.315（1.07 秒），持续冲刺循环的就是这一步；第三步从 0.315 起是收尾，两侧墨兽 0.38 起往中间收拢，身下墨兽 0.47 起变化、约 0.58（起冲后约 3.3 秒）跳开散成墨。从起冲到约 0.40 叫骑乘段，之后墨兽收拢、身下墨兽准备跳开。

| u | 事件 | 依据 |
| --- | --- | --- |
| 约 0.10 | 两侧墨兽成形 | 原版录制 |
| 0.126 | 第一步结束（起冲后 0.71 秒），循环圈首 | 原版录屏逐帧 |
| 0.126–约 0.25 | 尾巴、后发、袖子仍有不按步态重复的大摆（推断为上墨兽的余势） | 大摆：原版录制离线算；成因：推断。见附属件文档 §1.1 |
| 0.315 | 第二步结束（起冲后 1.78 秒），循环圈尾 | 原版录屏逐帧 |
| 0.38 | 两侧墨兽开始往中间收拢 | 原版录制 |
| 约 0.40 | 骑乘段结束 | 原版录制 |
| 0.47 | 身下墨兽开始变化 | 原版录制 |
| 约 0.58 | 身下墨兽跳开、散成墨 | 原版录制 |
| 0.85 | 模块的回绕判定在此之后放弃保持 | 模块代码 |

## 2. 组成，以及为什么不用 BEPOSE12

洁尔佩塔、梨诺用的 v12 姿态覆盖是：离线把剪辑还原成骨骼姿态、做成闭合循环（BEPOSE12），在 TailLateTick 之后按自带时钟整套写骨。祀不走这条路，原因有四条：

1. 墨兽不在祀的 Animator 根下，姿态覆盖只绑这个根下的骨，墨兽仍得靠自己的 Animator 回绕。覆盖层按自己的时钟（每帧累加 delta）取样，墨兽跟着各自 Animator 的回绕走，两者之间没有锁相，只要两边的时间推进有一点不一样，偏差就一圈圈累积；两侧墨兽的一圈（1.139 秒）本来就和祀（1.071 秒）不同（§3）。要对位，就得把墨兽也做进数据，或者另加同步（推断）。照上游打包脚本按 60 fps 整帧存，1.0711 秒的一圈存成 64 帧（1.0667 秒），每圈还要再差 4.4 ms；这一项可以靠 BEPOSE12 头里的浮点 fps 字段消掉。
2. 整套覆盖把被写骨上的实时动画层换成录制那一次冲刺的样子。optNew 里接着转向侧倾 BodyLean、注视 Lookat、受击 AdditiveHurt 等层（资源解包）；冲刺中它们输出多少没有测过。
3. 骨上特效的对位要另补，梨诺为同类问题加过 `EffectInstance.ManualUpdateFollow()`（见 [梨诺持续冲刺的机甲显隐与光效挂点](../../1.5.3/liino-dash/LIINO_DASH_VISUAL_FIX.md)）。
4. 要换掉的只有附属件的动作。身体经接缝修正后，接缝处的速度跳变不大（§5）；甩来自尾巴、后发等附属件每圈重播的动作内容（附属件文档 §1）。

| 层 | 做什么 | 数据 | 见 |
| --- | --- | --- | --- |
| Perform 保持 | 与洁尔佩塔、梨诺共用：拦掉自然结束，按住冲刺就一直保持 | — | [特殊冲刺动画持续播放](../../../SPECIAL_DASH_CONTINUOUS_ANIMATION.md) §1；1.6.3 改挂的入口见 [1.6.3 Windows 挂钩与资源更新](../../../../host/research/1.6.3/hook-update/GAME_UPDATE_1_6_3.md) |
| 第 0 层回绕 | 祀和身下墨兽 0.315→0.126，两侧墨兽 0.317→0.116 | 固定常量 | §3 |
| 特效与声音 | 保持 0–5、13 号的寿命，不生成 10–12 号，停墨兽渲染特效的钟，停起冲音效 | 固定常量 | §4 |
| 接缝修正 | 祀和墨兽每根骨按首尾差在接缝两侧各补一半 | 身体接缝差表 `seam_jsspsi.bin`，运行时自录 | §5 |
| 附属件覆盖 | 尾巴、后发按圈内相位整圈覆盖 | 附属件循环表 `accessory_jsspsi.bin`，随包发布 | 附属件文档 |

`external_loop` 对祀不起作用：祀的档案没有姿态数据文件（`pose_file` 为空），`external_loop=true` 时完整姿态覆盖对她直接跳过，不去找 `pose_jsspsi.bin`；接缝修正和附属件覆盖照常运行。

配置在 `%LOCALAPPDATA%\BetterEndfieldNext\BetterEndfieldNext.ini` 的 `[betterendfieldnext.actions]` 节，示例见 [特殊冲刺动画持续播放](../../../SPECIAL_DASH_CONTINUOUS_ANIMATION.md) §4。`characters` 里有 `jsspsi` 就处理祀的冲刺。Windows 启动器「冲刺持续」页的「祀」开关写这一项，默认关。Android 设置页的「祀」开关：从没保存过洁尔佩塔、梨诺选择的（新装）默认开，保存过的（从旧版升级）默认关，保存过祀的选择时以保存值为准；Android 的冲刺持续总开关默认关。手写的 INI 没有 `characters` 键时，按上游的兼容规则所有支持的角色都开，包括祀。

下面五个键只在 INI 里，界面上没有，省略时取默认值。在 Windows 启动器的「冲刺持续」页拨任一开关，或者点「保存」「保存并启动」，启动器都会重写这一节，这五个键和 `external_loop` 原样保留；启动器自己不写 `external_loop`，缺省为 false。Android 设置页生成的配置固定写 `external_loop=true`（只影响有姿态数据的洁尔佩塔、梨诺），不写这五个键，模块按默认值运行。

| 键 | 默认 | 作用 |
| --- | --- | --- |
| `jsspsi_seam_fix` | true | 接缝修正，祀和墨兽一起开关 |
| `jsspsi_accessory_loop` | true | 附属件覆盖总开关 |
| `jsspsi_accessory_mask` | `0x03` | 按组开附属件覆盖，各位含义见附属件文档 §2.4 |
| `jsspsi_accessory_fade` | 0.4 | 松手淡出秒数，0–1，0 为不淡出 |
| `jsspsi_capture` | false | 录制模式（§7） |

`jsspsi_accessory_mask` 写十进制或 `0x` 开头的十六进制（`010` 按十进制读成 10）；`jsspsi_accessory_fade` 取 0–1 之间的有限数，整段文本都要是数字（`0.4junk` 无效）；布尔键只认 `true`、`false`、`1`、`0`。已识别的键有一个写错，整节判为无效，三个角色的持续冲刺全部关掉，日志 `Invalid action configuration; special dash disabled.`；`enabled=true` 而没有 `schema_version`（1–3）时也一样。不认识的键名不报错，按没写处理。

其余取值（回绕区间、淡入、接缝混合宽度、特效编号、停声音的淡出）都是常量，与实机验证时相同。配置的相等比较逐字段进行：解析出的配置只要有一个字段和当前的不同，模块就发布新的配置快照，附属件开关关着时改 `jsspsi_accessory_mask`、`jsspsi_accessory_fade` 也算。祀的冲刺遇到新快照就以 `configuration changed` 结束；洁尔佩塔、梨诺的冲刺在只有这五个 `jsspsi_*` 键变化时继续，并改用新快照，其余键变化时照旧结束。

## 3. 回绕点

| 对象 | 回绕点 begin → 目标 target | 一圈 period | 回绕规则 |
| --- | --- | --- | --- |
| 祀 | 0.315 → 0.126 | 0.189，即 1.071 秒 | 当前是左右冲刺状态、不在过渡、u<0.85 且已过回绕点时发 CrossFade，之后确认落点（见下） |
| 身下墨兽 | 与祀同一帧 | 同上 | 用祀算出的目标，不读自己的时间，不确认落点 |
| 两侧墨兽 | 0.317 → 0.116 | 0.201，即 1.139 秒 | 各读自己的第 0 层：不在过渡、u 在回绕点到回绕点+0.2 之间、距上次回绕至少 300 ms |

CrossFade 用的是 `UnityEngine.Animator.CrossFade` 的 8 参数重载（`Int32, Single, Int32, Single, Single, AnimationBlendStyle, AnimatorTransitionInterruptionSource, Boolean`），参数为 `(当前状态 hash, 淡入, 层 0, 目标+(u−回绕点), 0, HermiteCubic, CurrentThenNext, false)`。目标保留越过回绕点的那点超量，例如在 0.316 发出回绕，目标就是 0.127（模块代码；日志）。淡入就是上游回绕常量里的 blend，取归一化时间 0.05（约 0.28 秒）；接缝修正对这个对象处于应用状态时改用 0，也就是硬切（§5）。固定值写在 `native/modules/actions/dash_policy.h`：`kJsspsiLoop{.315f, .126f, .05f, .315f - .126f}`、`kJsspsiSideLoop{.317f, .116f, .05f, .317f - .116f}`，字段依次是 begin、target、blend、period。

- **落点确认**（祀）：CrossFade 发出后进入等待，等待期间不再发新的回绕。之后的每个 tick，等第 0 层不在过渡、u 已小于发出时的 u 减半圈，就检查落点：落在 [目标−0.04, 目标+0.05+0.08] 之内（目标按发出时的 u 算，0.05 是回绕常量里的 blend）算确认，否则结束保持；等待期间累计的 delta 超过剪辑长度 × 0.5 + 0.5 秒（祀约 3.33 秒）还没确认，也结束保持。两种结束都先打 `Sustained dash v9 loop rejected: …`，结束原因是 `loop interrupted, late, or not acknowledged`（模块代码）。
- **圈首、圈尾**：0.126、0.315 是第一步、第二步结束时的两个姿势，在原版录屏里逐帧对出来（§1.3）。回绕区间不往后延，因为 0.38 起两侧墨兽开始收拢、0.47 起身下墨兽开始变化。
- **祀和身下墨兽共用回绕区间**：她骑在身下墨兽上，两边的姿态要对齐，所以同一帧回绕。
- **两侧墨兽单独一个回绕区间**：它们一直在走先散开、再收拢的弧线，用祀的区间时接缝处位置差半米多；0.317→0.116 时骨架差 15°、骨盆差 11 cm（原版录制离线算）。这个区间在它们成形之后、收拢之前。
- **约束**：回绕点要早于 0.85。两侧墨兽的一圈要长过两次回绕之间的 300 ms 限流，短了回绕跟不上、相位一路往后漂（日志）。两侧墨兽每圈比祀长 0.068 秒，相对祀的相位每圈漂一点，这是分开回绕区间的代价。
- **第一次回绕时的附带动作**：第一次 CrossFade 调用成功后（不等落点确认），停起冲音效、停墨兽渲染特效的钟、身下墨兽同一帧回绕，并开始保持特效寿命（§4）；附属件覆盖也从这一帧起生效（附属件文档 §2.3）。只回绕第 0 层，另外 10 层模块不碰；timeRef 在循环区间是常数，叠加层停在 Walk/Run/Sprint 状态、过渡已稳定时，回绕前后拿到的播放时间不变（资源解包；§1.1）。

## 4. 特效与声音

起冲时 Perform 一次放出 14 个特效：actor 0–12 是 `P_fxbat_jsspsi_sprint_dash_sp_start_01`–`12`（1、2 号都是 start_02），13 号是 `P_fxbat_jsspsi_sprint_dash_sp_Bip001_jnt`。每个都按自己的寿命（`EffectInstance.DurationTick`）在起冲后固定时刻结束，不跟动画走（日志）。

| actor | 是什么 | 原生结束 | 处理 | 原因 |
| --- | --- | --- | --- | --- |
| 0–2 | 墨兽身上的渲染特效，只有 EffectSetting、没有粒子，给墨兽墨色外观；0 号在身下墨兽，1、2 号在两侧 | 1 号 3.16 秒，2 号 3.39 秒 | 第一次回绕起停寿命钟 | 到寿命会撤掉外观 |
| 3–5 | 同类渲染特效：起冲时让墨兽从墨里成形，之后给墨色质感；3 号在身下墨兽，4、5 号在两侧（推断） | 5.66 秒 | 停寿命钟，并停渲染控制器的钟（§4.1） | 约 5.4 秒会自己进入收尾、把墨兽化掉，收完撤材质；也不能不放：不放的话墨兽起冲就是没有墨色外观的黑色模型（实机观察） |
| 7 | 起冲水花 | 1.24 秒 | 照原生 | 第一次回绕前已结束 |
| 10–12 | 剪辑结尾的水墨水花：收尾时祀踩墨兽、墨兽跳开那一下溅起 | 4.43 / 5.33 / 5.73 秒 | 持续冲刺从起冲就不生成；录制模式照原版生成 | 持续冲刺只有开始和循环的过程，墨兽不会跳开，没有这个结尾 |
| 13 | 祀脚下的拖尾，8 个 ParticleSystem | 6.34 秒 | 改成循环；第一次回绕起停寿命钟 | 长冲刺里到寿命会断 |
| 6、8、9 | 其他起冲特效 | 6.34 / 6.77 / 6.70 秒 | 照原生 | 渲染部分在 6.0–6.4 秒停，不影响墨兽 |

**不生成 10–12 号**：模块挂着 `EffectPlayAction.OnPlay`（梨诺的退场处理也挂这个），在调原函数之前判断：模块开着、不在录制模式、`characters` 选了祀；动作的 actor 编号在跳过位图 `0x1C00`（10–12，`kJsspsiSkipEffects`）里；动作所属 Perform 的名字是祀的（允许 L/R 后缀），实体有效且是操控角色。全部满足时不调原函数，这个特效就不会生成。任一条不满足、契约不可用，或者身份读取失败时，照原生放；普通托管错误不额外记录这条日志，只有抛出 C++ 异常被 catch 捕获时才打印 `Sustained dash Jsspsi effects: splash identity failed; native action retained.`。这一步不要求模块已经建立会话：这几个动作在原生 StartSpDash 里就播了，那时会话还没建立（模块代码）。所以第一次回绕前就松手的冲刺，也没有这三个水花。`characters` 没选祀时，这一步不读动作的任何字段。

**保持寿命**：保持名单是位图 `0x203F`（0–5、13，`kJsspsiHoldEffects`），按 actor 编号认，不核对特效名。会话进入保持后，有粒子的实例改成循环并重新播放，没有粒子的只记下、等着停寿命钟（日志 `lifetime prepared … (no particles)`）；寿命钟从第一次回绕才开始停，因为从起冲就停会连 1、2 号自己 0.39 秒的起始延迟一起停住，它们就出不来。冲刺结束或原生销毁实例时恢复粒子设置。

容量限制和编号是两回事：编号对了，也可能因为容量不处理。每个特效实例最多处理 16 个 ParticleSystem，超过时整个实例不保持，寿命钟也不停，日志 `Sustained dash v9: particle enumeration failed; VFX hold skipped.`，这次冲刺不再重试。停钟名单最多收 8 个 `EntityVFX`，满了不再加，也不报错（模块代码）。

### 4.1 墨兽渲染特效的钟

- 渲染特效控制器（溶解 `EntityVFXDissolveController`、附加材质、设参数、残影，基类是 Rendering.Beyond.dll 的 `EntityVFXControllerStateBase`）按 Start → Loop 或到时长自动 Stop → Ending → 撤材质推进。每帧过了多少时间由 `EntityVFXControllerBase.GetDeltaTimeFromLastTick` 按 `Time.time` 自己算，不看 `EntityVFX.Tick` 传入的 delta，也不看特效寿命（静态反汇编）。所以只停寿命、或把 `EntityVFX.Tick` 的 delta 改成 0，都拦不住 3–5 号的收尾。
- 模块从第一次回绕起做两件事：给三只墨兽的 `EntityRenderHelper`（`TmpObjectInst.m_entityRenderHelper`）名下、以及由祀的特效启动的 `EntityVFX` 传 delta 0；挂 `GetDeltaTimeFromLastTick`，原函数照跑（更新它自己的时间戳），控制器所属的对象是这三个 `EntityRenderHelper` 之一时返回 0。墨兽外观停在第一次回绕那一刻；按住 8 秒的冲刺里 3–5 号没有进入收尾（实机观察）。会话结束后钟照常走。
- ECS 版的附加材质控制器直接用 `Time.deltaTime`，这个办法管不到；墨兽没有用它（静态反汇编）。

### 4.2 起冲音效

- 祀的动画在起冲约 0.02 秒时投递一个声音事件（日志里是 Wwise 事件 1063030539），是照原生 5.7 秒配好的一整段（日志）。墨兽的脚步声在这段声音里（实机观察）。循环以后，这段声音仍按原生时间轴放，墨兽的脚步声和画面对不上，所以第一次回绕时停掉。
- 游戏的声音都经 Audio.Beyond.dll 静态的 `Beyond.Audio.AudioAdapter._PostEvent`，返回 playing id；冲刺期间投递的大半是队友的脚步声。模块按投递来源认：投递它的 `AnimatorMono` 所在物体名含 `jsspsi` 或 `inkbiter`（档案的 `owned_name_token`、`companion_name_filter`，不分大小写），或者是祀的实体经 `AudioManager.PostEvent` 投递；取第一次回绕前这样投递的第一个 playing id，不按事件 id 认。
- 第一次回绕时调 `AudioAdapter.StopByPlayingId(id, 300)`，淡出 300 ms。代价是这段声音在回绕之后的部分都不再播放，包括脚步声（实机观察）。

### 4.3 网格组隐藏

档案沿用梨诺的规则（`defer_mesh_group_hide`）：保持期间，同一 Perform、同一实体对 MeshGroup1/2 的隐藏被跳过，原生 Clear 照常恢复。日志里祀的会话没有触发过这条（结束日志的 `hide_deferrals` 都是 0）；墨兽是靠 §1.2 的 Animator 回绕留住的，不是网格组。没有测过关掉它的情况，所以档案里仍开着。

## 5. 接缝修正

圈首、圈尾的姿态本来就对不上：髋（`Bip001` 的局部高度）在 0.126 时 102.1 cm、0.315 时 110.9 cm，差 8.8 cm；头的位置差 12 cm，上身朝向差 24–28°（原版录制离线算）。硬切会跳，淡入只是把这一跳抹开。

**做法**（祀是对象 0，三只墨兽是对象 1–3；祀和身下墨兽用祀的回绕区间 0.126–0.315，两侧墨兽用自己的 0.116–0.317）：

- **绑定**：每个对象从自己 Animator 的根节点枚举全部 Transform（含未激活的），按名字排除 `GEN_` 开头、`Funnel` 开头和 `lineJoint`，布料骨和碰撞体都在内。枚举要完整：节点超过 4096 个，或者任一节点读名字、pin 失败，这次绑定就算失败。写骨前要拿到这棵树的写骨租约（PoseLease），每个根在一次冲刺里只申请一次，拿不到的对象这次冲刺不修正，日志 `Sustained dash Jsspsi pose: root <n> unavailable; no bone writes for this dash.`。墨兽在 Perform 开始后才生成：从这次冲刺第一次进入接缝修正起 1.5 秒内，没绑上的对象每帧重试，过了 1.5 秒仍没绑上的，这次冲刺不修正。
- **记录**：表里没有（状态 hash、目标、回绕点）这一键，且这一圈首次越过目标时 u 不超过目标+0.03，就进入记录：在首次 u≥目标的那一帧读每根骨的局部姿态 `first`，首次 u≥回绕点的那一帧读 `end`，存旋转差 D = first ⊗ end⁻¹（w 取非负）和位置差 dp = first − end。记录的那一圈仍按 0.05 淡入回绕，会跳一下；之后的圈、之后的冲刺和下次启动都直接用表。
- **应用**：圈内进度 p = clamp((u−目标)/(回绕点−目标), 0, 1)，接缝混合宽度 w=0.5（占一圈的比例），s(x)=x²(3−2x)。p>1−w 时补 +½·s((p−(1−w))/w)；这一圈是回绕来的且 p<w 时补 −½·(1−s(p/w))；其余为 0。旋转左乘 slerp(单位, D, |份额|)（负份额取共轭），位置加 dp×份额，只写局部位置和旋转，不写缩放；差几乎为零的骨跳过。w=0.5 时两半盖满整圈，只有 p=0.5 不改。两边在接缝处是同一个姿态，所以回绕直接硬切；每根骨离原版最多偏 D 的一半，而且只在接缝那一刻。每次冲刺第一次自然到达圈首的那一圈不补圈首那一半。
- **不累积**：读当前姿态时，某个通道若仍是模块上一帧写的值，就换回记下的原生基线，免得动画不每帧写的通道把修正一圈圈叠上去。
- **两个入口**：`BeyondDynamicBone.ClothManager.ClothUpdate` 之前和 TailLateTick 之后各有一个入口，按 `Time.frameCount` 去重。布料按设置可以在 LateUpdate 之前（OnBeforeLateUpdate）或之后（OnAfterLateUpdate）跑；跑在之前时还没到 TailLate，布料入口保证布料读骨之前已经补过（模块代码）。默认设置下布料在 TailLate 之后才跑，日志里一直是 TailLate 先做（静态反汇编；日志）。附属件覆盖接管时，布料入口让给 TailLate 统一做。

**补哪些骨**：全部，含布料骨。布料每帧在 TailLate 之后读骨头当模拟底子，布料骨自己的动画姿态在接缝处照样跳；只补主骨架（`Bip001*`，110 根）或补到 232 根不含布料骨的动画骨时，布料部位照样跳（实机观察）。TailLate 里写的骨会显示：把 `Bip001_Spine1` 多转 40°，画面上明显歪（实机观察）。

**身体接缝差表**：存在 `modules/actions/seam_jsspsi.bin`（格式见附录 A），不随包发布，删掉就重录。按（状态 hash、目标、回绕点）找表，容差 5e-4；每次新冲刺检查文件修改时间，变了就重读。生成随包附属件表时用的那份差表（附属件文档 §3.2 列了它的 SHA-256）有 5 条：祀左右各一条（638 个名字，超过极小差阈值的左 395、右 408），身下墨兽一条（63 个名字，32 根非零），两侧墨兽各一条（63 个名字，非零 35、28）；三条墨兽的表左右冲刺共用。这是一次录制的样本统计，重新录制时名字数和非零数不保证相同：绑定到哪些 Transform 随装备变（附录 B），非零与否取决于记录的那两帧。表可以长期复用：同一右侧，表与另一次独立录制的端点差最多 3.15°；两次左侧冲刺的首圈端点差，身体骨最大 0.36°（离线算）。

**和附属件覆盖的关系**：被附属件覆盖接管的骨，对象 0 的接缝修正整次冲刺都不碰（从第一圈匹配到表、绑好骨起），其余骨和三只墨兽照常修正；为什么要整次冲刺让出，见附属件文档 §4.3。同一次冲刺里，对象 0 的接缝修正和附属件覆盖共用祀 Animator 根的那一份租约。`jsspsi_seam_fix=false` 时祀和墨兽的接缝修正一起关。

**边界**：

- 只闭合位置，不闭合速度：smoothstep 两端斜率为 0，修正项在接缝处不带速度（数学推导）。身体在接缝处的速度跳变，髋、头、左手是 8–17 cm/s，右手例外，79 cm/s（离线重放）；附属件链末端的速度突变见附属件文档 §1.2。
- 记录的端点是首次越过目标、回绕点的那一帧，不是精确的 0.126、0.315。
- 录表那一圈经 0.05 淡入进入下一圈，丢了「回绕来的」标记，第一个应用圈不补圈首那一半。
- 冲刺结束时不还原接缝修正写过的骨姿。
- 表按骨的叶名作键：重名的 `L_skirtA_02_jnt_Collider` 两个节点共用一条；表里没有的名字跳过。
- 键里不含骨的集合，也不含剪辑内容，表也不记游戏或动画的版本。改了要补哪些骨，或游戏更新改了冲刺剪辑而状态 hash 和回绕区间没变，旧表都会被继续使用，没有任何提示。两种情况都要换文件头的版本号让旧表作废重录；不改代码时，只能在发布说明里让用户删掉 `modules/actions/seam_jsspsi.bin`，让模块重录。

## 6. 一帧之内的顺序和布料

设第 N 帧是模块发出回绕的那一帧：

| 阶段 | 发生什么 | 依据 |
| --- | --- | --- |
| EarlyUpdate 末 | 布料把自己的骨还原成注册时的初始局部姿态（`UseAnimatorTransform=false` 时） | 静态反汇编；开关运行值没读 |
| Update | 黑板按上一帧缓存的第 0 层状态算参数，比第 0 层晚一次求值；同侧稳态循环里祀的 timeRef 是常数，这一拍延迟不造成跳变 | 静态反汇编；资源解包 |
| PreLateUpdate | Animator 求值、混合 11 层、写骨；模块的 PreLate 钩子读第 0 层，到回绕点发 CrossFade，再跑原生 PreLateTick | 推断（按 Unity 默认顺序）；模块代码 |
| LateUpdate 脚本 | FinalIK（与 TailLate 谁先没定）；原生 TailLateTick（`ExcuteIKPosetSolver` 平移 `IK_Root`，预约布料之后的挂件求解）；然后模块依次做完整姿态覆盖（档案有姿态数据文件的角色才写骨，祀没有）、附属件准备、接缝修正、附属件覆盖、松手淡出、录制 | 静态反汇编；模块代码 |
| 布料 | `ClothManager.ClothUpdate`（`OnAfterLateUpdate`）：读当前骨 → 模拟 → 按物理权重混回骨头 | 静态反汇编 |
| 布料之后 | 本帧预约了、开着执行开关、driver 已烘焙且权重为正时，`JsspsiRibbonSolver` 按 driver 重算 8 根 driven 骨；回绕那一帧实际有没有执行，没有测 | 资源解包；静态反汇编 |
| 下一次正常求值（推断为第 N+1 帧） | CrossFade 生效：回绕那一帧的 TailLate 仍读到 0.316，下一次读到 0.130 | 日志（逐帧采样）；帧归属是高把握推断 |

**布料**：

- 头发、尾巴、袖子、裙子是同一套布料的 11 件 `BeyondBoneCloth`：`MC_jsspsi_` 加 Hair_Front_Short、Hair_Front_Long、Hair_Back、Tail、sleeve_L、sleeve_Ribbon_L_01、sleeve_Ribbon_L_02、sleeve_R、sleeve_Ribbon_RU、Skirt、Skirt_Ribbon，都在组件的 `m_boneCloths`；老 MagicaCloth 的两张表和 Static 表都是空的（日志：运行时枚举组件）。
- 布料默认读当前 Transform（`UseCrossFrameJob=true`、`UseAnimatorTransform=false` 是编译默认值），能读到 TailLate 写的值；写回时按 clothSimulateWeight × clothLodFadeWeight 在当前值和模拟值之间插值（静态反汇编）。
- 11 件都是 `animationPoseRatio=1`（约束的参考姿态就是当前动画），都开着角度恢复；前短发、前长发、尾巴还开着角度限制（尾巴限角曲线从 40 降到约 4.71756）；恢复强度曲线末值尾巴约 0.26650、两袖（sleeve_L/R）约 0.06710（资源解包）。
- 布料物理权重是黑板 `_UpdateMagicaCloth` 按 Animator 参数 MagicaClothWeight/Add/Force 算出、逐件下发成布料 clothSimulateWeight 的那个值，也就是上一条写回比例里的第一个因子；开了覆盖就用覆盖值，变化超过 0.001 才逐件下发（静态反汇编）。冲刺中约 0.01，推断来自剪辑自带的 MagicaClothWeight 轨道（§1.1）；起冲后 1.5–2.5 秒才从约 0.8–0.95 降下来，第一圈结束时还在 0.04–0.18（日志）。模块不覆盖物理权重；把它调到 0.9 为什么治不了头发、尾巴，见附属件文档 §1.3。
- 原生权重下，画面基本就是模块在 TailLate 写下的姿态，这是离线数据能预测画面的前提，但有检查范围的限制。在 Canvas 渲染前回读写过的蒙皮骨，与写入值的最大差随物理权重变化：0.011 时 0.5°，0.015 时 0.6°，0.043 时 1.7°，0.177 时 8.5°，0.9 时 39–41°（日志）。这是圈尾抽检：只查圈内进度 ≥0.85、每次冲刺的前三圈，每次最多列 12 根，没有全骨、全相位的保证；原生权重下另有几次写后变化达 5–10°，原因不明（日志）。另一个试验构建不写布料骨，回读时始终没有骨被改；在模块写骨之后、Canvas 检查之前，`BipedIK.UpdateSolver` 的调用计数为 0；所以改动者是布料写回（推断，高把握）。
- 挂件求解器只有一个 `JsspsiRibbonSolver`，`bIsExcuteAfterMC=1`。8 根 driven 是 `L/R_collar_01_jnt`、`ribbonH_01_jnt`、`L/R_clothA_01_jnt`、`L/R_ribbonG_01_jnt`、`ribbonI_01_jnt`，不在头发、尾巴、两袖（sleeveD）链上，也不是这些链根骨的祖先（资源解包）。

## 7. 录制模式

`jsspsi_capture=true` 时，之后每次冲刺都录一份原版动作。怎么录、怎么按左右另存，见 [祀附属件循环表生成工具](../../../../../tools/actions/jsspsi_accessory/README.md) 的「准备」和「输入」；这里只记录制模式做什么和录制里的坑。

- 录制的这次冲刺祀不回绕（回绕点抬到 2，`kCaptureNoRewind`），两侧墨兽不回绕，接缝修正和附属件覆盖都不跑，Perform 保持照常；动画按原版播到 0.85，模块在那里结束这次冲刺。停寿命钟、停渲染特效的钟、停起冲音效都在第一次回绕时才做，录制时都不发生；10–12 号照原版生成。保持名单里有粒子的 13 号仍会在第一圈改成循环（这一步不看录制开关），寿命钟不停（模块代码）。
- 骨表在冲刺开始时建一次：祀和当时已经生成的墨兽各自根下的全部 Transform（含未激活的，每个根最多 2048 个）。之后不再补，当时不在的墨兽、途中新增的节点都不进文件。此后每个 TailLate、当前是特殊冲刺状态时记一帧，最多 900 帧；冲刺结束时写 `modules/actions/capture_jsspsi.bin`（格式见附录 B），每次覆盖同一个文件。录制的那次冲刺会明显卡顿（实机观察）。
- 日志有 `Sustained dash Jsspsi capture: armed for jsspsi: bones=… (loop disabled for this dash).` 和 `Sustained dash Jsspsi capture: written: bones=… frames=… ok=1`，都不受 `diagnostics` 控制。bones 应等于角色组的 Transform 数加三只墨兽各 63（1.6.4 带专武时是 697 + 3×63 = 886）；只有 armed、没有 written，只能确认没有完成写出；正常结束时可能是一帧未录到（例如 TailLate 钩子缺失），模块关闭或功能异常也会直接丢弃已有帧。结合异常和关闭日志排查，并确认 written 的 ok=1。`Component.get_gameObject`、`Transform.get_parent`、`UnityEngine.Transform` 类型缺一个就不录，日志 `Sustained dash Jsspsi capture: Unity helpers unavailable; nothing recorded.`；找不到数据目录时是 `Sustained dash Jsspsi capture: data directory unavailable; nothing recorded.`。生成附属件表只用祀的那一组骨（分组 0）。
- 录在模块写骨之后、布料之前，录到的是动画姿态。原生权重降下来以后它基本等于画面；起冲后 1.5–2.5 秒内权重还高，那段画面和录制不完全一样。
- 文件里没有状态 hash，分不出左右，要靠日志里那次冲刺的 `selected state=left/right` 区分；这一行只在 `diagnostics=true` 时打印。
- 算速度一律用动画时钟 t = u × 5.667。录制里的秒数是系统时钟（`GetTickCount64`），游戏把动画帧增量夹在 1/15 秒，掉帧时两者对不上：0.126 所在的那个帧间隔是 66.67 ms 动画对 17 ms 系统时钟，用系统时钟求导，圈首一侧速度被放大 3.92 倍。
- 录制有空洞：1.6.3 的右侧录制在循环区间里有 9 处帧距超过中位数 2 倍，最大一处 66.67 ms 正压在 0.126 上（u 0.1158→0.1276）。
- 以后若给祀加 BEPOSE12 数据：完整姿态覆盖在录制之前写骨（§6 的顺序），录制时要先关掉它，否则录下的是覆盖后的姿态。
- 卸载器不删 `capture_jsspsi.bin` 和 `seam_jsspsi.bin`：这两个文件是模块运行时写出的，不在安装清单里。

## 8. 日志与验收

日志在 `%LOCALAPPDATA%\BetterEndfieldNext\logs\BetterEndfieldNext.log`。开着 `diagnostics=true`（默认）时，一次正常的冲刺按下表的先后出现这些行（1.6.4 实机日志）；注明「不受 diagnostics 控制」的，关了诊断也打印：

| 时机 | 日志 | 说明 |
| --- | --- | --- |
| 模块加载 | `Sustained dash Jsspsi contracts: hooks=6/6 tmp=1 hierarchy=1 vfx=1 clock=1 sound=1 seam=1 accessory=1 splashes=1` | 祀专用的可选契约，各项见下表；不受 diagnostics 控制 |
| 起冲，附属件表第一次读或文件有变化时 | `Sustained dash Jsspsi accessory: table loaded entries=2`，随后每个 entry 一行 `Sustained dash Jsspsi accessory: entry state=… begin=0.315000 target=0.126000 N=… M=…` | 出现在 armed 行之前；N、M 是这一侧的样本数、骨数（附属件文档 §3.2）。读不进来是 `Sustained dash Jsspsi accessory: table accessory_jsspsi.bin: rejected: <原因>`，同一状态只打一次 |
| 起冲 | `Sustained dash armed for jsspsi: left_hash=528430122, right_hash=-445642423` | 会话建立；两个 hash 从游戏读出 |
| 起冲 | `Sustained dash Jsspsi effects: lifetime prepared actor=… (no particles).`（0–5 号各一行）、`Sustained dash v9: effect 13 continuous particles prepared: count=8` | 保持名单里的特效准备好；寿命钟从第一次回绕才停 |
| 第一次进入接缝修正，差表第一次读或文件有变化时 | `Sustained dash Jsspsi seam: table loaded, entries=…` | 没有或读坏是 `Sustained dash Jsspsi seam: table absent/rejected; relearn, entries=0` |
| 进入冲刺状态 | `Sustained dash selected state=right, time=…, length=5.667, clip_loop=…`（左侧是 `left`） | 游戏选的侧 |
| 第一圈 | `Sustained dash Jsspsi accessory: dash reserved state=… mask=0x00000003 bound=30 skipped=0 before_rewind=1` | 附属件找到 entry、绑好骨（附属件文档 §2.3） |
| 第一圈到达目标 | `Sustained dash Jsspsi seam: object 2 state=… window 0.116-0.317 -> apply`、对象 3 同样一行；随后对象 1、0 的 `window 0.126-0.315 -> apply` | 两侧墨兽的目标早，先打印；`record` 表示这一圈在录表，`idle` 表示错过了记录时机 |
| 录表那一圈到达回绕点 | `Sustained dash Jsspsi seam: recorded and saved.` | 每录好一个对象一行，不带对象号；写文件失败是 `Sustained dash Jsspsi seam: recorded; cache save failed, old file retained.` |
| 第一次回绕 | `Sustained dash Jsspsi audio: dash sound stopped: event=1063030539 playing=…` | 起冲音效停掉 |
| 第一次回绕 | `Sustained dash Jsspsi effects: render VFX held after first rewind.` | 墨兽渲染特效停钟 |
| 第一次回绕 | `Sustained dash v9 normalized bob blend: hash=…, source=0.316, target=0.127, duration=0.000, count=1` | 前 4 次和之后每 16 次各一行；duration 是这次的淡入，接缝修正应用时为 0 |
| 第一次回绕 | `Sustained dash v9: held target effect lifetime/auto-fade clock; particles keep simulating.` | 保持名单里每个特效一行，寿命钟开始停 |
| 第一次回绕 | `Sustained dash Jsspsi accessory: first apply state=… u=0.316… phi=0.005… bound=30 skipped=0 written=30 frame=…` | 附属件开始覆盖；phi 是圈内相位 φ（附属件文档 §2.1）；written 应等于 bound |
| 松手 | `Sustained dash Jsspsi accessory: fade started seconds=0.400 held=30 frames=…`、`Sustained dash ended: movement or owner ended, …`、`Sustained dash Jsspsi accessory: fade first apply …`、`Sustained dash Jsspsi accessory: ended reason=fade complete skipped=0 failed=0` | 松手淡出 |
| 会话结束 | `Sustained dash ended: <原因>, state_changes=…, natural_end_deferrals=…, replays=…, hide_deferrals=…` | 原因见下 |

实机验证时右侧一次冲刺回绕了 5 次、左侧一次 12 次，日志里没有 `pose: root … unavailable`、异常停用和表被拒的行。第一次回绕前就松手或又按了一次冲刺时，附属件还没开始覆盖，结束时不淡出，附属件那一行是 `ended reason=no fade eligible pose`；又按冲刺的那次前面还有 `Sustained dash v9 loop rejected: … transition=1 …`。

结束原因里常见的几种：

- `movement or owner ended`：松手，或者不再满足冲刺条件（不在冲刺或地面疾跑、离地、换了操控角色、角色倒下）。
- `configuration changed`：配置变了（§2）。
- `loop interrupted, late, or not acknowledged`：回绕没有确认（§3），或第 0 层换了状态，前面有一行 `Sustained dash v9 loop rejected: …`；冲刺中又按了一次冲刺时常见（日志）。录制模式的冲刺也在 0.85 以这一条结束。
- `native state or perform ended`：第 0 层离开了冲刺状态，或 Perform 不在了，前面有一行 `Sustained dash exit: …`。
- `perform cleared`、`perform released`：游戏清掉了 Perform。
- `optional feature exception`：祀专有的某项功能抛了异常，前面有 `Sustained dash Jsspsi: features disabled for this dash after an exception.`。

`Sustained dash Jsspsi contracts` 每一项是一组可选契约，解析或安装不到就只关掉对应的功能，模块其余功能照常（模块代码；没有在缺这些契约的客户端上测过）：

| 项 | 要求 | 缺了会怎样 |
| --- | --- | --- |
| hooks | 六个可选钩子装上的个数：`EffectInstance.PlayRenderVfx`、`EntityVFX.PlayVFX`、`EntityVFX.Tick`、`EntityVFXControllerBase.GetDeltaTimeFromLastTick`（先要解析到它的 `m_owner` 字段）、`BeyondDynamicBone.ClothManager.ClothUpdate`、`AudioAdapter._PostEvent`；装之前逐个核对静态或实例签名，对不上的不装 | 各钩子管的功能见下面几项；只缺 `ClothUpdate` 时，接缝修正少了布料前入口，布料跑在 TailLate 之后（默认）时没有影响 |
| tmp | `CharPerformHandleBase.m_tmpObjects` 字段、`TmpObjectInst.get_animator` | 墨兽不跟着回绕，约 0.58 照样跳开；墨兽不做接缝修正、不进录制，墨兽渲染特效的钟也停不住 |
| hierarchy | `UnityEngine.Transform` 类型、`Component.get_gameObject` | 接缝修正和附属件覆盖绑不了骨（seam、accessory 也是 0），录制不录 |
| vfx | `PlayRenderVfx`、`PlayVFX`、`Tick` 三个钩子都装上 | 祀特效启动的 `EntityVFX` 不停钟 |
| clock | `GetDeltaTimeFromLastTick` 钩子 | 墨兽渲染控制器的钟停不住，墨兽仍约在起冲后 5.4 秒化掉 |
| sound | `_PostEvent` 钩子、`AudioAdapter.StopByPlayingId` | 起冲音效不停，原生那一整段照常播完 |
| seam | 共用姿态契约（见下段）、hierarchy、`UnityEngine.Object.get_name` | 接缝修正和附属件覆盖都不运行 |
| accessory | seam 再加宿主的 PoseLease 接口 | 附属件覆盖每次冲刺都按绑定失败停用（`Sustained dash Jsspsi accessory: dash disabled: incomplete Transform enumeration/binding`）；接缝修正也拿不到写骨的租约，日志 `Sustained dash Jsspsi pose: root <n> unavailable; no bone writes for this dash.`，这时 seam 一项仍是 1 |
| splashes | `EffectPlayAction.OnPlay` 钩子装上；`CharInteractPerformActionBase.m_handle`、`m_actionData` 和 `ActorActData.actorIndex` 三个字段；`EffectPlayAction.OnPlay`、`CharPerformHandleBase.get_interactPerformId`、`CharPerformHandleBase.get_entity`、`Entity.IsValid`、`Entity.get_charCtrl`、`CharacterController.get_isMainCharacter` 都解析到且是实例方法 | 10–12 号照原生生成，持续冲刺里会在墨兽并没有跳开的时候出现 |

方法解析不到时，模块加载时先打 `Missing action method: <类>.<方法>`；祀的可选方法解析不到、没有函数指针或者静态与实例属性对不上，都再打一行 `Sustained dash Jsspsi contracts: unavailable <类>.<方法>`；祀的可选字段解析不到时是 `Missing action field: <字段>`。`TmpObjectInst.m_entityRenderHelper`、`EntityRenderHelper.m_entityVFX` 两个字段不在契约行里；缺了它们，墨兽渲染特效的钟停不住，没有专门的日志。

共用姿态契约是七个方法 `CharacterAnimationComponent.TailLateTick`、`Component.get_transform`、`Transform.Find`、`Transform.GetLocalPositionAndRotation`、`Transform.SetLocalPositionAndRotation_Injected`、`Time.get_frameCount`、`Transform.IsChildOf` 都解析到，并装上 TailLateTick 钩子。方法解析不到时打 `Missing action method: <类>.<方法>`；钩子装不上时打 `Sustained dash v12: TailLate hook unavailable; pose overlay disabled, native v9 hold remains.`。这两种情况下 seam、accessory 都是 0：接缝修正和附属件覆盖不运行，录制只打 armed 行、不写文件；Animator 回绕、墨兽回绕、特效和声音照常。祀相关的测试目标和命令见 [模块 README](../../../../../native/modules/actions/README.md) 的「祀的测试」。

## 9. 游戏更新后

下面这些是写死的常量，或按名字、编号匹配的契约，游戏更新可能让它们失效。附属件表本身怎么跟着更新，见附属件文档 §3.4。

| 要核的 | 怎么核 | 核不过会怎样 | 改哪里 |
| --- | --- | --- | --- |
| 模板 id 仍是 `chr_0036_jsspsi`，Perform 名仍是 `CharIntPerform_Jsspsi_SpdashL/R` | §8 的 `armed` 行出现 | `armed` 行不出现，祀照原版冲刺，没有别的提示 | 档案的 `template_id`、`perform_id`、`side_suffixed_perform` |
| 两侧状态 hash 仍是 528430122、-445642423 | `armed` 行的 left_hash、right_hash | 附属件表这一侧对不上（`Sustained dash Jsspsi accessory: dash disabled: no entry for current state`），不覆盖；身体接缝差表按新 hash 自动重录 | 附属件表重录、重生成；工具里的 hash 常量（`accessory_cycle.py` 的 `HASHES`） |
| 剪辑时长仍是 5.667 秒，内容没变 | `selected state` 行的 length；内容的核法见附属件文档 §3.4 | 回绕点不再落在第一步、第二步结束的姿势上；hash 和回绕区间没变时，旧的身体接缝差表和附属件表照用，没有提示 | 重新挑回绕点，改 `dash_policy.h` 的 `kJsspsiLoop`、`kJsspsiSideLoop`；身体接缝差表升版本号，或让用户删掉 `seam_jsspsi.bin`（§5）；工具 `pose_kinematics.py` 里的 `A`、`B`（回绕区间）和 `CLIP`（剪辑秒数，动画时间、采样网格和速度都按它换算）；附属件表重录、重生成 |
| 特效 actor 编号：0–5、13 要保持，10–12 是结尾水花 | 解包 Perform `CharIntPerform_Jsspsi_SpdashL/R` 的特效动作，按 actor 序号对名字（§4 的表） | 保持错了特效，或墨兽丢了外观；跳过位图对不上时，该放的特效不生成，结尾水花又出现；按编号认，不报错 | `dash_policy.h` 的 `kJsspsiHoldEffects`、`kJsspsiSkipEffects` |
| 起冲音效仍是第一次回绕前祀或墨兽投递的第一个声音 | `Sustained dash Jsspsi audio: dash sound stopped` 行的 event，现为 1063030539 | 停错了声音 | 见下一行的名字过滤 |
| 祀和墨兽仍按名字子串认得出：祀的特效名含 `jsspsi`，发声的 `AnimatorMono` 所在物体名含 `jsspsi` 或 `inkbiter` | `dash sound stopped` 行出现 | 起冲音效不停；祀特效启动的 `EntityVFX` 不停钟 | 档案的 `owned_name_token`、`companion_name_filter` |
| 祀专用契约都在 | §8 的 `Sustained dash Jsspsi contracts` 行：正常是 `hooks=6/6`，其余各项为 1 | 见 §8 的表 | 按游戏的新方法名、字段名改描述符 |

墨兽同步、特效保持和跳过、停起冲音效、墨兽渲染特效停钟、网格组隐藏延后这几层没有开关，出了问题只能改代码关；接缝修正（`jsspsi_seam_fix`）和附属件覆盖（`jsspsi_accessory_loop`、`jsspsi_accessory_mask`）有开关。

## 10. 已知边界

- **松手和打断**：模块发现不再冲刺就结束会话、打断 Perform，墨兽和特效当场消失，没有原生收尾（实机观察）。附属件骨在 `jsspsi_accessory_fade` 秒内淡回原生姿态（淡出的条件见附属件文档 §2.3），身体的接缝修正是一下撤掉的。
- **结尾水花和脚步声**：10–12 号从起冲就不生成，循环里也不按节奏补放；起冲音效在第一次回绕后停掉，循环里没有墨兽脚步声。
- **状态回调**：CrossFade 回到同一状态，可能触发状态机行为的 OnStateExit/OnStateEnter；祀的状态上挂着 `AnimatorBehaviourPlayEffect` 和 `AnimatorBehaviourHideGameObject`。模块不拦这两类回调，实机验证时没有看到由它们引起的问题。另一个试验构建曾在每次回绕后 700 ms 内跳过同一 Animator、同一状态 hash 的这两类回调，实机日志里一次也没有拦到。
- **墨兽相位**：两侧墨兽每圈比祀长 0.068 秒，相对相位一直漂；身下墨兽只保证与祀同一帧发出回绕，不确认落点。
- **第一圈和转向**：第一圈物理权重还没降到 0.01；「画面≈写下的姿态」只在圈尾抽检过（§6），第一圈和转向时没有验证；离线分析都是直线冲刺。
- **左右**：由游戏决定，模块不重抽；BECAP001 不带侧别，两侧数据要靠日志分开。
- **姿态数据**：祀的档案没有姿态数据文件，`external_loop` 对她不起作用。
- **数据不带版本**：身体接缝差表（BESEAM03）和附属件循环表（BEACC001）都不记录游戏或动画的版本。游戏改了冲刺动画，要重录、重新生成附属件表，并删掉 `seam_jsspsi.bin` 让模块重新学（§5，附属件文档 §3.4）。
- **卸载残留**：卸载器不删模块运行时写出的 `seam_jsspsi.bin`、`capture_jsspsi.bin`。
- **开销**：每帧开销没有测过。接缝修正每帧要读写差表里有非零差的骨（§5 那份差表里祀每侧约 400 根、每只墨兽 28–35 根），附属件覆盖写表里被 mask 选中的骨（发出的表的骨数见附属件文档 §3.2）；起冲后 1.5 秒内，没绑上的对象每帧重新枚举一次整棵 Transform 树。
- **Android**：Android 构建编译同一份 `module.cpp`，APK 带着 `accessory_jsspsi.bin`。只在 Linux 上用 `-D__ANDROID__` 编译检查过非 `_WIN32` 分支，没有用 NDK 构建、没有生成 APK，也没有在设备上运行。

## 附录 A. BESEAM03（身体接缝差表 `seam_jsspsi.bin`）

小端，模块自己写、自己读：

```text
char[8] "BESEAM03"
u32     条数                       < 256
每条：
  i32   第 0 层状态 hash
  f32   目标 target（圈首，祀为 0.126）
  f32   回绕点 begin（圈尾，祀为 0.315）
  u32   骨数                       ≤ 4096
  每根骨（顺序不定）：
    u16 名字字节数                 1–511
    u8  名字[字节数]               叶名，按 UTF-8 写
    f32 旋转差 D 的 x y z w        D = first ⊗ end⁻¹
    f32 位置差 dp 的 x y z         first − end，米
```

注意字段顺序：这里先目标后回绕点，BEACC001 先回绕点后目标。

载入时检查：文件头和条数；每条的目标、回绕点是有限数，且 0 ≤ 目标 < 回绕点 < 0.85；骨数不超过 4096；名字长度 1–511、不含 0 字节；D、dp 的每个分量是有限数、绝对值不超过 50；D 的模平方在 0.99–1.01 之间，读入后归一化；同一条里名字不重复；读完所有条后文件正好结束。任何一项不满足或读到一半文件结束，整表作废，按缺表处理、重新记录。名字是不是合法 UTF-8、D 的 w 是否非负（写表时的约定）不检查。写入先写同目录的 `.new`，flush 和关闭都成功才改名替换，写失败时删掉 `.new`、保留旧文件。附属件文档 §3.2 那份差表按这些规则核过，能读进来。改了要补哪些骨时，换版本号让旧表作废。

## 附录 B. BECAP001（`capture_jsspsi.bin`）

小端：

```text
char[8] "BECAP001"
u32     骨数
u32     帧数                       ≤ 900
每根骨：
  u8    分组                       0 祀，1–3 为 inkbiter01–03
  u16   路径字节数
  u8    路径[字节数]               UTF-8，从各自 Animator 根起的相对路径，根本身是 "."
每帧：
  f32   祀第 0 层的归一化时间 u
  f32   距录制开始的秒数           GetTickCount64 系统时钟，不能用来求速度
  骨数 × f32[7]                   局部旋转 qx qy qz qw、局部位置 px py pz（米）
```

骨表在冲刺开始时建一次（§7），收当时祀和已生成的墨兽每个根下的全部 Transform（含未激活的，每个根最多 2048 个），pin 不上的节点不收；之后读不到的骨记成单位姿态。录制里角色组的 Transform 数随装备变化，1.6.3 的录制是 659 个，带专武的 1.6.4 录制是 697 个；每只墨兽 63 个。
