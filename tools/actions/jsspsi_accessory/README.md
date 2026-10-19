# 祀附属件循环表生成工具

从祀（`jsspsi`）左右两侧的原版冲刺录制生成附属件循环表（BEACC001），即 `native/modules/actions/assets/accessory_jsspsi.bin`。默认输出左右两个 entry，只含组 0 尾巴、组 1 后发，每侧每圈 1024 个样本、30 根骨。做法、指标和验证见 [祀附属件平稳循环](../../../docs/actions/research/1.6.3/jsspsi-dash/JSSPSI_ACCESSORY_LOOP.md)。

下文 u 是祀第 0 层冲刺状态的归一化时间，0–1 对应剪辑的 0–5.667 秒；φ = frac((u − 0.126)/(0.315 − 0.126)) 是圈内相位（研究文档 §2.1）。「第二步」「第三步」指原版冲刺里 u 0.126–0.315 和 0.315 以后的两段（[实现依据](../../../docs/actions/research/1.6.3/jsspsi-dash/JSSPSI_SUSTAINED_DASH.md) §1.3）。

## 准备

- 动作模块的配置在 `%LOCALAPPDATA%\BetterEndfieldNext\BetterEndfieldNext.ini` 的 `[betterendfieldnext.actions]` 节，日志在 `%LOCALAPPDATA%\BetterEndfieldNext\logs\BetterEndfieldNext.log`。各键见实现依据文档 §2，日志各行见 §8。
- 先在启动器「冲刺持续」页打开「祀」（写进 `characters`）；不开的话模块不处理祀的冲刺，什么也录不到。
- 模块写出的录制和身体接缝差表都在安装目录的 `modules/actions/` 下。安装目录默认是 `%LOCALAPPDATA%\Programs\Better Endfield Next`，在 WSL 里是 `/mnt/c/Users/<用户名>/AppData/Local/Programs/Better Endfield Next`；路径带空格，命令里要加引号。

## 输入

三类输入都由参数给出，仓库里只有蒙皮骨目录。

1. `--right`、`--left`：动作模块录制模式写出的 BECAP001 录制，每侧至少一份，同侧可以给多份。
   - 设 `jsspsi_capture=true`，保持 `diagnostics=true`（默认）。之后每次冲刺都不回绕、按原版播放，结束时写 `modules/actions/capture_jsspsi.bin`，每次覆盖同一个文件。录完把 `jsspsi_capture` 改回 false，下面第 3 条要在关着录制时做。
   - 直线冲刺，每次按住到冲刺自己播完。一份录制只能是一次完整的原版冲刺，不能是循环播放，也不能把几次冲刺混在一起。每次都确认日志里这次冲刺有 `Sustained dash Jsspsi capture: armed for jsspsi: bones=…` 和 `Sustained dash Jsspsi capture: written: bones=… frames=… ok=1` 两行；只有 armed、没有 written，只能确认没有完成写出；正常结束时可能是一帧未录到（例如 TailLate 钩子缺失），模块关闭或功能异常也会直接丢弃已有帧。结合异常和关闭日志排查，并确认 written 的 ok=1。
   - BECAP001 不带状态 hash，分不出左右：每录完一次，按日志里这次冲刺的 `Sustained dash selected state=left` 或 `right` 把文件另存，文件名里带 `left` 或 `right`；这一行只在 `diagnostics=true` 时打印。左右由游戏在起冲时决定，冲到两侧都有为止。侧别由 `--right`、`--left` 参数决定；工具要求文件名含对应的侧名（不分大小写），对不上就报错，它不能从录制内容认出左右。左右内容完全相同的录制会被拒绝。
   - 动画归一化时间要严格递增，并覆盖 u 0.09–0.40。评分按动画时钟 t = u × 5.667 算，不用录制里的系统时钟秒数。
