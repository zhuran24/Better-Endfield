# 冲刺持续姿态数据来源与说明

本目录包含冲刺持续模块（`BetterEndfieldNext.Actions`）运行时读取的二进制数据：`external_loop=true` 模式下的骨骼姿态覆盖数据（BEPOSE12 格式），以及祀持续冲刺的附属件循环表（BEACC001 格式）。

## 文件清单

1. **`pose_aglina.bin`**
   - 对应角色：洁尔佩塔（`chr_0013_aglina`，Perform: `CharIntPerform_Aglina_Spdash`）
   - 规格：226 骨骼，周期 103 帧（约 1.717 秒，60 fps），包含左右双侧采样。
   - 详见：`docs/actions/SPECIAL_DASH_CONTINUOUS_ANIMATION.md`

2. **`pose_liino.bin`**
   - 对应角色：梨诺（`chr_0035_liino`，Perform: `CharIntPerform_Liino_Spdash`）
   - 规格：339 骨骼，低位循环 40 帧（约 0.667 秒，60 fps），包含左右双侧采样。
   - 生成脚本：`tools/actions/build_liino_glide.py`
   - 详见：`docs/actions/research/1.5.3/liino-dash/LIINO_LOW_GLIDE.md`

3. **`accessory_jsspsi.bin`**
   - 对应角色：祀（`chr_0036_jsspsi`，Perform: `CharIntPerform_Jsspsi_SpdashL` / `CharIntPerform_Jsspsi_SpdashR`）
   - 格式与规格：BEACC001 v1，左右两个 entry（右侧状态 hash `-445642423` 在前，左侧 `528430122` 在后），回绕区间 0.315 → 0.126；只含组 0 尾巴（10 骨）、组 1 后发（20 骨），每圈 1024 个样本，1,721,336 字节。按祀第 0 层的圈内相位覆盖尾巴、后发的局部姿态，与 `external_loop` 无关，由 `jsspsi_accessory_loop`、`jsspsi_accessory_mask` 控制。
   - 生成工具：`tools/actions/jsspsi_accessory/`（输入为模块录制模式录下的左右原版冲刺、蒙皮骨目录 `tools/CustomModel/catalog/chr_0036_jsspsi.json`、模块自录的身体接缝差表；生成这份表所用输入的 SHA-256 见研究文档 §3.2）
   - 详见：`docs/actions/research/1.6.3/jsspsi-dash/JSSPSI_ACCESSORY_LOOP.md`

## 验证与测试

数据契约与反序列化边界通过 `BetterEndfieldNext.ActionsPoseTests`、`BetterEndfieldNext.ActionsAccessoryTests` 进行回归验证，两者在 `config/test-suites.json` 里登记为 `actions.pose`、`actions.accessory_table`：

先按 [模块 README 的「祀的测试」](../README.md#祀的测试) 配置 CMake，并在同一个 PowerShell 会话里设置 `$nativeBuild`；以下命令从仓库根目录运行：

```powershell
cmake --build "$nativeBuild" --config Release --target BetterEndfieldNext.ActionsPoseTests BetterEndfieldNext.ActionsAccessoryTests
& "$nativeBuild/Release/BetterEndfieldNext.ActionsPoseTests.exe" native/modules/actions/assets/pose_aglina.bin native/modules/actions/assets/pose_liino.bin
& "$nativeBuild/Release/BetterEndfieldNext.ActionsAccessoryTests.exe"
```

`ActionsAccessoryTests` 用内存里构造的表跑 BEACC001 解析器的截断、篡改与边界用例，以及 mask 解析、相位和采样，不读本目录的表。

本目录 `accessory_jsspsi.bin` 的表头用附属件工具的 `inspect` 核对。工具用模块的解析代码读表，要在工具 README（`tools/actions/jsspsi_accessory/README.md`）说的 Python 环境里、从仓库根目录运行：

```bash
python3 -B tools/actions/jsspsi_accessory/accessory_table.py inspect \
  --source native/modules/actions/assets/accessory_jsspsi.bin \
  --work <工作目录>/accessory-inspect
```

输出应为：

```text
BEACC001 version=1 entries=2
state=-445642423 begin=0.314999998 target=0.126000002 N=1024 M=30
state=528430122 begin=0.314999998 target=0.126000002 N=1024 M=30
```

这张表的字节数、SHA-256 和各项取值见 `docs/actions/research/1.6.3/jsspsi-dash/JSSPSI_ACCESSORY_LOOP.md` §3.2。

祀的附属件覆盖、接缝修正和角色策略另有测试目标，见 `native/modules/actions/README.md` 的「祀的测试」。

## 构建分发

`native/CMakeLists.txt` 的 `BetterEndfieldNext.Layout` 目标把本目录的 `pose_aglina.bin`、`pose_liino.bin`、`accessory_jsspsi.bin` 逐个复制到安装包/交付目录的 `modules/actions/`；新增数据文件要在那里补一条复制。Android 打包同样带上这三个文件，运行时按内容逐字节比较，不同才释放到游戏的 `files/betterendfieldnext/actions`，再通过 `BETTER_ENDFIELD_NEXT_ACTIONS_ASSET_ROOT` 告诉模块。

祀的身体接缝差表 `seam_jsspsi.bin` 和录制文件 `capture_jsspsi.bin` 是模块运行时在同一个 `actions` 目录里写出的，不放在本目录，也不随包分发；卸载器不删这两个文件。`accessory_jsspsi.bin` 和 `seam_jsspsi.bin` 都不记录游戏或动画的版本：游戏改了祀的冲刺动画，要重录、重新生成 `accessory_jsspsi.bin`，并删掉 `seam_jsspsi.bin` 让模块重新学。
