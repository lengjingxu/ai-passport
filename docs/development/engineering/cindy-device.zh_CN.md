<p align="right"><strong>简体中文</strong> · <a href="cindy-device.md">English</a></p>

# Cindy 设备连接与交互

这是 Cindy 设备固件。[Codex Buddy](https://github.com/zhangsan2000w-art/ai-passport-codex-buddy/tree/52d612cbe47c4528b95994710d320b19cc7479c6) 提供蓝牙任务状态、心跳、中文界面和实体按键的设计参考；它的 Windows 桥接与 Codex hooks 不能连接 Cindy。

## 中文与录音参考

[AI Passport 小智](https://github.com/FoloToy/folo-ai-passport-xiaozhi/tree/d24fce080d86d7cc642f71585f6efde40fb99104/main/audio) 使用 16 kHz 单声道、60 ms Opus 帧、复杂度 0 和有界队列，通过 WebSocket/Wi-Fi 传输。Cindy 参考其分帧与编码器，接入自己的蓝牙传输。固定 16 kbit/s，关闭 VBR 与 DTX，每个音频载荷为 120 字节。拥塞会中止整段录音，不静默丢字。

小智使用 Noto 字库资产，支持二进制字库加载与按需字形下发。Cindy 将 14 px、1 bpp 的 Noto 中文点阵放在 Flash，来源与许可见[字库说明](../../../assets/README.zh_CN.md)。覆盖 ASCII、中文标点、扩展 A、基本汉字区和全角字符，不覆盖 emoji 与扩展 B。任务预览按 UTF-8 字符边界截断，不需要 PSRAM。

## 蓝牙交互

使用配套 Cindy macOS 客户端，在设置 → 快捷键 → 配件 → Cindy Passport 中启用。设备进入 **Cindy BLE**，电脑选择发现的设备标识；macOS 请求配对时输入设备屏幕显示的码。界面显示权限与连接状态，不需要 IP 或 HTTP 桥接。菜单栏控制仍可使用。

UP/DOWN 选择任务，OK 打开详情。详情中 UP/DOWN 切换任务，OK 开始录音，再按 OK 停止并发送，30 秒自动提交；长按 OK 退出并取消。采集与发送使用独立工作任务及 8 块 PCM 队列。队列满等待 20 ms 后中止；每次 indication 最多等待确认 1.5 秒。编码发送任务使用 24 KB 栈，堆内存与时序仍需实测。

客户端保留已观察到的完成/错误任务，活动提示清空后仍可选择；归档或删除后按正式任务目录移除。最多显示 8 个任务，依次优先等待、错误、运行、完成。保留记录仅在内存中，最多 100 条，适配器停止时清空；活动任务可能占满 8 个位置。这是本机任务目录，不复刻远程镜像行或侧栏排序。

完整录音在 Mac 内存中封装为 Ogg Opus，交给 Cindy 已有的批量语音转写入口，使用客户端配置的批量 ASR 路由与凭证，不在设备保存密钥；该路由可能与实时麦克风输入使用的供应商不同。转写后打开原任务，只填入空白草稿；已有草稿保留，新文字等待草稿为空。用户在 Cindy 检查并发送，不自动发送消息，也不执行设备审批。取消、乱序、截断、超限、断连的录音都会拒绝，转写前后复核任务目录、账号及连接边界。

## 协议

服务 `C1DC0001-51C4-499D-A186-4621A4938301` 使用认证配对、Secure Connections 与 16 字节密钥。RX `8302` 接收认证写入，TX `8303` 提供认证读取和打开任务通知。快照为 uint16LE 载荷长度、版本 1、0–8 个任务，每条 328 字节：UTF-8 id/title/status/message 分别占 40/80/16/192 字节，以 NUL 填充。Mac 每 5 秒发送心跳，设备 10 秒无快照后清空任务。BLE 回调不访问 LVGL。

录音特征 `8304` v1 使用 indication：`kind:u8, token:u32LE, sequence:u16LE, payload`。开始 kind 1 携带 40 字节任务 ID，序号为 0；数据 kind 2 携带 120 字节的 60 ms Opus，序号从 0 连续递增。结束 kind 3、取消 kind 4 无载荷，使用下一个序号。最多 502 帧容纳 30 秒录音、补齐帧和编码器尾部排空；Mac 拒绝不完整序列或耗时超过 45 秒的录音。Ogg 保留编码延迟和尾部静音，不猜测 preskip。没有 `8304` 支持的旧客户端不能录音，需要两端一起更新。

## 旧 Wi-Fi 模式

**Cindy Tasks** 是独立手动选择的局域网模式，使用忽略提交的 `main/app_config.h` 和 `python3 bridge/server.py --port 8787`。任务来源是文件，HTTP 分块 PCM 保存为 WAV，不提供真实 Cindy 事件或转写。蓝牙不会切换到这条路径。本地配置固件可能含凭证，不能公开上传。

## 构建与验收

激活 ESP-IDF 5.5.3 后运行 `./tools/validate.sh`，验证后的合并镜像位于 `build/FoloToy-AI-Passport-full.bin`。保留 8 MB Flash、3 MB 应用上限及 `0x356000` 的 `cardid`。确认镜像结束地址早于 `cardid` 后可从 `0x0` 写入，不整片擦除。

分别报告 Build、Host tests、Device tests、Unverified。主机检查覆盖 UTF-8/任务模型、有界录音队列及清理；Mac 检查覆盖协议限制、完成任务保留及 Ogg 封装。中文屏幕、蓝牙录音/转写连续性、反复录音的堆稳定性、权限弹窗、重连和页面退出仍需设备验收；编译通过不能证明这些结果。
