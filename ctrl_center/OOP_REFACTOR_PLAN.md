# ctrl_center 面向对象重构方案（最终版）

> 本文在上一版基础上进一步升级，满足你的新要求：
>
> 1. **AppController 采用“单例 + 工厂”模式**；
> 2. **WebSocket 与 IPC_UDP 都重构为 class**（不再使用过程式接口）；
> 3. **WebSocket 与 IPC_UDP 均采用多线程模型**；
> 4. 给出完整架构思路、运行过程、迁移计划与验收标准。

---

## 1. 目标与边界

## 1.1 重构目标

- 将 `ctrl_center` 从“main + 全局变量 + 函数回调”升级为“类对象 + 生命周期管理 + 可扩展工具路由”；
- 将 `AppController` 设计为系统唯一控制入口（单例），同时通过工厂创建其内部组件（WebSocket/IPC/HTTP/MCP）；
- 将 `llm` 文本消息中的工具调用交给 MCP 调度器，支持后续扩展。

## 1.2 明确替换项

本次方案中，以下旧式调用路径全部下线：

- `websocket_*` 函数调用路径；
- `ipc_endpoint_*` 函数调用路径；
- `active_device` 过程式调用路径。

统一改成类成员对象调用。

---

## 2. 总体架构图（逻辑）

```text
main.cpp
  -> AppController::factoryCreate(AppOptions)
  -> AppController::instance().init()
  -> AppController::instance().run()

AppController (Singleton)
  ├── created by AppControllerFactory
  ├── owns WebSocketClient (class, multi-thread)
  ├── owns UdpChannel(Audio/UI) (class, multi-thread)
  ├── owns HttpActivator (class)
  ├── owns McpRegistry + McpDispatcher (class)
  └── owns State/Session/Config
```

---

## 3. AppController：单例 + 工厂模式设计

## 3.1 为什么“单例 + 工厂”

- **单例**：控制中心天然只能有一个，避免多个实例抢占端口、重复连接云端；
- **工厂**：单例对象内部依赖较多（WS/UDP/HTTP/MCP），工厂负责按配置装配，降低 `main` 复杂度。

## 3.2 建议模式

### A. AppController（Singleton）

- 对外提供：`instance()`（获取唯一实例）；
- 禁止拷贝与赋值；
- 生命周期：`init() -> run() -> stop()`。

### B. AppControllerFactory（Factory）

- 对外提供：`factoryCreate(const AppOptions&)`；
- 负责创建并注入：
  - `std::unique_ptr<WebSocketClient>`
  - `std::unique_ptr<UdpChannel>`（audio/ui 可两实例）
  - `std::unique_ptr<HttpActivator>`
  - `std::unique_ptr<McpDispatcher>` / `McpRegistry`

> 实现上可采用“工厂先构建依赖，再注入单例”的方式，既保留单例唯一性，又保持可配置性。

---

## 4. AppController 详细职责（完整版）

## 4.1 成员字段建议

```cpp
class AppController final {
private:
    // ---- singleton ----
    static std::mutex singletonMtx_;
    static std::unique_ptr<AppController> self_;

    // ---- 基础配置 ----
    AppOptions options_;
    std::string cfgFile_;
    std::string mac_;
    std::string uuid_;
    std::string sessionId_;

    // ---- 业务状态 ----
    std::atomic<bool> running_{false};
    std::atomic<bool> audioUploadEnabled_{true};
    std::atomic<DeviceState> deviceState_{kDeviceStateUnknown};

    // ---- 组件对象（工厂注入） ----
    std::unique_ptr<WebSocketClient> wsClient_;
    std::unique_ptr<UdpChannel> audioUpChannel_;
    std::unique_ptr<UdpChannel> audioDownChannel_;
    std::unique_ptr<UdpChannel> uiUpChannel_;
    std::unique_ptr<UdpChannel> uiDownChannel_;
    std::unique_ptr<HttpActivator> httpActivator_;
    std::unique_ptr<McpDispatcher> mcpDispatcher_;
    std::unique_ptr<McpRegistry> mcpRegistry_;

    // ---- 控制线程 ----
    std::thread monitorThread_;
    std::atomic<bool> stopRequested_{false};
};
```

---

## 4.2 AppController 方法设计（比上一版更细）

### A. 单例/工厂方法

- `static AppController& instance();`
- `static bool factoryCreate(const AppOptions& options);`
- `static void factoryDestroy();`

行为定义：

1. 首次 `factoryCreate` 时创建唯一实例并注入组件；
2. 重复调用返回 false 或更新配置（按策略选择）；
3. `factoryDestroy` 负责 `stop()` 后释放实例。