2. `--catalog`：祀的蒙皮骨目录 `tools/CustomModel/catalog/chr_0036_jsspsi.json`。工具把蒙皮骨数写死为 375，并要求每个名字在录制的角色组里恰好出现一次；目录或骨名变了，要先改 `pose_kinematics.py` 里的这两条检查（研究文档 §3.4）。
3. `--seam`：BESEAM03 身体接缝差表，就是动作模块运行时自录的 `modules/actions/seam_jsspsi.bin`。表里要有祀左右两条（右 `-445642423`、左 `528430122`，回绕区间 begin 0.315、target 0.126）。在 `jsspsi_capture=false`、`jsspsi_seam_fix=true`（默认）、`diagnostics=true` 下反复冲刺：某一侧还没有表时，这次冲刺第一圈的日志是 `Sustained dash Jsspsi seam: object 0 state=<这一侧的 hash> window 0.126-0.315 -> record`，冲过 u 0.315（起冲后 1.78 秒）后接着有 `Sustained dash Jsspsi seam: recorded and saved.`；已经有表的一侧是 `-> apply`。两侧的 hash 都出现过 `-> apply`，差表就录齐了。表里缺哪一侧，工具会报 `差表没有状态 <hash>` 并退出。差表参与完整祖先链的评分和录制选择，不能省略。

工具会检查所需骨名、蒙皮名单、左右内容是否相同、时间是否递增，以及旋转对数坐标是否接近分支。

## 环境

- Python 3，第三方依赖只有 NumPy、SciPy，版本钉在 `requirements.txt`；支持 C++20、命令行与 GCC 相同的编译器（默认 `g++`，可用 `CXX` 指定 `clang++` 等）。采样器按 `$CXX -std=c++20 -O2 -I native/modules/actions <源文件> -o <输出>` 编译，MSVC 的 `cl` 不接受这组参数。
- 采样器 `tools/actions/jsspsi_accessory/native/accessory_table_tool.cpp` 直接包含仓库里的 `native/modules/actions/accessory_table.h`、`pose_policy.h`，用的就是模块自己的解析和采样代码；这两个头文件不在时工具报错退出。
- 生成入口在导入 NumPy、SciPy 之前把 `OPENBLAS_NUM_THREADS`、`OMP_NUM_THREADS` 都设成 1（已有的值也覆盖），数值库单线程运行。
- 参数和录制的取舍规则是固定的：评分先量化到 10⁻⁹ 的网格再比；并列时依次比最大速度比、最大速度变化比，仍并列就按参数的规范 JSON 字符串取舍；同侧几份录制并列时按录制的 SHA-256 取舍。这样数值库末位舍入不同也不会换掉赢家：系统 cblas 和 scipy-openblas 两种 NumPy 构建、三个哈希种子下，默认和全组输出都逐字节相同。规则写在输出 JSON 的 `selection_policy` 里，`test_selection.py` 是它的测试（`python3 -B -m unittest discover -s tools/actions/jsspsi_accessory -p 'test_*.py'`）。
- 只在 Linux x86_64 上跑过：Python 3.14.8、NumPy 2.5.4、SciPy 1.18.1、GCC 16.2.1。Windows 上没有试过，建议在 WSL 里运行。其他机器、库版本和编译器上的耗时与字节一致性没有验证。

依赖装进虚拟环境，目录放在仓库外（下面放在仓库的上一级）。Debian、Ubuntu（包括 WSL 里默认的 Ubuntu）的系统 Python 不许直接往系统环境里 `pip install`（PEP 668）；没有 venv 模块或编译器时，先装 `python3-venv` 和 `g++`。下面三条命令在仓库根目录运行，没有在 Ubuntu 或 WSL 上实际跑过：

```bash
python3 -m venv ../jsspsi-accessory-venv
. ../jsspsi-accessory-venv/bin/activate
python -m pip install -r tools/actions/jsspsi_accessory/requirements.txt
```

下面的命令都在激活了这个环境的 shell 里、从仓库根目录运行。

## 生成

录制和差表换成实际路径：

```bash
python3 -B tools/actions/jsspsi_accessory/build_jsspsi_accessory.py \
  --right <录制目录>/capture_jsspsi-right.bin \
  --left <录制目录>/capture_jsspsi-1-left.bin <录制目录>/capture_jsspsi-2-left.bin \
  --catalog tools/CustomModel/catalog/chr_0036_jsspsi.json \
  --seam "<安装目录>/modules/actions/seam_jsspsi.bin" \
  --work <工作目录>/jsspsi-work \
  --output <工作目录>/accessory_jsspsi.bin
```

