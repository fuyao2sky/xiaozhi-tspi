# xiaozhi-tspi 开发者指南

> 面向开发者的代码导读文档，帮助快速理解本项目的模块职责、数据流、构建方式、关键依赖与实现细节。

## 1. 项目概览

本项目是“小智 AI 聊天机器人”的泰山派/RK3566 适配版本，代码来源于 `xiaozhi-linux`，并基于 CMake 做了重构，目标是同时支持：

- Ubuntu 本地开发与调试
- RK3566/泰山派交叉编译部署
- 三个独立进程通过本机 UDP 进行 IPC 协作
- 面向云端 AI 服务的 HTTP 激活 + WebSocket 实时音频/文本通信

当前仓库实际由 **3 个独立子项目**组成：

- `ctrl_center/`：核心控制中心
- `sound/`：音频采集、编码、播放
- `gui/`：当前是 CLI 形态的 UI 适配层，不是图形 GUI

> 注意：你提供的参考架构中提到的 LVGL / FreeType 图形界面，在**当前仓库代码里尚未实现**。根据根目录 `README.md`，`gui` 目前只是一个“接收 JSON 并在终端输出”的轻量 CLI 模块，后续计划才是替换成 Qt 或其他真正的 GUI。

---

## 2. 总体软件架构

### 2.1 三进程结构

项目采用“三进程 + 本地 UDP IPC”的分布式架构：

```text
┌───────────────────────────────────────────────────────┐
│                   云端 AI 服务                        │
│  HTTPS 激活接口 + WSS 实时会话接口                   │
└───────────────────────────────────────────────────────┘
                     ▲                      ▲
                     │ HTTP 激活             │ WebSocket 文本/二进制音频
                     │                      │
                     ▼                      ▼
┌───────────────────────────────────────────────────────┐
│                ctrl_center / my_ctrl_center           │
│  - 设备激活                                           │
│  - UUID / MAC 管理                                    │
│  - WebSocket 会话管理                                 │
│  - 状态机 / 文本消息分发                              │
│  - UI 与音频中枢                                       │
└───────────────────────────────────────────────────────┘
              ▲                            ▲
              │ UDP 5676/5677             │ UDP 5678/5679
              │                            │
              ▼                            ▼
┌───────────────────────┐      ┌────────────────────────┐
│   sound / my_sound    │      │      gui / my_gui      │
│ - ALSA 录音           │      │ - 接收状态/文本 JSON    │
│ - Opus 编解码         │      │ - 终端打印解析结果      │
│ - UDP 音频收发        │      │ - 预留上行 UI 控制      │
│ - 录音/播放双线程     │      │                        │
└───────────────────────┘      └────────────────────────┘
```

### 2.2 为什么用 UDP 做本地 IPC

本仓库没有引入共享内存、消息队列或 gRPC，而是选择本地回环地址 `127.0.0.1` 上的 UDP：

- 实现简单，便于不同进程完全解耦
- 跨模块接口稳定，音频模块和控制中心可独立启动/替换
- 对小包消息（JSON 状态、单帧 Opus）足够轻量
- 与原始项目思路贴近

代价也很明显：

- 没有可靠传输保障
- 没有流量控制、顺序保证
- 音频帧/状态消息丢失时需要上层容错

当前代码中，这种容错主要依赖“持续流式发送”和“状态不断覆盖”，而不是消息确认机制。

---

## 3. 目录结构速览

```text
xiaozhi-tspi/
├── README.md
├── DEVELOPER_GUIDE.md
├── ctrl_center/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── http.h
│   │   ├── ipc_udp.h
│   │   ├── json.hpp
│   │   ├── uuid.h
│   │   └── websocket_client.h
│   ├── src/
│   │   ├── http.cpp
│   │   ├── ipc_udp.cpp
│   │   ├── main.cpp
│   │   ├── uuid.cpp
│   │   └── websocket_client.cpp
│   ├── tests/
│   └── third_party/websocketpp/
├── sound/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── aplay.h
│   │   ├── ipc_udp.h
│   │   ├── opus.h
│   │   └── record.h
│   ├── src/
│   │   ├── aplay.cpp
│   │   ├── ipc_udp.cpp
│   │   ├── main.cpp
│   │   ├── opus.cpp
│   │   └── record.cpp
│   └── tests/
└── gui/
    ├── CMakeLists.txt
    ├── include/
    │   ├── ipc_udp.h
    │   └── json.hpp
    └── src/
        ├── ipc_udp.cpp
        └── main.cpp
```