### B. 生命周期方法

#### `bool init()`

固定顺序（建议不可随意调整）：

1. `loadConfig()`
2. `initDeviceIdentity()`（MAC/UUID）
3. `activateDeviceUntilReady()`（HTTP）
4. `initMcp()`（注册工具）
5. `initIpcChannels()`（UDP）
6. `initWebSocketClient()`（WS）
7. `bindCallbacks()`（WS/UDP 回调绑定到成员函数）

#### `void run()`

- 启动 WS 线程；
- 启动 UDP 接收线程；
- 启动监控线程（重连、健康检查、心跳）；
- 设置状态为 Idle/Listening。

#### `void stop()`

- 设置 `stopRequested_ = true`；
- 停止 WS/UDP 子线程；
- join `monitorThread_`；
- 关闭 socket 与清理资源。

### C. 消息处理方法

- `void onWsText(const std::string& payload);`
- `void onWsBinary(const std::vector<uint8_t>& data);`
- `void onAudioFrameFromLocal(const std::vector<uint8_t>& opusFrame);`
- `void onUiCommand(const std::string& uiJson);`

- `void routeJsonByType(const json& j);`
- `void handleHello(const json& j);`
- `void handleTts(const json& j);`
- `void handleStt(const json& j);`
- `void handleLlm(const json& j);`
- `void handleIot(const json& j);`

### D. 状态与回传方法

- `void setDeviceState(DeviceState s);`
- `void publishDeviceState();`
- `void publishTextToUi(const std::string& text);`
- `void sendStartListening(ListeningMode mode);`
- `void scheduleReconnect();`

### E. 激活与身份方法

- `bool initDeviceIdentity();`
- `bool activateDeviceUntilReady();`
- `ActivationResult requestActivationOnce();`

---

## 5. WebSocket 重构为 class（多线程）

## 5.1 类名与职责

`class WebSocketClient`

职责：

- 管理云端连接（TLS、握手、头部、hello）；
- 异步接收文本/二进制；
- 对外提供线程安全 `sendText/sendBinary`；
- 提供自动重连。

## 5.2 线程模型

至少 2 线程：

1. **I/O线程**：运行 websocket event loop（读取/写入/回调）；
2. **重连线程或定时任务线程**：连接断开后按退避策略重试。

可选：

- **发送队列线程**：若发送压力大，可将 `send` 变成队列消费，避免业务线程阻塞。

## 5.3 对外接口建议

```cpp
class WebSocketClient {
public:
    bool start();
    void stop();
    bool sendText(const std::string& text);
    bool sendBinary(const uint8_t* data, size_t size);

    void setTextCallback(std::function<void(const std::string&)> cb);
    void setBinaryCallback(std::function<void(const std::vector<uint8_t>&)> cb);
    void setOpenCallback(std::function<void()> cb);
    void setCloseCallback(std::function<void(int, const std::string&)> cb);
};
```

## 5.4 并发与安全

- `sendText/sendBinary` 内部加互斥或投递到线程安全队列；
- 连接状态使用原子变量；
- 回调执行时避免长耗时逻辑（交给 AppController 分发线程）。

---

## 6. IPC_UDP 重构为 class（多线程）

## 6.1 类名与职责

`class UdpChannel`

职责：

- 管理单个 UDP 端点（本地端口 + 远程地址）；
- 提供 `send` 与后台 `recv loop`；
- 收包后通过 callback 通知上层。

## 6.2 线程模型

每个 `UdpChannel` 至少 1 个接收线程：

- **Recv线程**：阻塞 `recvfrom`，收到包后回调。

建议按职责建多个 channel：

- `audioUpChannel`（本地录音上行）
- `audioDownChannel`（云端音频下行）
- `uiUpChannel`
- `uiDownChannel`

> 也可用 2 个双向 channel，但分开更清晰，便于调试。

## 6.3 对外接口建议

```cpp
class UdpChannel {
public:
    bool open(const UdpEndpointConfig& cfg);
    void close();
    bool send(const uint8_t* data, size_t size);
    void setRecvCallback(std::function<void(const uint8_t*, size_t)> cb);

    bool startRecvThread();
    void stopRecvThread();
};
```

## 6.4 并发与安全

- `close()` 必须能安全打断阻塞 recv（可用 shutdown/非阻塞+select）；
- 回调与关闭并发时，需先置 stop 标志后再释放 fd；
- 防止回调访问已销毁对象（弱引用或停止顺序控制）。

---

## 7. HTTP 激活重构为 class

## 7.1 类名

`class HttpActivator`

## 7.2 主要方法