| 参数 | 必需 / 默认值 | 含义 |
| --- | --- | --- |
| `--right` | 必需，1 份或多份 | 右侧 BECAP001 录制，文件名须含 `right` |
| `--left` | 必需，1 份或多份 | 左侧 BECAP001 录制，文件名须含 `left` |
| `--catalog` | 必需 | 祀的蒙皮骨目录 |
| `--seam` | 必需 | 含两侧状态的 BESEAM03 身体接缝差表 |
| `--groups` | `0,1` | 逗号分隔、不重复的组号，只收 0–6：0 尾巴、1 后发、2 两袖、3 袖飘带、4 前发、5 裙子与裙飘带、6 右臂 |
| `--samples` | `1024` | 最终表每圈的样本数，只收 8–4096；全组、原始样本数用 `--groups 0,1,2,3,4,5,6 --samples 1152` |
| `--output` | 当前目录下的 `accessory_jsspsi.bin` | 最终双侧表 |
| `--work` | 当前目录下的 `jsspsi-work/` | 中间数据和采样器编译目录 |

- 入口按自己所在的目录找本工具的模块和 C++ 源文件，按仓库布局找 `native/modules/actions/` 下的头文件；输入、输出的相对路径以运行时的当前目录为准。
- `--output` 和 `--work` 必须是还不存在的路径，也不许和输入重叠。派生的 `<output>.json`、`<output>.summary.md` 没有存在性检查，已有同名文件会被覆盖，所以输出名要选新的。编译产物、临时文件和搜索中间数据都在 `--work` 下。
- 仓库里那张表的四份录制（右 1、左 3）、默认参数，生成约 80 秒（两次生成同时运行时测得，包括搜索四份录制、生成表、编译采样器和误差比较）。

生成完先验收，再替换 `native/modules/actions/assets/accessory_jsspsi.bin`：

1. `<output>.json` 里 `resampling_error.passed` 为 true，也就是输出的表和全组表在 φ=i/8192 上逐骨的误差都在门槛内。
2. 读 `<output>.summary.md` 里各组连接段的评分（门槛：速度 1.2、速度变化 1.5）。两侧尾巴应当过门槛；后发不过是已知情况，分数不应明显高于研究文档 §4.1 列的值。
3. 用下面的 `compare` 和仓库里现有的表比一次，只用来看改动集中在哪些组、哪些相位；换了录制，`passed` 多半是 false，它不是通过条件。
4. 进游戏核对：把新表换到 `<安装目录>/modules/actions/accessory_jsspsi.bin`（不用重启，下一次冲刺生效），左右各冲刺几次。日志里 `Sustained dash Jsspsi accessory: entry state=… N=… M=…` 和新表一致，`first apply` 行的 `written` 等于 `bound`，第一圈之后每圈尾巴、后发不再甩。
5. 替换仓库里的表，同步研究文档 §3.3 第 4 步列的那几处。`<output>.json` 记的是输入的绝对路径，不要随表提交。

怎么选：

- 每份录制都做全组搜索，只导出两组时也一样。右臂先定，两袖、袖飘带的评分带上右臂覆盖；每组按 60/120 Hz 下最差的归一化速度和速度变化选参数。
- 同侧有多份录制时，按最差分数、平均分数、输入哈希选一份完整录制，不把不同录制的逐组赢家拼在一起。
- 选好后先生成全组、1152 样本的表 `<work>/accessory_jsspsi.full.bin`；绕圈处把第 1 个和最后一个样本按第 0 个样本对称调整，让绕圈两侧的切线一致。再按模块的采样规则在 φ=j/N 处取样，写选定的组和样本数；不按新的 N 重新合成曲线，N 为 1152 时原样保留 float32 样本。仓库里的表就是用默认参数从 1.6.4 的四份录制这样生成的，输入的 SHA-256 见研究文档 §3.2。

## 输出

- `--output`：BEACC001 表，右 entry 在前、左 entry 在后。
- `<output>.json`：输入的路径和 SHA-256、每份录制的搜索结果、选中的录制和参数、输出哈希、耗时，以及相对全组表在 φ=i/8192 上的重采样误差（`resampling_error`）。
- `<output>.summary.md`：重采样前全组曲线的 60/120 Hz 运动学评分（门槛：速度 1.2、速度变化 1.5）。它不是精简误差，也不是画面验收。
- `--work`：每份录制的候选 TSV、`stable.json`（每组通过平稳门槛的 uk，以及是否走了回退）、选中参数、逐骨评分、全组表 `accessory_jsspsi.full.bin`、编译出的采样器和临时文件。

## 精简、比较和查看已有的表

表工具 `accessory_table.py` 有三个子命令，都用模块自己的解析和采样代码（每次运行在 `--work` 下重新编译采样器）。

生成器默认已经输出精简表。`compact` 用在手里已有全组表（例如 `<work>/accessory_jsspsi.full.bin`）、只想换组或重新选样本数的时候，不重跑搜索：