---

## 4. 技术栈与依赖说明

## 4.1 语言与构建系统

- **语言**：C++17
- **构建系统**：CMake 3.14+
- **并发模型**：POSIX `pthread` + 模块内部回调线程
- **目标环境**：Ubuntu、RK3566/Buildroot 交叉编译

## 4.2 control_center 依赖

`ctrl_center/CMakeLists.txt` 中体现的依赖：

- **libcurl**：设备激活 HTTP POST
- **Boost.System**：WebSocketPP/Asio 相关系统依赖
- **OpenSSL**：WSS/TLS 加密连接
- **websocketpp**：WebSocket 客户端实现（头文件库）
- **nlohmann/json**：JSON 解析与生成
- **pthread**：异步网络/IPC 回调线程

## 4.3 sound 依赖

`sound/CMakeLists.txt` 中体现的依赖：

- **ALSA (`asound`)**：录音与播放硬件访问
- **Opus (`opus`)**：语音编码/解码
- **SpeexDSP (`speexdsp`)**：主要用于重采样，头文件中也可见预处理依赖
- **pthread**：录音线程、播放线程

## 4.4 gui 依赖

- **nlohmann/json**：解析 control_center 下发的 UI 消息
- **pthread**：UDP 回调线程

> 当前 `gui` 没有真正的图形依赖；也没有 LVGL/Qt 代码。

## 4.5 测试栈

- **Google Test**
- `ctrl_center/tests/`：HTTP / UUID / IPC 测试
- `sound/tests/`：录音、播放、Opus 测试

注意：`ctrl_center` 的 GTest 默认只在 Debug 构建中启用；`sound` 则直接 `add_subdirectory(third_party/googletest)`，但仓库根目录当前并未展示该 third_party 内容是否完整，接手者需要结合本地依赖环境验证。

---

## 5. 进程间通信（IPC）设计

### 5.1 端口分配

三个模块都硬编码了相同的 UDP 端口映射：

| 方向 | 端口 | 含义 |
|---|---:|---|
| `AUDIO_PORT_UP` | 5676 | `sound -> ctrl_center` 音频上行 |
| `AUDIO_PORT_DOWN` | 5677 | `ctrl_center -> sound` 音频下行 |
| `UI_PORT_UP` | 5678 | `gui -> ctrl_center` UI 上行 |
| `UI_PORT_DOWN` | 5679 | `ctrl_center -> gui` UI 下行 |

定义位置主要见：

- `ctrl_center/src/main.cpp`
- `sound/src/main.cpp`
- `gui/src/main.cpp`

### 5.2 IPC 抽象层

三个子项目都各自带了一份几乎相同的 `ipc_udp.cpp` / `ipc_udp.h`，它封装了统一的 endpoint 结构：

- `ipc_endpoint_create_udp(local_port, remote_port, cb, user_data)`
- `send(...)`
- `recv(...)`
- `ipc_endpoint_destroy_udp(...)`

其核心逻辑是：

1. 创建一个发送 socket，目标固定为 `127.0.0.1:remote_port`
2. 创建一个接收 socket，绑定 `127.0.0.1:local_port`
3. 若传入回调 `cb`，额外创建一个线程阻塞在 `recvfrom`
4. 收到数据后直接调用回调处理

### 5.3 IPC 消息类型

#### JSON 文本消息

control_center 下发到 `gui` 的消息目前主要包括：

```json
{"state": 5}
{"text": "你好，有什么可以帮到你"}
```

`gui/src/main.cpp` 还兼容解析：

- `emotion`
- `wifi`
- `battery`

但这些字段在当前 `ctrl_center` 代码里并未完整下发。

#### 二进制音频消息

- `sound -> ctrl_center`：Opus 编码后的音频帧
- `ctrl_center -> sound`：云端下发的 Opus TTS 音频帧

---

## 6. control_center 模块详解

`ctrl_center` 是整个系统的大脑，负责连接本地两个子模块与云端服务。

### 6.1 入口文件与职责分配

核心入口：`ctrl_center/src/main.cpp`

相关源码职责：

