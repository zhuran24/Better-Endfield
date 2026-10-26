# 祀持续冲刺的离线输入数据

这个分支只放数据，不含代码，不要合进代码分支。

`inputs/` 里是游戏 1.6.4 Windows 客户端里录下的祀（chr_0036_jsspsi）特殊冲刺原版动作，和模块运行时记下的身体接缝差表。生成 `native/modules/actions/assets/accessory_jsspsi.bin` 用的就是这批输入，SHA-256 与分支 `feat/actions-jsspsi-sustained-dash` 上 `docs/actions/research/1.6.3/jsspsi-dash/JSSPSI_ACCESSORY_LOOP.md` §3.2 的表一致。格式见同一分支的 `JSSPSI_SUSTAINED_DASH.md` 附录 A（BESEAM03）、附录 B（BECAP001）。

| 文件 | 说明 |
| --- | --- |
| `capture_jsspsi-20261018-180810-right.bin` | 右侧录制，534 帧，生成时选中 |
| `capture_jsspsi-20261018-180717-left.bin` | 左侧录制 1，514 帧 |
| `capture_jsspsi-20261018-180723-left.bin` | 左侧录制 2，534 帧 |
| `capture_jsspsi-20261018-180728-left.bin` | 左侧录制 3，465 帧，生成时选中 |
| `seam_jsspsi.bin` | 身体接缝差表，BESEAM03，5 条 |
