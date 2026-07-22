# API 使用文档

tts_server 提供 3 个 gRPC 方法：`SynthesizeStream`、`SynthesizeClient`、`GetMetrics`。

监听地址：默认 `0.0.0.0:50061`（可通过 `GRPC_BIND` 配置）。

鉴权：由 `GRPC_AUTH_ENABLED` 控制，**默认关闭**。开启（`GRPC_AUTH_ENABLED=true`）时，所有方法要求 HTTP header `authorization: Bearer <JWT>`，JWT 使用 RS256 签发；关闭时忽略该 header（内网模式）。

---

## 1. proto 契约

```proto
service TTS {
  rpc SynthesizeStream(SynthesizeRequest) returns (stream SynthesizeResponse);
  rpc SynthesizeClient(stream TextChunk) returns (stream SynthesizeResponse);
  rpc GetMetrics(google.protobuf.Empty) returns (MetricsSnapshot);
}
```

### SynthesizeRequest

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | string | ✅ | 待合成文本（中文/英文均可） |
| `model` | Model | | 模型枚举（上游模式生效；本地模式忽略） |
| `voice` | VoiceSetting | | 音色配置 |
| `audio` | AudioSetting | | 音频参数（强制 16kHz mono） |
| `pronunciation` | PronunciationDict | | 自定义读音（上游模式） |
| `timbre_weights` | repeated TimbreWeight | | 音色混合（上游模式） |
| `language_boost` | LanguageBoost | | 小语种增强（上游模式） |
| `voice_modify` | VoiceModify | | 声音效果器（上游模式） |
| `backend` | Backend | | `AUTO`/`LOCAL`/`UPSTREAM`，默认 `AUTO` |

### VoiceSetting

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `voice_id` | string | `"male-qn-qingse"` | 音色 ID。本地引擎：`"0"` = speaker 0 |
| `speed` | float | 1.0 | 语速 [0.5, 2.0] |
| `vol` | float | 1.0 | 音量 (0, 10] |
| `pitch` | int32 | 0 | 语调 [-12, 12] |
| `emotion` | Emotion | — | 情绪（上游模式） |
| `english_normalization` | bool | false | 英文规范化（上游模式） |

### AudioSetting

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `sample_rate` | int32 | 16000 | **限定 8000 或 16000**，其他值返回 `INVALID_ARGUMENT` |
| `bitrate` | int32 | 128000 | MP3 比特率（上游模式） |
| `format` | AudioFormat | PCM | **本地引擎仅支持 PCM**；上游支持 MP3/PCM/FLAC/WAV |
| `channel` | int32 | 1 | **限定 1（单声道）**，其他值返回 `INVALID_ARGUMENT` |

### Backend

| 值 | 说明 |
|---|---|
| `BACKEND_AUTO` | 本地优先；本地未就绪时降级上游（默认） |
| `BACKEND_LOCAL` | 强制本地；本地未就绪时报错 |
| `BACKEND_UPSTREAM` | 强制上游 MiniMax WebSocket |

### SynthesizeResponse

| 字段 | 类型 | 说明 |
|---|---|---|
| `audio` | bytes | 音频数据（PCM int16 小端，本地；或上游格式） |
| `meta` | AudioMeta | 元数据（sample_rate / channels / format / duration） |
| `is_final` | bool | 是否最后一块 |
| `cumulative_bytes` | int64 | 累计字节数 |
| `status_code` | int32 | 0=成功；其他=错误码 |
| `status_msg` | string | 错误描述 |
| `trace_id` | string | 链路 ID（上游模式） |

---

## 2. SynthesizeStream（服务端流式）

一次完整文本 → 服务端流式返回音频块。

### 调用流程

```
Client                                          Server
  │                                                │
  ├─ SynthesizeRequest (text, backend=LOCAL) ─────►│
  │                                                │ JWT 验证 (RS256)
  │                                                │ parse → TtsRequestMsg
  │                                                │ → request_channel
  │                                                │
  │                                                │ Heartbeat::tick()
  │                                                │  ├─ handle_request
  │                                                │  │   └─ backend=Local
  │                                                │  │       → LocalSynthJob
  │                                                │  ├─ LocalSynthJob → CPU pool
  │                                                │  │   → sherpa_onnx::OfflineTts
  │                                                │  │   ::GenerateWithConfig(text)
  │                                                │  ├─ AudioResampler 22050→16000
  │                                                │  ├─ LocalSynthCompleteMsg
  │                                                │  ├─ handle_local_complete
  │                                                │  └─ WriteToClient → local_queue
  │                                                │
  │◄──────── SynthesizeResponse (audio, is_final) ─┤ dispatch_to_writers
  │                                                │   → writer->Write(out)
```

### C++ 客户端示例

仓库已内置可执行文件 `build/tests/grpc_smoke_test`：