- `main.cpp`：整体业务流程、状态机、消息分发
- `http.cpp`：设备激活 HTTP 请求
- `websocket_client.cpp`：WSS 客户端、TLS 与消息收发
- `uuid.cpp`：UUID 配置读写、MAC 地址获取
- `ipc_udp.cpp`：本地 UDP IPC

### 6.2 启动时序

可以把启动流程理解为 4 个阶段。

#### 阶段 1：设备身份准备

1. 通过 `get_wireless_mac_address()` 获取无线网卡 MAC
   - 优先找 `wlan*` / `wlp*`
   - 找不到就退化到“第一个可用网卡”
2. 从配置文件读取 UUID：
   - 常量：`CFG_FILE = /home/grand/xiaozhi-desktop/cfg/xiaozhi.cfg`
   - 调用：`read_uuid_from_config(cfg_file)`
3. 若配置中没有 UUID：
   - `generate_uuid()` 生成新 UUID
   - `write_uuid_to_config(uuid, cfg_file)` 持久化

这里的设计把：

- **MAC** 当作硬件标识（更接近 `Device-Id`）
- **UUID** 当作软件实例标识（更接近 `Client-Id`）

#### 阶段 2：初始化本地 IPC

主函数中创建两个 endpoint：

- `g_ipc_ep_audio = ipc_endpoint_create_udp(AUDIO_PORT_UP, AUDIO_PORT_DOWN, process_opus_data_uploaded, NULL);`
- `g_ipc_ep_ui = ipc_endpoint_create_udp(UI_PORT_UP, UI_PORT_DOWN, process_ui_data, NULL);`

含义分别是：

- 音频端点：
  - 监听 `5676`，接收 `sound` 上传音频
  - 发往 `5677`，把云端 TTS 下发给 `sound`
- UI 端点：
  - 发往 `5679`，把状态/文本推给 `gui`
  - 监听 `5678`，预留接收 GUI 上行

> 当前 `process_ui_data()` 是空实现，因此 GUI 上行控制链路尚未真正启用。

#### 阶段 3：设备激活

`http_data_t` 组装完成后，调用：

- `active_device(&http_data, active_code)`

请求目标：

- `https://api.tenclass.net/xiaozhi/ota/`

POST 内容包括：

- UUID
- application name/version
- board type/name

Header 包括：

- `Content-Type: application/json`
- `Device-Id: <mac>`
- `User-Agent`
- `Accept-Language`

激活逻辑：

- 返回 `0`：已激活
- 返回 `1`：未激活，但拿到了激活码
- 返回 `-1`：失败

主循环中会每 5 秒重试一次；若拿到激活码，则：

- 状态切换到 `kDeviceStateActivating`
- 通过 `send_device_state()` 通知 GUI
- 通过 `send_stt("Active-Code: ...")` 显示激活码

#### 阶段 4：建立 WebSocket 会话

激活成功后，control_center 继续构造 `websocket_data_t`：

- `hostname`
- `port`
- `path`
- `headers`
- `hello`

随后：

1. `websocket_set_callbacks(process_opus_data_downloaded, process_txt_data_downloaded, &ws_data)`
2. `websocket_start()`

### 6.3 WebSocket 会话模型

#### 连接地址

从代码设计与用户提供资料可知，目标是云端小智接口，路径类似：

- `wss://api.tenclass.net:443/xiaozhi/v1/`

#### Header 语义

实际代码会拼装至少这些身份信息：

- `Authorization`
- `Device-Id`
- `Client-Id`
- `Protocol-Version`

#### hello 报文

连接建立后发送的 hello 文本中，会声明：

- `type = hello`
- `version = 1`
- `transport = websocket`
- `audio_params.format = opus`
- `audio_params.sample_rate = 16000`
- `audio_params.channels = 1`
- `audio_params.frame_duration = 60`

这与 `sound` 侧编码目标完全对应。

### 6.4 control_center 内部状态机

`main.cpp` 定义了 `DeviceState`：

- `kDeviceStateUnknown`
- `kDeviceStateStarting`
- `kDeviceStateWifiConfiguring`
- `kDeviceStateIdle`
- `kDeviceStateConnecting`
- `kDeviceStateListening`
- `kDeviceStateSpeaking`
- `kDeviceStateUpgrading`
- `kDeviceStateActivating`
- `kDeviceStateFatalError`

状态同步方式很简单：

- `set_device_state(state)`：只更新全局变量
- `send_device_state()`：序列化成 `{"state": N}` 发给 GUI

