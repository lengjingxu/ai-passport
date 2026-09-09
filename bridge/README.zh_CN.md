<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# Cindy 任务桥

零依赖的本地 HTTP 服务，把 AI Passport 设备和电脑上的任务状态连起来。

## 协议

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| GET | `/tasks` | 每次请求都重新读取 `bridge/tasks.json` 并返回 `{"tasks": [...]}`，任何进程都可以在两次轮询之间改写它。 |
| GET | `/health` | 存活检查。 |
| POST | `/feedback?task_id=<id>&hz=16000&bits=16&ch=1` | 请求体为原始小端 PCM。服务端包装成 WAV 存到 `bridge/feedback/<task_id>/`，并在 `bridge/feedback/log.jsonl` 追加一行记录。 |

设备消费的任务字段：`id`（必填且唯一）、`title`、`status`（`queued` / `running` / `in_progress` / `done` / `completed` / `failed` / `error`）、`message`、`updated_at`（epoch 秒）。设备最多展示 8 条。

## 运行

`bash
python3 bridge/server.py --port 8787
`

## 喂任务数据

`tasks.json` 是唯一接入点。Cindy 会话、定时脚本或手工编辑都可以在轮询之间改写这个文件。任务消息尽量用 ASCII/拉丁字符：设备字体没有中文字形。

## 设备配置

1. 复制 `main/app_config.h.example` 为 `main/app_config.h`（已被 gitignore）。
2. 填写 `APP_WIFI_SSID`、`APP_WIFI_PASSWORD`（2.4GHz）和 `APP_BRIDGE_URL`（本电脑局域网地址，例如 `http://192.168.1.20:8787`）。
3. 按 `docs/development/engineering/build-and-test.md` 构建烧录，在设备菜单里打开 Tasks 页面。