- `ActivationResult activate(const ActivationRequest& req);`
- `ActivationResult parseActivationResponse(const std::string& body);`

## 7.3 AppController 调用策略

- `activateDeviceUntilReady()` 内部循环调用 `httpActivator_->activate(...)`；
- `Pending(code)` 时向 UI 发布激活码并等待重试；
- `Activated` 时进入下一阶段。

---

## 8. MCP 层（工具注册与调度）

## 8.1 类结构

- `IMcpTool`：工具接口
- `McpRegistry`：注册/查找工具
- `McpDispatcher`：解析 llm JSON 并调用工具

## 8.2 调度流程

1. `handleLlm(j)` 判断 `intent == tool_call`；
2. 提取 `tool.name/tool.arguments/request_id`；
3. `registry.findTool(name)`；
4. `tool->invoke(arguments)`；
5. 生成 `tool_result` JSON 回传。

## 8.3 JSON 约定

### 输入

```json
{
  "type": "llm",
  "intent": "tool_call",
  "tool": {
    "name": "SetVolume",
    "arguments": {"volume": 66},
    "request_id": "req-66"
  }
}
```

### 成功输出

```json
{
  "type": "tool_result",
  "request_id": "req-66",
  "ok": true,
  "result": {"applied": true},
  "error": ""
}
```

### 失败输出

```json
{
  "type": "tool_result",
  "request_id": "req-66",
  "ok": false,
  "result": {},
  "error": "tool_not_found: SetVolume"
}
```

---

## 9. 重构后的完整运行过程（端到端）

1. `main` 调用 `AppController::factoryCreate(options)`；
2. `AppController::instance().init()`：
   - 加载配置；
   - 初始化身份（MAC/UUID）；
   - HTTP 激活直到成功；
   - 初始化 MCP 工具；
   - 初始化 UDP channel；
   - 初始化 WebSocket client 并绑定回调。
3. `run()` 启动 WS/UDP/监控线程；
4. 本地音频帧通过 `onAudioFrameFromLocal` -> `ws.sendBinary` 上传；
5. 云端返回二进制音频 -> `onWsBinary` -> `audioDownChannel.send`；
6. 云端返回文本 JSON -> `onWsText` -> `routeJsonByType`；
7. 若为 `llm tool_call` -> `McpDispatcher` 执行工具 -> 返回 `tool_result`；
8. 断线由 `scheduleReconnect()` 执行退避重连；
9. `stop()` 时按顺序关闭线程与资源。

---

## 10. 迁移计划（建议 4 个里程碑）

## M1：框架落地（AppController 单例 + 工厂）

- 完成 `factoryCreate/instance/init/run/stop` 框架；
- `main` 仅保留入口逻辑。

## M2：Transport class 化

- 引入 `WebSocketClient` class（替代旧过程式）；
- 引入 `UdpChannel` class（替代旧过程式）；
- 完成多线程收发。

## M3：HTTP class 化 + 激活迁移

- 引入 `HttpActivator` class；
- 删除 `active_device` 依赖路径。

## M4：MCP 接入与验证

- 接入 `McpRegistry/McpDispatcher`；
- 至少注册 1 个工具（如 `SetVolumeTool`）；
- 验证 `tool_call -> tool_result` 闭环。

---

## 11. 验收标准

### 结构验收

- `main.cpp` 不再承载业务分发；
- `AppController` 为唯一业务编排点（单例）；
- WS/UDP/HTTP 均为 class 对象调用；
- 不存在对 `websocket_*`、`ipc_endpoint_*`、`active_device` 的主路径依赖。

### 运行验收

- 启动后能正常激活、连接、收发音频、处理文本；
- 断线可自动重连；
- MCP 调用可根据 JSON 正确分发工具并返回结果。

### 并发验收

- stop 时无僵尸线程；
- 高频音频发送场景无明显阻塞；
- 回调与关闭并发不出现野指针访问。

---

## 12. 风险与规避

1. **多线程复杂度提升**：统一线程命名、日志前缀、生命周期顺序；
2. **资源释放顺序错误**：先停回调源，再释放对象；
3. **重连风暴**：指数退避 + 最大重试间隔；
4. **MCP 参数污染**：dispatcher 层做字段白名单校验。

---

## 13. 结论

这个版本满足你的全部新增要求：

- `AppController` 使用**单例工厂模式**；
- `WebSocket` 与 `IPC_UDP` 都是**独立 class + 多线程**；
- `HTTP` 激活也收敛为 class；
- 保留 MCP 扩展点，并给出完整运行链路与迁移路线。

可直接据此进入下一步：先落地类骨架（头文件与空实现），再分阶段迁移旧逻辑。