这说明当前状态机更偏“显示状态同步”，而不是带严格状态迁移约束的有限状态机框架。

### 6.5 文本消息处理链

云端下发文本消息后，最终流转为：

1. `websocket_client.cpp::on_message()`
2. `process_txt_data_downloaded()`
3. 根据 `type` 分流：
   - `hello` -> `process_hello_json()`
   - 其他 -> `process_other_json()`

#### `process_hello_json()` 做了什么

- 解析云端返回的音频参数
- 保存 `session_id`
- 上报 3 组 IoT 描述符：
  - Speaker
  - Backlight
  - Battery
- 发送初始 `listen start` 请求
- 上报一次设备 state
- 打开 `g_audio_upload_enable`

这说明 control_center 不只是聊天代理，也在模拟“可控智能设备”的能力描述。

#### `process_other_json()` 重点逻辑

##### 1) `type = tts`

- `state = start`
  - 关闭音频上传：`g_audio_upload_enable = 0`
  - 通知 GUI 状态
- `state = sentence_start`
  - 提取 `text`
  - `send_stt(text)` 推给 GUI
  - 设置为 `kDeviceStateSpeaking`
- `state = stop`
  - `sleep(2)` 做简单防回声缓冲
  - 再次发送监听请求 `send_start_listening_req(kListeningModeAutoStop)`
  - 状态切回 `kDeviceStateListening`
  - 恢复音频上传 `g_audio_upload_enable = 1`

##### 2) `type = stt`

- 直接提取识别文本
- 调用 `send_stt(text)` 发往 GUI

##### 3) `type = llm`

代码里读取了 `emotion` 字段，但尚未真正下发给 GUI 或驱动其他设备能力。

##### 4) `type = iot`

当前预留空分支，未来可以扩展设备控制、属性同步等能力。

### 6.6 音频转发链

#### 上行：本地录音 -> 云端

`sound` 将 Opus 数据通过 UDP 发到 5676 后，会触发：

- `process_opus_data_uploaded(char *buffer, size_t size, void *user_data)`

逻辑非常直接：

- 若 `g_audio_upload_enable == 1`
- 每 100 帧打印一次调试日志
- `websocket_send_binary(buffer, size)` 发给云端

#### 下行：云端 TTS -> 本地播放

当 WebSocket 收到二进制音频时，会回调：

- `process_opus_data_downloaded(const char *buffer, size_t size)`

逻辑是：

- `g_ipc_ep_audio->send(g_ipc_ep_audio, buffer, size)`

也就是直接把云端音频转发给 `sound` 进程做解码与播放。

### 6.7 HTTP 激活实现细节

`ctrl_center/src/http.cpp` 使用 libcurl：

- `CURLOPT_URL`
- `CURLOPT_POSTFIELDS`
- `CURLOPT_HTTPHEADER`
- `CURLOPT_WRITEFUNCTION`

返回内容通过 `nlohmann::json` 解析。

当前只关注一个关键字段：

- `activation.code`

若存在这个字段，认为设备未激活；否则认为设备已激活。

### 6.8 UUID 与配置持久化

`ctrl_center/src/uuid.cpp` 包含 4 个关键函数：

- `get_wireless_mac_address()`
- `generate_uuid()`
- `read_uuid_from_config()`
- `write_uuid_to_config()`

设计特点：

- UUID 文件是纯 JSON，不是 ini 或 kv
- 读取失败、JSON 异常有明确日志输出
- UUID 生成逻辑为自定义随机串，而非系统 uuid 库

---

## 7. sound 模块详解

`sound` 负责与声卡交互，并把音频转为云端协议要求的 Opus 帧。

### 7.1 核心职责

- 录音：通过 ALSA 从麦克风抓取 PCM
- 编码：重采样/混声道后编码为 Opus
- 上传：通过 UDP 发给 control_center
- 下载：从 UDP 接收云端 TTS Opus
- 解码：Opus -> PCM
- 播放：通过 ALSA 输出到扬声器

### 7.2 线程模型

`sound/src/main.cpp` 中主函数做了三件事：

1. 创建音频 UDP endpoint
2. 创建录音线程 `create_record_thread(record_callback, NULL)`
3. 创建播放线程 `create_play_thread(play_get_data_callback, NULL)`

这两个线程各自独立，形成“录音上行”和“播放下行”两条并发数据通路。

