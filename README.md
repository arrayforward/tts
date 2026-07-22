# tts_server

基于 **反应式消息驱动框架**（四阶段 SEDA + 数据黑板 + CSP 通道 + 异步事件总线）构建的 gRPC 文本转语音服务。

默认使用 **sherpa-onnx + vits-piper-zh_CN** 本地模型做中文 TTS（纯 CPU 推理，无需联网），可选接入 MiniMax `wss://api.minimaxi.com/ws/v1/t2a_v2` 上游 WebSocket。

---

## 特性

- **本地优先**：默认 `vits-piper-zh_CN-huayan-medium`（sherpa-onnx v1.13.4），无需 GPU、无需联网
- **上游可选**：通过 `TTS_LOCAL_ENABLED=false` 回退到 MiniMax 上游 WebSocket
- **每请求 backend 选择**：`SynthesizeRequest.backend ∈ {AUTO, LOCAL, UPSTREAM}`，客户端可逐请求切换
- **统一音频输出**：16 kHz 单声道 PCM int16（本地引擎自带 22050→16000 重采样）
- **gRPC 接口**：`SynthesizeStream`（server streaming）、`SynthesizeClient`（bidi）、`GetMetrics`
- **JWT RS256 鉴权**：OpenSSL 直接验签，无外部 JWT 库依赖
- **会话池 LRU**：max=16，idle_timeout=60s
- **结构化日志**：spdlog JSON 输出 + 慢任务 WARN（CPU>10ms / IO>1s）
- **可测试性注入**：IClock / IWsTransport / ILocalTtsEngine / Mock upstream，11 个单元测试全覆盖

---

## 架构

```
┌──────────── gRPC IO 线程池 ────────────┐
│ GrpcServiceImpl → RequestParser        │
│   → TtsRequestMsg → RequestQueue       │
│ GrpcResponseWriter ← WriteToClient     │
│ JwtAuthInterceptor (RS256)             │
└──────────────┬─────────────────────────┘
               │ CopyChannel (深拷贝)
               ▼
┌────────── 心跳线程 (单线程, 阶段3) ──────────┐
│ Heartbeat::tick()                            │
│   1. SwapOutAll (RequestQueue ∪ WsEventQueue)│
│   2. ProcessBatch                            │
│      ├─ backend=Local  → LocalSynthJob      │
│      └─ backend=Upstream → OutWsFrame       │
│   3. DataEvolutionEngine::EvolveOnce (单层)  │
│   4. ChangeSetBuilder.collect()              │
│   5. ExecuteChangeSet                        │
└──────────────┬───────────────────────────────┘
               │
       ┌───────┴────────┐
       ▼                ▼
┌─ 本地引擎 ─┐    ┌─ 上游 WS (libwebsockets) ─┐
│ sherpa-onnx│    │ UpstreamClient             │
│ OfflineTts │    │ WsFrameCodec               │
│ (CPU 池)   │    │ MiniMaxProtocolAdapter     │
└────────────┘    └────────────────────────────┘
```

**核心约束**：
- DataBoard 仅心跳线程单写
- 跨线程一律深拷贝（CopyChannel）
- 监听者禁止直接写黑板，状态变更回投 Message
- 单层数据演进，禁连锁
- 真实时钟一律 IClock 注入

详见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

---

## 快速开始

### 1. 环境（WSL Ubuntu 22.04）

```bash
bash scripts/install_deps.sh          # g++/cmake/grpc++/libwebsockets/spdlog/gtest
bash scripts/install_sherpa_onnx.sh   # sherpa-onnx v1.13.4 预编译包 + C++ headers
bash scripts/download_vits_piper_zh.sh # vits-piper-zh_CN-huayan-medium 模型 (~64MB)
bash scripts/gen_jwt_keypair.sh        # RS256 JWT 测试密钥
```

### 2. 构建

```bash
bash scripts/build.sh
bash scripts/smoke_test.sh   # 11 个单元测试
```

### 3. 启动

