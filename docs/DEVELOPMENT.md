# 开发指南

本文档面向 tts_server 的二次开发，涵盖架构原理、模块边界、扩展方式与调试技巧。

---

## 1. 总体架构（四阶段 SEDA）

所有业务严格遵循四阶段流转，**不允许跨阶段调用**，阶段间通过明确的类型契约通信：

```
阶段1 外部输入 (gRPC IO 线程)
   │
   ▼ TtsRequestMsg (纯值，深拷贝)
阶段2 消息传递 (CopyChannel)
   │
   ▼ vector<Message>
阶段3 消息处理 (心跳线程，单线程)
   │   ├─ ProcessBatch (汇总处理)
   │   └─ DataEvolutionEngine::EvolveOnce (单层触发)
   ▼ ChangeSet { ws_frames, client_writes, close_sessions, local_jobs, emit_messages }
阶段4 执行副作用 (IO/CPU 线程池)
   │
   └─► 阶段1 (闭环)
```

**关键不变量**：

| # | 约束 | 实现位置 |
|---|---|---|
| 1 | DataBoard 仅心跳线程单写 | `databoard/*` |
| 2 | 跨线程一律深拷贝 | `framework/copy_channel.h` |
| 3 | 监听者禁止直接写黑板 | `framework/event_bus.h` + `observability/*` |
| 4 | 单层数据演进，禁连锁 | `heartbeat/evolution_engine.h` |
| 5 | 真实时钟一律 IClock 注入 | `framework/clock.h` |
| 6 | 消息处理不可跳过；定时任务可跳 | `framework/priority_timer.h` |

---

## 2. 模块边界

```
src/
├─ framework/      通用运行时（与业务无关，可复用）
├─ databoard/      黑板数据 + 锁
├─ local/          本地 TTS 引擎（sherpa-onnx + Mock）
├─ upstream/       MiniMax WebSocket 接入（libwebsockets + 协议适配）
├─ heartbeat/      阶段3 心跳全流程
├─ observability/  事件监听（指标/日志/准入）
└─ entry/          阶段1 gRPC 入口 + JWT 鉴权
```

每个模块的**职责上限**（一个类只做一件事）：

| 类 | 职责 | 不做的事 |
|---|---|---|
| `RequestParser` | proto → TtsRequestMsg，强制 16k/mono | 不入队、不发请求 |
| `GrpcResponseWriter` | 内部消息 → proto | 不读黑板 |
| `JwtVerifier` | RS256 验签 | 不做业务 |
| `WsFrameCodec` | MiniMax JSON 编解码 + hex | 不懂业务 |
| `MiniMaxProtocolAdapter` | 原始事件 → 内部事件 | 不直接写黑板 |
| `UpstreamClient` | libwebsockets 主循环 | 不懂 MiniMax 协议 |
| `ILocalTtsEngine` | 本地 TTS 推理接口 | 不参与路由 |
| `SherpaOnnxLocalTts` | sherpa-onnx 推理封装 | 不做重采样 |
| `AudioResampler` | 采样率/声道变换 | 不做推理 |
| `SessionPool` | 会话增删查改 + LRU | 不做决策 |
| `PendingRequestStore` | 请求音频累积 | 不做决策 |
| `MessageProcessor` | 消息分发 + 会话分配 | 不产生副作用 |
| `DataEvolutionEngine` | 单层 ECA 评估 | 不级联 |
| `ChangeSetBuilder` | 副作用聚合 | 不执行 |
| `BackpressureController` | 队列深度评估 | 不改黑板 |
| `Heartbeat` | 阶段3 编排 | 不做 IO |
| `MetricsCollector` | 指标聚合（订阅事件） | 不做调度 |
| `EventLogger` | 事件日志（订阅事件） | 不改状态 |
| `AdmissionGate` | 请求准入（订阅事件） | 不拒绝已入队消息 |

---

## 3. 异步事件总线（监听者模式）

**原则**：任何子系统间的"通知式"通信都走 `EventBus`，禁止直接方法调用。

### 同步 vs 异步

```cpp
// 同步：仅心跳线程内（保持确定性）
bus_->publish_sync(HeartbeatTickEvent{us, batch_size, cs_size});

// 异步：跨线程 → MPSC 队列 → BusWorker 线程消费
bus_->publish_async(WsFrameSendFailedEvent{sid, err});
```

### 监听者的"回流"机制（关键）

监听者**禁止直接写 DataBoard**（跨线程写黑板违规）。正确做法：

```cpp
// 错误：监听者直接改黑板
bus_->subscribe<UpstreamDisconnectedEvent>([&](auto& e) {
    board_->session_pool()->mark_closed(e.sid);  // 跨线程写黑板！违规
});

// 正确：回投 Message，由心跳处理
bus_->subscribe<UpstreamDisconnectedEvent>([&](auto& e) {
    hb_queue_->send(Message{MarkSessionClosedMsg{e.sid}});  // 下一轮心跳处理
});
```

### 事件清单