### 7.3 录音链路：`record_callback`

录音线程每收到一段 PCM 数据，就会调用：

- `record_callback(unsigned char *buffer, size_t size, void *user_data)`

其内部逻辑：

#### 第一次回调时初始化编码器

- `get_actual_record_settings(...)` 获取真实声卡参数
- `init_opus_encoder(inputSampleRate, inputChannels, 60, 16000, 1)`

这表示：

- 输入：录音硬件实际采样率/声道数
- 输出：固定转成 **16kHz / 单声道 / 60ms**

#### 缓冲累积

数据先进入：

- `g_record_buffer`
- 使用 `g_record_buffer_offset` 维护当前缓存长度

#### 到达 60ms 阈值后编码

满足 `g_record_buffer_offset >= g_originalPCMDataSize` 后：

- 每次取一段完整 60ms PCM
- `pcm2opus(...)` 编码
- 编码结果存入 `g_opus_record_buffer`
- 通过 `g_ipc_ep->send(...)` 发送到 control_center

#### 剩余数据保留

如果末尾不足 60ms：

- `memcpy` 到缓冲开头
- 等待下一批录音数据补齐

### 7.4 播放链路：`play_get_data_callback`

播放线程在设备需要音频数据时调用：

- `play_get_data_callback(unsigned char *buffer, size_t size)`

逻辑：

#### 第一次回调时初始化解码器

- `get_actual_play_settings(...)`
- `init_opus_decoder(16000, 1, 60, outputSampleRate, outputChannels)`

说明输入永远按云端约定：**16kHz / 单声道 Opus**，输出转换成当前声卡需要的 PCM 格式。

#### 缓冲不足时拉取 UDP 音频

若 `play_buffer_offset < size`：

- `g_ipc_ep->recv(...)` 从 `ctrl_center` 接收 Opus 帧
- 写入 `g_opus_play_buffer`
- `opus2pcm(...)` 解码到 `g_play_buffer`
- 累加 `play_buffer_offset`

#### 复制给 ALSA

- `memcpy(buffer, g_play_buffer, size)`
- `memmove(...)` 把剩余 PCM 前移

这是一个典型的“解码缓冲区 + 消费前移”模型。

### 7.5 Opus 编解码实现

`sound/src/opus.cpp` 是音频算法核心。

#### 编码端 `init_opus_encoder()`

做了两层初始化：

1. `speex_resampler_init(...)`
2. `opus_encoder_create(...)`

并且设置：

- `OPUS_SET_BITRATE(64000)`

#### `pcm2opus()` 的关键步骤

1. 计算原始帧大小和目标帧大小
2. 把输入 PCM 读到 `rawFrame`
3. 根据声道数做通道变换
   - 多声道 -> 单声道时做平均混音
4. 用 SpeexDSP 重采样到目标采样率
5. 用 `opus_encode()` 编码
6. 把结果拼接到输出缓冲

#### 解码端 `init_opus_decoder()`

同样先建：

- Speex 重采样器
- OpusDecoder

#### `opus2pcm()`

负责：

- 先按 Opus 帧解码成 16kHz 单声道 PCM
- 再重采样/扩展到播放硬件的实际参数

> 这解释了为什么仓库里录音参数可以是 44.1k / 双声道，而网络协议仍稳定为 16k / 单声道。

### 7.6 音频格式与关键参数

从代码和业务约定综合来看：

- 录音硬件输入：依设备而定，常见为 `44100Hz / 2ch / 16bit`
- 网络上传格式：`16000Hz / 1ch / Opus / 60ms`
- 云端返回格式：`16000Hz / 1ch / Opus / 60ms`
- 播放硬件输出：依设备实际配置

### 7.7 ALSA 封装

虽然这里没有展开 `record.cpp` / `aplay.cpp` 全文，但从头文件可以看出其职责边界：

- `create_record_thread(...)`
- `get_actual_record_settings(...)`
- `create_play_thread(...)`
- `get_actual_play_settings(...)`

也就是说，ALSA 的复杂细节被封装在独立文件中，对上层主流程只暴露“线程 + callback + 当前硬件配置”三类接口。

---

## 8. gui 模块详解

### 8.1 当前真实定位

尽管你提供的参考资料把 GUI 描述为 LVGL 图形界面，但当前仓库中的 `gui` 更准确地说是：

