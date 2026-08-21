# 基于声音与振动的家庭安防预警系统 TCP 版

本目录是 HTTP 版本的 TCP 改造版，只修改 `基于声音与振动的家庭安防预警系统_TCP`。设备仍为 STM32F103C8 + ESP-01S，ESP-01S 工作在 AP + TCP Server 模式，默认：

- SSID：`ESP8266_AP`
- 密码：`12345678`
- 设备 IP：`192.168.4.1`
- TCP 端口：`5000`

## 项目结构

```text
Drivers/APPLICATION/Menu/              业务状态机、采样处理、TCP 消息分发
Drivers/APPLICATION/TcpProtocol/       嵌入式 TCP 应用层协议解析与封包
Drivers/Hardware/ESP_01S/              ESP-01S AT 驱动和 TCP Server 管理
Projects/MDK_ARM/                      Keil STM32 工程
PC_TcpUpper/                           PC TCP 上位机，WinForms/C#
Android_TcpUpper/                      安卓 TCP 上位机，原生 Java
docs/TCP_PROTOCOL.md                   TCP 协议说明
tools/protocol_selftest.py             协议粘包/半包/非法包离线自测
tools/tcp_simulator.py                 本地 TCP 设备模拟器
```

## 设备端说明

核心声音/振动检测、报警判断、OLED 菜单、ESP AP TCP Server 初始化逻辑保持不变。HTTP 网页和 GET 路由已替换为 TCP 应用层协议：

- `LOGIN_REQ/RSP`：客户端注册/登录
- `PING/PONG`：心跳
- `STATUS_QUERY/RSP/PUSH`：状态查询和定时推送
- `AUDIO_REPORT`：声音检测结果推送
- `VIBRATION_REPORT`：振动检测结果推送
- `ALARM_REPORT`：报警事件推送
- `CONFIG_SET/RSP`：阈值和采样融合参数下发
- `CONTROL_CMD/RSP`：布防、撤防、静音、清警
- `HISTORY_QUERY/RSP`：最近 16 条报警历史
- `ERROR_RSP`：协议或业务错误

协议格式见 [docs/TCP_PROTOCOL.md](docs/TCP_PROTOCOL.md)。

## PC 上位机运行

源码目录：`PC_TcpUpper`

功能：

- 连接/断开 TCP 服务端
- 自动登录和心跳
- 实时显示设备状态、声音、振动和报警信息
- 查询报警历史
- 下发报警保持时间、融合窗口、声音阈值
- 下发布防、撤防、静音、清警命令
- 显示收发日志和错误信息

运行方式：

1. 使用 Visual Studio 打开 `PC_TcpUpper/PC_TcpUpper.csproj`。
2. 编译并运行。
3. PC 连接 ESP AP 后，保持默认 `192.168.4.1:5000` 点击 Connect。

本机如果只有 .NET Runtime 而没有 .NET SDK，可使用 Visual Studio/MSBuild 或 .NET Framework `csc.exe` 编译。

## 安卓上位机运行

源码目录：`Android_TcpUpper`

功能：

- TCP 连接、断开和自动重连
- 自动登录和心跳
- 实时数据显示和报警弹窗
- 参数配置下发
- 控制命令下发
- 历史报警记录查看

构建方式：

```powershell
cd Android_TcpUpper
gradle assembleDebug
```

APK 输出：

```text
Android_TcpUpper/build/outputs/apk/debug/Android_TcpUpper-debug.apk
```

安装到手机后，手机连接 `ESP8266_AP`，保持默认 `192.168.4.1:5000` 点击 Connect。

## 本地模拟测试

没有实物设备时，可先运行本地模拟器：

```powershell
python tools/tcp_simulator.py --host 127.0.0.1 --port 5000
```

PC 上位机把 Host 改为 `127.0.0.1` 即可验证登录、心跳、状态推送、配置下发、控制命令、报警推送和历史查询。

协议解析自测：

```powershell
python tools/protocol_selftest.py
```

自测覆盖完整包、半包、粘包、非法 magic 和超长 body。

## Keil 构建

工程文件：

```text
Projects/MDK_ARM/Project.uvprojx
```

新增 `TcpProtocol.c/.h` 已加入 Application 分组。可在 Keil uVision 中打开工程编译，也可在安装了 Keil 命令行工具时执行：

```powershell
UV4.exe -b Projects\MDK_ARM\Project.uvprojx -j0
```

## 实机验证流程

1. 编译并下载 STM32 固件。
2. PC 或安卓连接 Wi-Fi `ESP8266_AP`，密码 `12345678`。
3. 上位机连接 `192.168.4.1:5000`。
4. 观察登录响应和状态推送。
5. 修改配置并确认设备返回 `CONFIG_RSP`。
6. 触发声音或振动事件，确认 `AUDIO_REPORT`、`VIBRATION_REPORT` 和 `ALARM_REPORT`。
7. 查询历史记录，确认最近报警快照存在。

## 兼容性与容错

- TCP 粘包/半包：协议层按 `body_len` 缓存解析。
- 非法包头：解析器尝试重新同步到下一个 `HS`。
- 超长包：丢弃缓存并返回错误。
- 未登录访问：返回 `NOT_LOGIN`。
- 心跳超时：设备端 30 秒无数据关闭连接并清理状态。
- 重复连接/断线：按 ESP link 管理登录状态，断线时清理该 link 的解析缓存。
