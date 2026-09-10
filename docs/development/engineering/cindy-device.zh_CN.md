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
OK 开始录音，再按 OK 停止并发送；达到按内存计算的录音上限也会发送。
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
