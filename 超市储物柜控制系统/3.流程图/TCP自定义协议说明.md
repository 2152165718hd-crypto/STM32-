# TCP 自定义协议说明

## 1. 连接方式

- 设备热点：`don1ng`
- 热点密码：`88888888`
- 设备 IP：`192.168.4.1`
- TCP 端口：`9000`

手机或上位机先连接设备热点，再建立 TCP 长连接。

## 2. 协议格式

- 采用 `JSON 行协议`
- 每条报文是一行 UTF-8 JSON
- 必须以 `\n` 结尾
- 单行最大长度：`256` 字节
- 支持 TCP 粘包和分包

### 请求统一格式

```json
{"type":"cmd","cmd":"get_status","seq":1}
```

### 公共字段

| 字段 | 说明 |
| --- | --- |
| `type` | 固定为 `cmd` |
| `cmd` | 命令名 |
| `seq` | 可选，命令序号，响应会原样带回 |
| `locker` | 开柜命令使用，柜号范围 `1-16` |

## 3. 客户端命令

### 3.1 查询状态

```json
{"type":"cmd","cmd":"get_status","seq":1}
```

### 3.2 管理员开柜

```json
{"type":"cmd","cmd":"open_locker","seq":2,"locker":3}
```

### 3.3 清除报警

```json
{"type":"cmd","cmd":"clear_alarm","seq":3}
```

### 3.4 导出记录

```json
{"type":"cmd","cmd":"export_records","seq":4}
```

## 4. 设备响应

### 4.1 hello

连接成功后设备主动发送一次：

```json
{"type":"hello","device":"locker","proto":1,"ip":"192.168.4.1","port":9000,"slots":16}
```

### 4.2 ack

命令被接受或执行成功：

```json
{"type":"ack","seq":1,"cmd":"get_status","result":"ok"}
```

### 4.3 status

```json
{"type":"status","proto":1,"slots":16,"used":2,"free":14,"alarm":0,"door_open":1,"locker":3,"lockers":[0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `used` | 已占用柜格数 |
| `free` | 空闲柜格数 |
| `alarm` | 报警状态，`0/1` |
| `door_open` | 柜门是否打开，`0/1` |
| `locker` | 当前操作柜号 |
| `lockers` | 16 个柜格占用状态，`1` 表示已占用 |

### 4.4 record

导出记录时逐条发送：

```json
{"type":"record","seq":4,"op":1,"op_name":"store","locker":3,"face":12,"result":1,"time":"2026-05-03 10:12:30"}
```

`op` 含义：

| 值 | 含义 |
| --- | --- |
| `1` | 存物 |
| `2` | 取物 |
| `3` | 管理员开柜 |
| `4` | 清空全部 |
| `5` | 超时报警 |

### 4.5 export_done

```json
{"type":"export_done","seq":4,"count":12}
```

### 4.6 error

```json
{"type":"error","seq":2,"code":"invalid_locker","message":"locker must be 1 to 16"}
```

常见错误码：

| code | 说明 |
| --- | --- |
| `bad_json` | 报文不是合法 JSON 行 |
| `invalid_type` | `type` 不是 `cmd` |
| `unknown_cmd` | 命令不支持 |
| `invalid_locker` | 柜号非法 |
| `door_busy` | 柜门已打开 |
| `export_busy` | 正在导出记录 |
| `line_too_long` | 单行超过 256 字节 |
| `queue_full` | 命令队列满 |

## 5. 使用说明

- `open_locker` 只负责管理员开柜
- 柜门关闭仍需按设备本地按键完成
- 设备会在开柜、关门、清除报警、报警触发、记录导出开始/结束后同步状态
- 建议每条命令都带 `seq`，方便客户端关联响应

## 6. 示例流程

1. 连接热点 `don1ng`
2. TCP 连接 `192.168.4.1:9000`
3. 收到 `hello`
4. 发送 `get_status`
5. 发送 `open_locker`
6. 关闭柜门后收到新的状态更新
7. 发送 `export_records`，逐条接收 `record`，最后收到 `export_done`