```bash
python3 -B tools/actions/jsspsi_accessory/accessory_table.py compact \
  --source <work>/accessory_jsspsi.full.bin --output <工作目录>/accessory_jsspsi.bin --work <工作目录>/compact-work
```

默认保留组 0、1（`--groups` 可改）。两侧共用的样本数从 128 起翻倍、不超过原 N，取第一个通过的：在 φ=i/8192（i=0..8191）上逐骨比较，旋转最短角差不超过 0.05°、局部位置差不超过 0.01 cm。都不过就回退原 N、原样本。每档结果写在工作目录里；`--output` 已存在时会被覆盖。

`compact` 先用 Python 读表，要求比模块严：各 entry 的原始样本数相同，同一 entry 里骨名不重复，各 entry 的状态 hash 不重复，四元数模长与 1 的差小于 1e-5。本工具生成的表都满足；模块能载入的 BEACC001 不一定都能精简。

```bash
python3 -B tools/actions/jsspsi_accessory/accessory_table.py compare \
  --source <参考表> --candidate <候选表> --work <工作目录>/compare-work
```

`compare` 用模块的解析代码读两张表：entry 数要相同；候选表的每个 entry 要在参考表里找到同一状态 hash，begin、target 的 float32 值完全相等；候选表的每根骨要在参考表对应 entry 里找到同名、同组号的骨，所以候选可以只含参考表的一部分骨。结果写 `<work>/comparison.json` 并打印出来，含每侧的 N、M，以及每侧、每组、每根骨的最大误差和所在相位；进程正常退出只表示比较做完，过没过看 `passed`（每根骨在每个网格相位上旋转差不超过 0.05°、位置差不超过 0.01 cm）。

```bash
python3 -B tools/actions/jsspsi_accessory/accessory_table.py inspect \
  --source native/modules/actions/assets/accessory_jsspsi.bin --work <工作目录>/inspect-work
```

`inspect` 用模块的解析代码载入一张表，打印 entry 数，以及每个 entry 的状态 hash、begin、target、N、M；表载入失败时打印原因并以非零状态退出。

误差都用模块自己的采样器算：载入时归一化四元数，按 `floor(φN)` 取段，末样本接回第 0 个，旋转走最短弧 slerp（接近时按模块阈值改 nlerp），位置 `std::lerp`。这是均匀网格上的核对，不是全周期的严格上界。

## 各组怎么取段、连接

设 A=0.126、B=0.315。每组一圈先走原版第三步 B→u3，再用连接段回到第二步较早的 uk，最后走第二步末段 uk→B，回到 B 后重复。u3、uk 和连接段类型（cubic、quintic、hybrid、damped、minjerk）各组各侧自动选，结果写在输出 JSON 里。评分只看蒙皮骨；链末端不蒙皮的骨也导出，所以尾巴 10 根、后发 20 根。各段定义、连接段和搜索规则见研究文档 §2。

## 文件

```text
build_jsspsi_accessory.py → accessory_cycle.py → pose_kinematics.py → capture_reader.py
                          → accessory_table.py → pose_kinematics.py
                                               → native/accessory_table_tool.cpp
```

- `build_jsspsi_accessory.py`：生成入口。检查输入，按侧、按录制搜索参数，选录制，写全组表，重采样输出，写 JSON 和摘要。
- `accessory_cycle.py`：五类连接段的合成、全蒙皮评分、生成 entry；两侧的状态 hash 常量 `HASHES`、末节蒙皮骨 `SKIN_TIPS` 也在这里。
- `pose_kinematics.py`：公共库。把录制解析成骨架、正向运动学、身体接缝修正的离线重放、BESEAM03 读取、BEACC001 读写、骨分组规则 `group_for`、链末端 `TIPS` 和蒙皮骨数检查；回绕区间 `A`、`B` 和剪辑时长 `CLIP` 这几个常量也在这里。
- `capture_reader.py`：读 BECAP001 录制。
- `accessory_table.py`：`compact`、`compare`、`inspect` 三个子命令，以及编译、调用采样器。
- `native/accessory_table_tool.cpp`：采样器，调用模块的 `AccessoryTable::Load` 和 `AccessoryEntry::Sample` 做重采样、比较和表头输出。
- `test_selection.py`：取舍规则的单元测试（近似并列、精确并列、参数和录制的顺序无关、明显更优的分数仍然胜出）。
- `requirements.txt`：Python 第三方依赖的版本。