| 事件 | 派发 | 监听者 |
|---|---|---|
| `HeartbeatTickEvent` | sync | MetricsCollector, AdmissionGate |
| `UpstreamConnectedEvent` | async | SessionPool(经回流), MetricsCollector, Logger |
| `UpstreamDisconnectedEvent` | async | SessionPool(经回流), MetricsCollector |
| `WsFrameDecodedEvent` | async | MiniMaxProtocolAdapter |
| `WsFrameSendFailedEvent` | async | SessionPool(经回流), Logger |
| `AudioChunkReadyEvent` | async | PendingStore(经回流), GrpcWriter(经回流) |
| `RequestFailedEvent` | async | PendingStore(经回流), Logger |
| `ShutdownRequestedEvent` | async | Reactor |

---

## 4. 数据黑板（DataBoard）

所有可能被多线程访问的数据放入 `DataBoard`，每个数据对象独立锁。

### 所有权策略

| 策略 | 适用 | 实现 |
|---|---|---|
| **线程所有权** | 仅单线程访问的私有数据 | `VoiceCatalog` (启动时加载，之后只读) |
| **黑板 + 细粒度锁** | 多线程读、心跳单线程写 | `SessionPool`, `PendingRequestStore` |
| **值语义消息队列** | 跨线程传递的临时数据 | `CopyChannel<Message>` |

### SessionPool LRU 状态机

```
Cold → Connecting → Ready → Streaming → Draining → Closed
                            ↓
                          Errored
```

- 状态迁移决策由 `DataEvolutionEngine` 在心跳内完成
- IO 线程通过 Message 上报 "WS closed / recv failed"，不直接改状态

---

## 5. 单层数据演进（ECA）

```cpp
EvolveOnce():
    changed = move(pending_changes_); pending_changes_.clear();
    
    // 评估每个变更，触发副作用
    for (s in changed.sessions): 评估 → 可能 close
    for (r in changed.requests): 评估 → 可能 emit
    
    // 本轮演进产生的新变更不会再次触发其他演进
    // 留待下一轮心跳的 EvolveOnce
```

**禁止连锁**：一次演进触发的业务逻辑所修改的数据，**不会被其他演进组件在同一轮感知**。保证每轮心跳复杂度可控。

---

## 6. 本地 TTS 引擎（sherpa-onnx）

### 集成方式

- **预编译包**：`deps/sherpa-onnx-v1.13.4/`（`libsherpa-onnx-c-api.so` + `libsherpa-onnx-cxx-api.so` + `libonnxruntime.so`）
- **C++ headers**：通过 `scripts/install_sherpa_onnx_headers.py` 从 GitHub 拉取（预编译包不含 headers）
- **模型**：`models/vits-piper-zh_CN-huayan-medium/`（`model.onnx` + `tokens.txt` + `espeak-ng-data/`）

### 调用流程

```
TtsRequestMsg(backend=Local)
   ↓ Heartbeat::handle_request → LocalSynthJob
   ↓ main.cpp heartbeat loop
   ↓ engine->Synthesize(text, voice_id, speed)
   ↓ sherpa_onnx::OfflineTts::GenerateWithConfig(text, config, nullptr, nullptr)
   ↓ 22050 Hz mono float PCM
   ↓ AudioResampler::to_16k_mono_int16(22050→16000, 1ch→1ch)
   ↓ LocalSynthCompleteMsg → request_channel
   ↓ Heartbeat::handle_local_complete → WriteToClient
   ↓ dispatch_to_writers → gRPC stream → 客户端
```

### 换模型

1. 下载其他 sherpa-onnx TTS 模型到 `models/<name>/`
2. `export TTS_LOCAL_MODEL_DIR=$PWD/models/<name>`
3. 重启服务

**推荐模型**（中文）：

| 模型 | 采样率 | 大小 | 备注 |
|---|---|---|---|
| vits-piper-zh_CN-huayan-medium | 22050 | 64MB | 默认，单说话人 |
| vits-melo-tts-zh_en | 44100 | 110MB | 中英混合 |
| vits-zh-aishell3 | 24000 | 100MB | 多说话人 |
| matcha-icefall-zh-baker | 24000 | 150MB | 长句更稳 |

---

## 7. 上游 MiniMax WebSocket

### 接入点

```
TtsRequestMsg(backend=Upstream)
   ↓ Heartbeat::handle_request
   ├─ acquire_or_create_session (LRU 池)
   ├─ encode_task_start (model, voice_setting, audio_setting)
   ├─ encode_task_continue (text)
   ↓ OutWsFrame → WsTaskScheduler → libwebsockets
   ↓ MiniMax 返回 task_continued (hex audio)
   ↓ WsFrameCodec::parse_server_frame → TtsResponseMsg
   ↓ Heartbeat::handle_response (AudioChunk) → PendingStore 累积
   ↓ is_final=true → emit_completed → WriteToClient
```

### 协议事件

- 发送：`task_start` / `task_continue` / `task_finish`
- 接收：`connected_success` / `task_started` / `task_continued` / `task_finished` / `task_failed`

### 错误码

