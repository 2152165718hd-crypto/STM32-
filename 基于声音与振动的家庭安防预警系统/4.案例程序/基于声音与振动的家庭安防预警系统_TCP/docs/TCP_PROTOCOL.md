# Home Security TCP Protocol

本协议用于“基于声音与振动的家庭安防预警系统”设备端、PC 上位机和安卓上位机之间通信。设备继续作为 TCP Server，默认运行在 ESP-01S AP 模式下的 `192.168.4.1:5000`。

## 1. Packet Format

所有 TCP 数据都按“固定包头 + JSON 数据体”封装。TCP 是字节流，接收端必须按 `body_len` 做缓存重组，不能假设一次 `recv` 就是一包。

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 2 | magic | 固定 ASCII `HS` |
| 2 | 1 | version | 当前为 `1` |
| 3 | 1 | header_len | 固定 `16` |
| 4 | 2 | msg_type | 消息类型，大端 |
| 6 | 1 | flags | 保留，当前为 `0` |
| 7 | 1 | reserved | 保留，当前为 `0` |
| 8 | 4 | seq | 序号，大端；请求和响应保持一致 |
| 12 | 4 | body_len | JSON body 长度，大端 |
| 16 | N | body | UTF-8 JSON |

嵌入式端接收 body 上限为 `240` 字节；设备端发送状态和历史数据时按 TCP 字节流分段发送，PC/安卓按包头长度字段重组。

## 2. Message Types

| Name | Value | Direction | Body |
| --- | ---: | --- | --- |
| `LOGIN_REQ` | `0x0001` | PC/Android -> Device | `client_type`, `client_id`, `protocol` |
| `LOGIN_RSP` | `0x0002` | Device -> PC/Android | `ok`, `device_id`, `protocol`, `heartbeat_ms`, `server_ip`, `server_port` |
| `PING` | `0x0003` | PC/Android -> Device | `{}` |
| `PONG` | `0x0004` | Device -> PC/Android | `ok`, `tick` |
| `STATUS_QUERY` | `0x0010` | PC/Android -> Device | `{}` |
| `STATUS_RSP` | `0x0011` | Device -> PC/Android | 设备状态快照 |
| `STATUS_PUSH` | `0x0012` | Device -> PC/Android | 设备状态快照 |
| `AUDIO_REPORT` | `0x0020` | Device -> PC/Android | 声音检测结果 |
| `VIBRATION_REPORT` | `0x0021` | Device -> PC/Android | 振动检测结果 |
| `ALARM_REPORT` | `0x0030` | Device -> PC/Android | 报警事件 |
| `CONFIG_SET` | `0x0040` | PC/Android -> Device | 参数配置 |
| `CONFIG_RSP` | `0x0041` | Device -> PC/Android | 更新后的设备状态 |
| `CONTROL_CMD` | `0x0050` | PC/Android -> Device | `cmd` |
| `CONTROL_RSP` | `0x0051` | Device -> PC/Android | 更新后的设备状态 |
| `HISTORY_QUERY` | `0x0060` | PC/Android -> Device | `{}` |
| `HISTORY_RSP` | `0x0061` | Device -> PC/Android | 最近报警记录 |
| `ERROR_RSP` | `0x00FF` | Device -> PC/Android | `code`, `code_text`, `detail` |

## 3. Status Body

```json
{
  "device_id": "HOME_SECURITY_001",
  "tick": 123456,
  "state": "ARMED",
  "state_code": 2,
  "armed": 1,
  "silenced": 0,
  "reason": "NONE",
  "last_alarm_tick": 0,
  "audio": {
    "freq_hz": 850,
    "energy": 2400,
    "ratio_pct": 35,
    "rms_mv": 62
  },
  "vibration": {
    "freq_hz": 120,
    "peak_mv": 410,
    "energy": 380,
    "zero_cross_permille": 120
  },
  "config": {
    "alarm_hold_ms": 15000,
    "fusion_window_ms": 300,
    "audio_medium_ratio_pct": 40,
    "audio_strong_ratio_pct": 55
  },
  "wifi_ready": 1,
  "clients": 1
}
```

状态枚举：`0 DISARMED`、`1 WARMUP`、`2 ARMED`、`3 SUSPICIOUS`、`4 ALARM`。

## 4. Config and Control

`CONFIG_SET` 支持字段：

| Field | Range |
| --- | --- |
| `alarm_hold_ms` | `3000..60000` |
| `fusion_window_ms` | `100..1000` |
| `audio_medium_ratio_pct` | `20..90` |
| `audio_strong_ratio_pct` | `30..95` |

示例：

```json
{"alarm_hold_ms":15000,"fusion_window_ms":300,"audio_medium_ratio_pct":40,"audio_strong_ratio_pct":55}
```

`CONTROL_CMD` 支持：

```json
{"cmd":"arm"}
{"cmd":"disarm"}
{"cmd":"silence"}
{"cmd":"clear_alarm"}
```

## 5. Reports

声音上报：

```json
{"tick":123456,"level":2,"freq_hz":980,"energy":5800,"ratio_pct":76,"rms_mv":120}
```

振动上报：

```json
{"tick":123456,"level":2,"freq_hz":140,"peak_mv":960,"energy":1800,"zero_cross_permille":180}
```

报警上报：

```json
{
  "tick": 123456,
  "reason": "VIB_STRONG",
  "state": "ALARM",
  "state_code": 4,
  "audio": {"freq_hz": 980, "energy": 5800, "ratio_pct": 76},
  "vibration": {"freq_hz": 140, "peak_mv": 960, "energy": 1800}
}
```

历史记录：

```json
{
  "count": 1,
  "items": [
    {"t":123456,"r":"VIB_STRONG","s":4,"af":980,"ae":5800,"ar":76,"vf":140,"vp":960,"ve":1800}
  ]
}
```

## 6. Error Codes

| Code | Meaning |
| --- | --- |
| `OK` | 成功 |
| `BAD_MAGIC` | 包头 magic 非法 |
| `UNSUPPORTED_VERSION` | 协议版本或包头长度不支持 |
| `FRAME_TOO_LARGE` | body 长度超过上限 |
| `BAD_JSON` | JSON 解析失败 |
| `UNKNOWN_TYPE` | 未知消息类型 |
| `INVALID_FIELD` | 缺少必要字段 |
| `INVALID_VALUE` | 参数值非法或越界 |
| `NOT_LOGIN` | 未登录就访问受保护消息 |
| `TIMEOUT` | 心跳超时 |

## 7. Heartbeat and Reconnect

PC/安卓客户端连接成功后发送 `LOGIN_REQ`，随后每 10 秒发送 `PING`。设备端记录每个 ESP link 的最近活动时间，超过 30 秒未收到任何 TCP 数据则关闭连接并清理登录状态。客户端应在断线后重新建立 TCP 连接并重新登录。

## 8. Example Frame

`PING`，`seq=1`，body 为 `{}`：

```text
48 53 01 10 00 03 00 00 00 00 00 01 00 00 00 02 7B 7D
```

其中 `48 53` 是 `HS`，`00 03` 是 `PING`，`00 00 00 02` 表示 body 长度 2。
