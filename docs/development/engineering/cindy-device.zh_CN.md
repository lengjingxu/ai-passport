<p align="right">
  <strong>简体中文</strong> · <a href="cindy-device.md">English</a>
</p>

# Cindy 设备连接与交互

产品是面向 Cindy 的任务设备。外部
[Codex Buddy 源码](https://github.com/zhangsan2000w-art/ai-passport-codex-buddy/tree/52d612cbe47c4528b95994710d320b19cc7479c6)
用于参考设计，不作为本产品的交付固件。

## 参考项目实现了什么

Codex 生命周期 Hook 把事件交给本地 Windows 桥，再通过加密 Nordic UART BLE
发送到中文像素宠物界面。控制器扫描 `Codex-*`，连接、订阅通知并同步时间、使用者和状态。
每 10 秒发送心跳，设备超过 30 秒未收到心跳会标记状态过期。
单独完成配对不能传送任务事件。

240 × 320 界面显示工作、等待、完成、错误状态，以及时间、电量和声音。
UP 切换页面或滚动审批内容，DOWN 切换子页面或拒绝，OK 确认或单次允许，长按 OK 打开菜单。
背光休眠后首次按键只唤醒。待处理请求 ID 和决定锁防止重复审批。
Windows 启动代码使用 `winreg`，安装器写入 `~/.codex/hooks.json`。
这些前提不能被描述为已经适配 Cindy／macOS。

Cindy 可以借鉴连接阶段提示、任务详情更新、有界消息、工作任务负责 I/O 和反馈接收确认。
导入 Codex Hook、审批语义或参考固件不能接通 Cindy。
未来审批适配器必须接入真实 Cindy 请求 ID、过期和取消事件，设备按键才能授权操作。

## 当前 Cindy 链路

```text
任务写入程序 -> bridge/tasks.json -> 本地 HTTP 桥 -> Wi-Fi -> Cindy Tasks
设备录音 -> HTTP POST /feedback -> 电脑上的 WAV 归档
```

按示例填写被忽略的 `main/app_config.h`：2.4 GHz Wi-Fi 凭证，以及电脑 Wi-Fi
局域网地址和 8787 端口。运行 `python3 bridge/server.py --port 8787`；
`/health` 检查服务，`/tasks` 返回文件内容。VPN 默认路由对应的接口并非 Wi-Fi 局域网接口。
固件含本地配置，不能公开上传。

Tasks 自己持有 STA 网络接口及 DHCP 客户端。扫描到热点不能证明认证或 DHCP 成功。
页面显示连接中、获取 IP、数字断开原因、初始化错误或桥接错误；收到 IP 事件后才轮询。
退出页面时先停止工作任务，释放 Wi-Fi 并删除屏幕，其他演示页才能使用无线资源。
按键回调只排队事件，应用任务在 LVGL 锁保护下处理页面生命周期。

UP／DOWN 在可滚动列表中选择任务，OK 进入详情。详情随轮询更新，UP／DOWN 切换任务。
OK 开始录音，再按 OK 停止并发送；达到 30 秒录音上限也会发送。
长按 OK 退出并放弃尚未结束的录音。电量显示在右上方云朵下方。
界面仍使用英文，因为当前字体不含中文字形。

桥接服务目前没有自动订阅 Cindy 会话、转写录音或把回复交给智能体。
WAV 归档只确认收到录音。不能把示例任务描述为实时 Cindy 事件，或声称录音已能控制 Cindy。
自动会话状态与反馈路由需要明确的 Cindy 适配器，与本次固件连接修复分别验证。

## 构建与验收

激活 ESP-IDF 5.5.3，运行 `./tools/validate.sh`，合并固件输出到
`build/FoloToy-AI-Passport-full.bin`。保留 8 MB Flash、3 MB 应用上限和
`0x356000` 处的 `cardid`。合并文件在 `cardid` 前结束时，可从 `0x0` 写入，关闭整片擦除。
已配置身份的卡片禁止整片擦除。

主机回归测试通过 ESP-IDF 测试替身执行实际 STA 初始化、DHCP 事件、断开原因、
清理、初始化失败和再次进入流程。实物仍需验证 Wi-Fi 认证／DHCP、任务显示、录音上传，
以及反复切换 Tasks／扫描／菜单。分别报告 Build、Host tests、Device tests 和 Unverified。

## 分块录音

[AI Passport 小智实现](https://github.com/FoloToy/folo-ai-passport-xiaozhi/tree/d24fce080d86d7cc642f71585f6efde40fb99104/main/audio)
采用分帧采集、有限队列和 Opus 传输。Cindy 借鉴有限队列和持续传输，当前局域网桥接接收未压缩 PCM（每秒 32,000 字节），本次不引入 Opus 编码器。

8 个 512 字节 PCM 块将麦克风采集与网络写入分开。队列元数据、上传任务栈和 HTTP 缓冲还会占用额外内存。队列满时最多等待 20 ms，让优先级较低的上传任务处理麦克风缓存突发；持续拥堵则终止录音并报错，避免静默丢失语音。开始和结束日志包含空闲堆、最大连续块、字节数及错误码，不包含音频或凭据。采集连续性、实时性及反复录音后的堆稳定性仍须设备验收。

POST /feedback 要求 chunked 16 kHz / 16 位 / 单声道 PCM，每块最多 512 字节，总量最多 30 秒。桥接服务先写临时 WAV，收到合法结束块后才改名保存，并返回 HTTP 201。取消或截断会删除临时文件。没有收到确认则显示发送失败，设备不会自动重试。固件和桥接服务必须一起更新，不再接受固定 Content-Length 上传。

录音控制流程的主机回归测试模拟缓存输入，以及在采集任务等待时运行的上传任务，覆盖反复录音、持续拥堵、取消、录音上限和资源释放。测试不代表硬件调度与音频连续性验收。桥接日志用 `source=file` 标明任务来源，并在缺少结束块时记录被丢弃的音频字节数；这些日志不代表已订阅 Cindy 实时任务。

录音临时使用 `WIFI_PS_NONE`，在成功、取消和失败时恢复原模式。上传 socket 启用 `TCP_NODELAY`，避免 PCM 帧之间的 Nagle 延迟。任一配置失败则终止录音。这些设置通过增加录音期间的无线活动降低延迟，板级功耗和真实网络可靠性仍需测量。结束日志包含采集/发送字节数和 HTTP 最长写入耗时。队列等待仍限制在 20 ms，不增加音频缓冲内存。