| code | 含义 | 处理 |
|---|---|---|
| 0 | 成功 | 正常返回 |
| 1001 | 超时 | 重试 |
| 1002/1039 | 限流 | BackpressureController 触发 AdmissionGate 拒新请求 |
| 1004 | 鉴权失败 | 日志 + 返回错误给客户端 |
| 2201 | 超时断连 | 会话标记 Closed，回收 |
| 2203/2204 | 空文本/超长 | 参数校验失败 |

---

## 8. JWT RS256 鉴权

> 鉴权由 `GRPC_AUTH_ENABLED` 控制，**默认 `false`（关闭）**。关闭时服务端不加载公钥、不校验 token，适用于内网部署；设为 `true` 时必须配置 `GRPC_JWT_PUBLIC_KEY_FILE`，否则启动失败。

### 签发（测试）

```bash
bash scripts/gen_jwt_keypair.sh
# 生成 keys/jwt_private.pem + keys/jwt_public.pem
python3 keys/mint_token.py keys/jwt_private.pem   # 生成测试 token
```

### 验证（服务端）

- `JwtVerifier::from_rsa256_public_key_file(path)` 加载公钥
- `verify(token, issuer)`：签名验证 + exp + iss 校验
- 失败返回 `grpc::UNAUTHENTICATED`

### 客户端

```cpp
grpc::ClientContext ctx;
ctx.AddMetadata("authorization", "Bearer " + token);
```

---

## 9. 构建与测试

### 完整构建

```bash
bash scripts/install_deps.sh           # apt 依赖
bash scripts/install_sherpa_onnx.sh    # sherpa-onnx 预编译包 + headers
bash scripts/download_vits_piper_zh.sh # 模型
bash scripts/build.sh                  # cmake + make
```

### 单元测试

```bash
bash scripts/smoke_test.sh
# 11 个测试，全部走 Mock，无需联网
```

### 端到端测试（本地引擎）

```bash
export TTS_LOCAL_BACKEND=sherpa
export TTS_LOCAL_MODEL_DIR=$PWD/models/vits-piper-zh_CN-huayan-medium
export LD_LIBRARY_PATH=$PWD/deps/sherpa-onnx-v1.13.4/lib:$LD_LIBRARY_PATH

build/tts_server &
build/tests/grpc_smoke_test localhost:50061 keys/jwt_private.pem "你好世界" out.wav
```

默认鉴权关闭，无需 `GRPC_JWT_PUBLIC_KEY_FILE`；smoke 客户端发送的 JWT 会被忽略。若 `GRPC_AUTH_ENABLED=true`，需额外 `export GRPC_JWT_PUBLIC_KEY_FILE=$PWD/keys/jwt_public.pem`。

### 调试技巧

- **gRPC 日志**：`GRPC_VERBOSITY=DEBUG GRPC_TRACE=all ./build/tts_server`
- **心跳延迟**：看 `GetMetrics` 的 `heartbeat_p99_us`
- **慢任务**：stdout 中 `slow task id=T...` WARN
- **背压触发**：`admission: closing gate (changeset_size=N)` WARN

---

## 10. 扩展新后端

以接入一个新的本地引擎（例如 CosyVoice2 ONNX）为例：

### 步骤

1. **实现接口**

```cpp
class CosyVoice2LocalTts final : public ILocalTtsEngine {
public:
    explicit CosyVoice2LocalTts(LocalTtsConfig cfg);
    bool IsReady() const override { return ready_; }
    LocalTtsResult Synthesize(const std::string& text,
                              const std::string& voice_id,
                              float speed) override;
private:
    LocalTtsConfig cfg_;
    bool ready_{false};
};
```

2. **注册到工厂**（`src/local/local_tts_factory.cpp`）

```cpp
std::unique_ptr<ILocalTtsEngine> CreateLocalTts(const LocalTtsConfig& cfg) {
    if (cfg.backend_str == "cosyvoice2") {
        return std::make_unique<CosyVoice2LocalTts>(cfg);
    }
    ...
}
```

3. **配置切换**：`export TTS_LOCAL_BACKEND=cosyvoice2`

4. **音频重采样**：复用 `AudioResampler::to_16k_mono_int16`

### 注意

- 引擎**不直接写黑板**，只返回 `LocalTtsResult`
- 心跳负责把结果包成 `LocalSynthCompleteMsg` 投递
- 输出采样率若 ≠ 16000，必须经 `AudioResampler` 重采样

---

## 11. 已知限制与后续工作

- **SynthesizeClient (bidi)**：当前 proto 已声明，handler 已注册，但首个 TextChunk 的配置传递逻辑待完善
- **会话池与上游连接**：`ws_frames` 出口已留 `WsTaskScheduler::pump`，生产模式需将 libwebsockets 客户端实例注册到调度器
- **MOSS-TTS-Nano**：实时性更好但需自实现 LLM 自回归解码 + 音频编解码 pipeline，工作量大，暂以 sherpa-onnx 为主
- **流式合成**：本地引擎目前一次性生成；sherpa-onnx 支持 `GeneratedAudioCallback` 增量输出，可扩展为流式