```bash
build/tests/grpc_smoke_test <target> <privkey.pem> <text> [output.wav]
```

**中文合成并保存 WAV**：

```bash
build/tests/grpc_smoke_test localhost:50061 keys/jwt_private.pem "你好，世界" hello.wav
```

输出：

```
=== Chinese end-to-end smoke test ===
text: 你好，世界
chunks: 1
total_bytes: 46996
sample_rate: 16000
channels: 1
status_code: 0
status_msg: ok_local
rpc_ok: yes
PASS
wav: hello.wav (46996 bytes, 1s)
```

**强制上游 MiniMax**：

```cpp
req.set_backend(::tts::BACKEND_UPSTREAM);
```

**手动 C++ 代码（参考 `tests/test_grpc_synthesize.cpp`）**：

```cpp
#include <grpcpp/grpcpp.h>
#include "pb2/tts.grpc.pb.h"

auto stub = ::tts::TTS::NewStub(
    grpc::CreateChannel("localhost:50061", grpc::InsecureChannelCredentials()));

::tts::SynthesizeRequest req;
req.set_text("你好，世界");
req.set_model(::tts::SPEECH_2_8_TURBO);
auto* voice = req.mutable_voice();
voice->set_voice_id("0");           // 本地 speaker 0
voice->set_speed(1.0f);
auto* audio = req.mutable_audio();
audio->set_sample_rate(16000);
audio->set_bitrate(128000);
audio->set_format(::tts::PCM);
audio->set_channel(1);
req.set_backend(::tts::BACKEND_LOCAL);

grpc::ClientContext ctx;
ctx.AddMetadata("authorization", "Bearer " + jwt_token);

auto reader = stub->SynthesizeStream(&ctx, req);

std::vector<std::uint8_t> pcm;
::tts::SynthesizeResponse resp;
while (reader->Read(&resp)) {
    pcm.insert(pcm.end(), resp.audio().begin(), resp.audio().end());
    if (resp.is_final()) break;
}
auto status = reader->Finish();
// pcm 即为 16 kHz mono int16 PCM 数据
```

---

## 3. SynthesizeClient（双向流式）

客户端流式发送多个 `TextChunk`，服务端流式返回音频块。

### TextChunk

```proto
message TextChunk {
  oneof body {
    string text = 1;
    FinishMark finish = 2;
  }
}
```

### 调用流程

```
Client                                          Server
  │                                                │
  ├─ TextChunk(text="你好") ─────────────────────►│
  │                                                │ → request_channel
  │                                                │ Heartbeat → LocalSynthJob
  │◄──────── SynthesizeResponse (audio chunk) ────┤
  ├─ TextChunk(text="，世界") ───────────────────►│
  │◄──────── SynthesizeResponse (audio chunk) ────┤
  ├─ TextChunk(finish) ───────────────────────────►│
  │◄──────── SynthesizeResponse (is_final) ────────┤
```

### C++ 客户端示例（参考实现）

```cpp
auto stub = ::tts::TTS::NewStub(channel);
grpc::ClientContext ctx;
ctx.AddMetadata("authorization", "Bearer " + jwt_token);

auto stream = stub->SynthesizeClient(&ctx);

// 发送文本片段
for (const auto& part : {"你好", "，", "世界"}) {
    ::tts::TextChunk chunk;
    chunk.set_text(part);
    stream->Write(chunk);
}
::tts::TextChunk finish;
finish.mutable_finish();
stream->Write(finish);
stream->WritesDone();

// 接收音频
::tts::SynthesizeResponse resp;
while (stream->Read(&resp)) {
    // 处理 resp.audio()
}
auto status = stream->Finish();
```

> ⚠️ 当前 SynthesizeClient 首个 chunk 的配置传递逻辑待完善，建议优先使用 SynthesizeStream。

---

## 4. GetMetrics（指标查询）

无参数查询，返回 `MetricsSnapshot`：

| 字段 | 说明 |
|---|---|
| `inflight_requests` | 当前在途请求数 |
| `queued_messages` | 心跳队列积压 |
| `heartbeat_p99_us` | 心跳处理 p99 延迟（微秒） |
| `upstream_connected` | 上游累计连接数 |
| `upstream_disconnected_total` | 上游累计断连数 |
| `audio_bytes_emitted_total` | 累计音频字节输出 |
| `failed_requests_total` | 累计失败请求数 |
| `pool_idle_sessions` | LRU 池中空闲会话数 |
| `pool_active_sessions` | LRU 池中活跃会话数 |
| `backpressure_level` | 当前背压等级（0/1/2） |
| `heartbeat_count` | 心跳总次数 |
| `bus_worker_pending` | EventBus 队列积压 |

### C++ 客户端示例（参考 `tests/test_grpc_ping.cpp`）