- **终端 UI 适配进程**
- **用于验证 control_center -> UI IPC 是否正常**
- **未来图形界面的替身/占位模块**

### 8.2 入口逻辑

`gui/src/main.cpp` 做的事情很少：

1. 创建 UDP endpoint：
   - 本地接收 `5679`
   - 远端发送 `5678`
2. 注册回调 `process_ui_data`
3. 打印启动信息
4. 保持主循环不退出

### 8.3 UI 消息解析

`process_ui_data()` 会把收到的 JSON 转成终端输出，识别字段包括：

- `state`
- `text`
- `emotion`
- `wifi`
- `battery`

例如：

```json
{"state": 5}
{"text": "你好呀，我是小智"}
```

在终端会输出成带标签的解析结果。

### 8.4 与未来 GUI 的关系

如果后续要换成真正图形界面，这个模块的边界其实已经比较清晰：

- **输入接口不变**：继续监听 `5679` JSON
- **可选输出接口**：继续通过 `5678` 向 control_center 发 UI 控制消息
- **只需替换展示层**：把 CLI 打印换成 Qt/LVGL 的控件更新

因此它可以被看作“真正 GUI 的协议原型”。

---

## 9. 端到端数据流

## 9.1 启动数据流

```text
my_ctrl_center 启动
  -> 获取 MAC
  -> 读取/生成 UUID
  -> 初始化 audio/ui UDP 端点
  -> 调用 HTTP 激活接口
  -> 激活成功后建立 WebSocket 连接
  -> 发送 hello
  -> 等待云端回包与音频流
```

## 9.2 语音上行链路

```text
麦克风
  -> ALSA 录音
  -> record_callback
  -> g_record_buffer 聚合 60ms PCM
  -> pcm2opus
  -> UDP 5676
  -> ctrl_center/process_opus_data_uploaded
  -> websocket_send_binary
  -> 云端 STT / LLM / TTS
```

## 9.3 文本与控制下行链路

```text
云端 JSON 消息
  -> websocket on_message
  -> process_txt_data_downloaded
  -> process_hello_json / process_other_json
  -> send_device_state / send_stt
  -> UDP 5679
  -> gui/process_ui_data
  -> 终端展示
```

## 9.4 音频下行链路

```text
云端 TTS Opus
  -> websocket on_message (binary)
  -> process_opus_data_downloaded
  -> UDP 5677
  -> sound/play_get_data_callback
  -> opus2pcm
  -> ALSA 播放
  -> 扬声器输出
```

## 9.5 一次完整对话的状态切换

一个典型交互过程大致是：

1. 激活完成 -> `Idle`
2. 建立会话后开始监听 -> `Listening`
3. 用户说话，音频持续上传
4. 云端返回 `stt` -> GUI 显示识别文本
5. 云端返回 `tts start` -> 本地关闭录音上传
6. 云端返回 `tts sentence_start` -> GUI 显示播报文本，状态切换 `Speaking`
7. 云端下发 Opus TTS -> 扬声器播放
8. 云端返回 `tts stop`
   - 等待 2 秒
   - 再次请求开始监听
   - 恢复音频上传
   - 状态回到 `Listening`

---

## 10. 关键协议与消息语义

### 10.1 WebSocket 文本消息类型

当前代码明确处理或预留了这些 `type`：

- `hello`
- `listen`
- `stt`
- `tts`
- `llm`
- `iot`

### 10.2 `tts` 状态值

当前显式处理：

- `start`
- `sentence_start`
- `stop`

### 10.3 IOT 描述符

首次 hello 后，control_center 会向云端主动发送设备能力描述，当前包括：

- Speaker
  - 属性：`volume`
  - 方法：`SetVolume`
- Backlight
  - 属性：`brightness`
  - 方法：`SetBrightness`
- Battery
  - 属性：`level`、`charging`

这表明协议预期并不局限于聊天，而是支持智能硬件能力建模。

---

## 11. 构建与开发方式

## 11.1 Ubuntu 本地开发

每个子模块目录都可独立构建。典型方式：

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 典型开发习惯

建议在三个子模块分别建构建目录：

- `ctrl_center/build/`
- `sound/build/`
- `gui/build/`

这样更符合本仓库的“独立进程/独立 CMake 项目”组织方式。

## 11.2 RK3566 交叉编译

每个子模块都带有 `toolchain-rk3566.cmake`，使用方式类似：