```bash
export GRPC_JWT_PUBLIC_KEY_FILE=$PWD/keys/jwt_public.pem
export TTS_LOCAL_ENABLED=true
export TTS_LOCAL_BACKEND=sherpa
export TTS_LOCAL_MODEL_DIR=$PWD/models/vits-piper-zh_CN-huayan-medium
export LD_LIBRARY_PATH=$PWD/deps/sherpa-onnx-v1.13.4/lib:$LD_LIBRARY_PATH
build/tts_server
```

### 4. 调用（C++ 客户端示例）

```bash
build/tests/grpc_smoke_test localhost:50061 keys/jwt_private.pem "你好，世界" output.wav
```

详见 [docs/API.md](docs/API.md)。

---

## 配置（环境变量）

| 变量 | 必填 | 默认 | 说明 |
|---|---|---|---|
| `GRPC_JWT_PUBLIC_KEY_FILE` | ✅ | — | RS256 公钥 PEM 路径 |
| `TTS_LOCAL_MODEL_DIR` | 本地开启时 | — | sherpa-onnx 模型目录 |
| `TTS_LOCAL_ENABLED` | | `true` | `false` → 走 MiniMax 上游 |
| `TTS_LOCAL_PREFER` | | `true` | backend=AUTO 时的默认路由 |
| `TTS_LOCAL_BACKEND` | | `sherpa` | `sherpa` 或 `mock` |
| `TTS_LOCAL_NUM_THREADS` | | `cores/2` | ONNX Runtime 推理线程数 |
| `MINIMAX_API_KEY` | 上游模式 | — | MiniMax API key（**绝不入 git**） |
| `MINIMAX_WS_URL` | | `wss://api.minimaxi.com/ws/v1/t2a_v2` | 上游 WS 地址 |
| `GRPC_BIND` | | `0.0.0.0:50061` | gRPC 监听 |
| `GRPC_JWT_ISSUER` | | `tts-service` | JWT 期望 iss |
| `TTS_POOL_MAX` | | 16 | LRU 会话池上限 |
| `TTS_POOL_IDLE_MS` | | 60000 | 会话空闲回收阈值 |
| `TTS_POOL_LIFETIME_S` | | 3600 | 会话最大寿命 |
| `TTS_HEARTBEAT_MS` | | 50 | 心跳间隔 |
| `TTS_CPU_WORKERS` / `TTS_IO_WORKERS` | | cores×1.5 / cores×2 | CPU / IO 线程池 |
| `TTS_BUS_WORKERS` | | 1 | EventBus worker 线程 |

---

## 目录结构

```
tts/
├─ proto/tts.proto               # gRPC 契约
├─ src/
│  ├─ main.cpp                   # 装配入口
│  ├─ config.{h,cpp}             # env 配置
│  ├─ framework/                 # 时钟/CopyChannel/EventBus/Task/Timer/Reactor
│  ├─ databoard/                 # DataBoard + SessionPool(LRU) + PendingStore + Metrics
│  ├─ local/                     # sherpa-onnx 引擎 + AudioResampler + Mock
│  ├─ upstream/                  # libwebsockets + MiniMax 协议适配器
│  ├─ heartbeat/                 # Heartbeat + Processor + Evolution + Backpressure
│  ├─ observability/             # MetricsCollector + EventLogger + AdmissionGate
│  └─ entry/                     # gRPC Service + JWT 拦截器 + 协议编解码
├─ tests/                        # 11 个 GoogleTest + C++ gRPC smoke 客户端
├─ scripts/                      # WSL 安装/构建/运行/JWT/模型下载
├─ docs/
│  ├─ DEVELOPMENT.md             # 开发指南（架构、扩展、调试）
│  └─ API.md                     # API 使用文档 + 调用案例
└─ README.md
```

---

## 测试

```bash
bash scripts/smoke_test.sh
```

覆盖：EventBus 同步/异步、SessionPool LRU、单层演进、心跳本地路由、心跳 E2E、AudioResampler、MockLocalTTS、JWT RS256、协议 codec、CopyChannel、RequestParser。

---

## 安全

- API Key 绝不硬编码、不入 git、不写到 `.env.example`
- JWT 密钥仅通过环境变量注入，`keys/` 已加入 `.gitignore`
- 若 MiniMax API Key 泄漏，立即在 MiniMax 控制台轮换

---

## 文档

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — 开发指南
- [docs/API.md](docs/API.md) — API 使用文档与调用案例