```cpp
auto stub = ::tts::TTS::NewStub(channel);
grpc::ClientContext ctx;
ctx.AddMetadata("authorization", "Bearer " + jwt_token);
::google::protobuf::Empty req;
::tts::MetricsSnapshot resp;
auto status = stub->GetMetrics(&ctx, req, &resp);
// resp.inflight_requests(), resp.heartbeat_p99_us(), ...
```

---

## 5. JWT 签发

> 仅在 `GRPC_AUTH_ENABLED=true` 时需要。默认鉴权关闭，本节可跳过。

### 生成密钥对

```bash
bash scripts/gen_jwt_keypair.sh
# 生成 keys/jwt_private.pem（私钥，仅本地）和 keys/jwt_public.pem（公钥，服务端加载）
```

### 签发测试 token

```bash
python3 keys/mint_token.py keys/jwt_private.pem
# 输出：eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...
```

### 服务端配置

```bash
export GRPC_AUTH_ENABLED=true
export GRPC_JWT_PUBLIC_KEY_FILE=$PWD/keys/jwt_public.pem
export GRPC_JWT_ISSUER=tts-service
```

### 客户端使用

```cpp
ctx.AddMetadata("authorization", "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...");
```

---

## 6. 错误码

| gRPC code | 场景 |
|---|---|
| `UNAUTHENTICATED` | JWT 缺失/签名错误/过期/iss 不匹配（仅 `GRPC_AUTH_ENABLED=true` 时） |
| `INVALID_ARGUMENT` | 参数越界（sample_rate ≠ 8000/16000，channel ≠ 1，format 非 PCM/MP3） |
| `UNAVAILABLE` | 本地引擎未就绪且 backend=LOCAL；或上游连接失败 |
| `RESOURCE_EXHAUSTED` | 背压触发（AdmissionGate 拒绝新请求） |
| `INTERNAL` | 引擎推理失败（status_code=1000） |

引擎层错误码（`SynthesizeResponse.status_code`）：

| code | 含义 |
|---|---|
| 0 | 成功 |
| 1000 | 通用推理失败 |
| 1004 | 引擎未就绪 |
| 1001 | 上游超时 |
| 1002/1039 | 上游限流 |
| 2203 | 空文本 |
| 2204 | 超长文本 |

---

## 7. 完整调用案例

### 案例 1：中文短句合成 + 保存 WAV

```bash
# 启动服务（默认鉴权关闭，无需 JWT 配置）
export TTS_LOCAL_BACKEND=sherpa
export TTS_LOCAL_MODEL_DIR=$PWD/models/vits-piper-zh_CN-huayan-medium
export LD_LIBRARY_PATH=$PWD/deps/sherpa-onnx-v1.13.4/lib:$LD_LIBRARY_PATH
build/tts_server &

# 客户端（鉴权关闭时 token 被忽略，参数仍需占位）
build/tests/grpc_smoke_test localhost:50061 keys/jwt_private.pem "真正的中文tts" out.wav

# 验证
file out.wav
# RIFF (little-endian) data, WAVE audio, Microsoft PCM, 16 bit, mono 16000 Hz
```

### 案例 2：强制上游 MiniMax

```cpp
::tts::SynthesizeRequest req;
req.set_text("你好，世界");
req.set_backend(::tts::BACKEND_UPSTREAM);
auto* voice = req.mutable_voice();
voice->set_voice_id("male-qn-qingse");
auto* audio = req.mutable_audio();
audio->set_sample_rate(16000);
audio->set_format(::tts::MP3);
audio->set_channel(1);
```

需先 `export MINIMAX_API_KEY=sk-... && export TTS_LOCAL_ENABLED=false`。

### 案例 3：调整语速与音色

```cpp
auto* voice = req.mutable_voice();
voice->set_voice_id("0");      // 本地 speaker 0
voice->set_speed(1.5f);        // 1.5 倍语速
voice->set_vol(1.2f);          // 音量提升 20%
voice->set_pitch(2);           // 语调 +2（上游模式生效）
```

### 案例 4：监控指标

```bash
# 服务运行时，另一个终端
build/tests/grpc_ping_test localhost:50061
# connecting to localhost:50061
# status=0 inflight=3
```

---

## 8. grpcurl 调试（可选）

如果安装了 grpcurl：

```bash
TOKEN=$(python3 keys/mint_token.py keys/jwt_private.pem)

grpcurl -plaintext \
  -H "authorization: Bearer $TOKEN" \
  -d '{
    "text": "你好，世界",
    "backend": "BACKEND_LOCAL",
    "voice": {"voice_id": "0", "speed": 1.0},
    "audio": {"sample_rate": 16000, "format": "PCM", "channel": 1}
  }' \
  localhost:50061 tts.TTS/SynthesizeStream
```

> 注：grpcurl 会把响应音频 bytes 输出为 base64，需自行解码保存。