```bash
mkdir -p build_rk3566 && cd build_rk3566
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-rk3566.cmake -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 11.3 板端依赖

从各 README 可知，板端/Buildroot 通常需要开启：

- `libcurl`
- `boost`
- `opus`
- `speex`
- ALSA 相关支持
- OpenSSL

---

## 12. 测试结构与可维护性提示

## 12.1 control_center 测试

`ctrl_center/tests/` 目前关注：

- UUID 生成与读写
- HTTP 激活
- IPC

其中 `test_uuid.cpp` 比较适合新开发者快速理解：

- 如何验证 UUID 格式
- 如何测试配置文件读写
- MAC 获取逻辑允许“无无线网卡环境”通过

## 12.2 sound 测试

`sound/tests/` 主要面向：

- Opus 编解码
- 录音/播放封装

由于音频测试通常依赖具体硬件或声卡环境，CI 场景下需要额外做 mock 或跳过策略。

---

## 13. 当前代码的重要事实与架构差异

这是新开发者最容易误判的地方，单独列出来：

### 13.1 当前 GUI 不是图形界面

虽然业务设想里可能是 LVGL/Qt GUI，但当前仓库里的 `gui` 仅仅是：

- UDP 接收器
- JSON 解析器
- 终端日志输出器

### 13.2 代码存在模块复制

`ipc_udp.cpp/h` 在三个子项目中重复存在，后期如果协议变化，需要同步维护三份实现。

### 13.3 配置路径是硬编码的

control_center 使用：

- `/home/grand/xiaozhi-desktop/cfg/xiaozhi.cfg`

这对本地开发、容器环境、系统服务部署都不够友好，未来更适合改成：

- 命令行参数
- 环境变量
- XDG/系统标准配置目录

### 13.4 状态机较轻量

`DeviceState` 只是一个枚举加发送函数，并没有显式状态迁移表、事件中心或统一 reducer。

### 13.5 云端协议已有硬件控制扩展预留

`iot` 描述符和 `llm.emotion` 字段都说明后续可以接：

- 屏幕表情
- 背光亮度
- 音量控制
- 电池状态同步
- 硬件控制动作

这也是将来接入真实 GUI / 真实设备外设的主要扩展点。

---

## 14. 推荐的阅读顺序

如果你是第一次接手这个项目，建议按下面顺序看代码：

1. 根目录 `README.md`
2. `ctrl_center/src/main.cpp`
3. `ctrl_center/src/websocket_client.cpp`
4. `ctrl_center/src/http.cpp`
5. `ctrl_center/src/uuid.cpp`
6. `sound/src/main.cpp`
7. `sound/src/opus.cpp`
8. `gui/src/main.cpp`
9. 三个模块各自的 `ipc_udp.cpp`
10. 各自 `CMakeLists.txt` 和 `tests/`

这样能最快建立“业务流程 -> 关键协议 -> 本地音频实现 -> UI 适配层”的完整心智模型。

---

## 15. 后续扩展建议

结合当前架构，最自然的演进方向有：

1. **把 gui 从 CLI 升级为真实 GUI**
   - Qt / LVGL 都可
   - 保持 UDP JSON 协议兼容，减少对 control_center 的侵入

2. **把 IPC 抽成公共库**
   - 消除三份重复 `ipc_udp` 代码

3. **补齐 GUI 上行控制协议**
   - 麦克风按钮
   - 音量调节
   - 背光控制
   - Wi‑Fi 配置

4. **增强状态机**
   - 用事件驱动方式统一处理 `stt/tts/listen/iot`

5. **增强配置系统**
   - 去除硬编码配置路径
   - 支持 token / server / log-level / device profile 外部配置

6. **提高实时音频鲁棒性**
   - UDP 丢包监控
   - AEC/NS/AGC 接入
   - 更严格的播放/录音缓冲控制

---

## 16. 一句话总结

这个仓库本质上是一个 **“本地三进程语音终端 + 云端 AI 会话代理”**：

- `ctrl_center` 负责身份、连接、协议和业务编排
- `sound` 负责真实的语音采集与播放
- `gui` 目前负责最小化的 UI 协议验证

如果你理解了 **HTTP 激活 -> WebSocket 会话 -> UDP 音频上/下行 -> JSON 状态同步** 这四条主线，就已经掌握了这个项目的大部分核心架构。
