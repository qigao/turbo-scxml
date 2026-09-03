# CHTTP BasicHTTP Event I/O Processor

`TurboSCXML::CHttpEventIO` 是可选的 C11 组件，实现 SCXML 1.0 Appendix C.2
BasicHTTP Event I/O Processor。它以 CHTTP 提供真实 HTTP/1 POST ingress 和
egress，同时把标准 SCXML Event Processor 委托给宿主 adapter；
`TurboSCXML::SCXML` 本身不引入 CHTTP 依赖。

## 构建与消费

使用 `win-dev-chttp-user` 或 `win-release-chttp-user` preset，或显式设置：

```cmake
set(TURBOSCXML_ENABLE_CHTTP_EVENT_IO ON)
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS CHttpEventIO)
target_link_libraries(app PRIVATE TurboSCXML::CHttpEventIO)
```

匹配 profile 的 `SALTS_ROOT` 必须提供 `Salts::CHTTP`。功能默认关闭；关闭后
不会安装该 header/target，也不会改变 `TurboSCXML::SCXML` 的 link interface。

## 生命周期

严格按以下顺序管理所有权：

```text
processor_init -> processor_start -> binding_init
-> install descriptor + binding adapter in session config
-> session_init -> binding_activate
-> session_destroy -> binding_destroy
-> processor_stop -> processor_destroy
```

关键配置如下：

```c
scxml_chttp_processor processor = {0};
scxml_chttp_binding binding = {0};
scxml_ioprocessor_descriptor descriptor = {0};

scxml_chttp_processor_config_v1 processor_config = {
    .abi_version = SCXML_CHTTP_ABI_V1,
    .struct_size = sizeof(processor_config),
    .server = server_config,       /* finite capacities, positive deadlines */
    .client = client_config,       /* finite capacities, positive deadlines */
    .advertised_authority = "127.0.0.1",
    .base_path = "/scxml",
    .endpoint_capacity = 16,
    .egress_capacity = 64,
    .max_access_uri_bytes = 256,
    .max_event_name_bytes = 128,
    .max_form_entry_count = 32,
    .max_form_name_bytes = 128,
    .max_form_value_bytes = 1024,
    .max_encoded_body_bytes = 16384,
    .request_timeout_ms = 2000,
    .worker_poll_ms = 5,
    .resolve = resolve_authorized_target,
    .resolve_user = &allowlist
};

scxml_chttp_binding_config_v1 binding_config = {
    .abi_version = SCXML_CHTTP_ABI_V1,
    .struct_size = sizeof(binding_config),
    .scxml_adapter = host_scxml_adapter,
    .scxml_adapter_user = host_scxml_user,
    .decode = decode_ingress,
    .decode_user = decoder_state
};

if (scxml_chttp_processor_init(&processor, &processor_config) != SALTS_OK ||
    scxml_chttp_processor_start(&processor) != SALTS_OK ||
    scxml_chttp_binding_init(&binding, &processor, &binding_config) != SALTS_OK ||
    !scxml_chttp_binding_ioprocessor(&binding, &descriptor)) {
    /* unwind initialized owners in reverse order */
}

scxml_session_config session_config = {
    .program = program,
    .executor = executor,
    .external_event_capacity = 64,
    .internal_event_capacity = 32,
    .completion_capacity = 16,
    .microstep_limit = 1024,
    .max_storage_bytes = 65536,
    .effect_capacity = 64,
    .adapter_internal_event_capacity = 32,
    .delayed_send_capacity = 32,
    .event_io = scxml_chttp_binding_event_io_adapter(&binding),
    .adapter_user = scxml_chttp_binding_adapter_user(&binding),
    .ioprocessors = &descriptor,
    .ioprocessor_count = 1
};

/* Initialize the session, then activate the reserved binding endpoint. */
if (scxml_session_init(session, &session_config) !=
        CFLOW_STATECHART_INSTANCE_OK ||
    scxml_chttp_binding_activate(&binding, session, program) != SALTS_OK) {
    /* close/destroy in reverse order */
}
```

示例中的 `server_config` 与 `client_config` 必须为所有连接、命令、请求、header、
body 和 route 设置有限正容量，并设置正数 connect/read/write deadline。完整、持续
编译的 C/C++ public ABI 消费者位于 `tests/install_consumer/event_io_main.*`；真实
生命周期和 loopback 示例位于 `tests/scxml_chttp_lifecycle_test.c`、
`tests/scxml_chttp_egress_test.c` 与 `tests/scxml_chttp_ingress_test.c`。

## Resolver 与 transport 安全边界

resolver 是授权边界，不是通用 URL parser。应默认拒绝，仅把预先批准的逻辑 URI
映射到 CHTTP 的 `connection_uri`、HTTP `authority` 和 origin-form `target`：

```c
static int resolve_authorized_target(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out) {
    const struct allowed_target *allowed = user;
    if (allowed == NULL || uri == NULL || out == NULL ||
        uri_size != allowed->logical_uri_size ||
        memcmp(uri, allowed->logical_uri, uri_size) != 0)
        return SALTS_EPERM;
    *out = (scxml_chttp_resolved_target){
        allowed->connection_uri, allowed->authority, allowed->target};
    return SALTS_OK;
}
```

当前组件仅支持 CHTTP 提供的明文 HTTP/1 transport。不要接受 `https:`，不要把它
改写为 `tcp://`，不要重定向、重试或自动扩大 allowlist。TLS 必须由能验证 HTTPS
的独立 transport/adapter 提供。

## Decoder、背压与错误

decoder 接收的 request/form view 仅在 callback 内有效。它可以返回 callback-scope
的 `scxml_content_view`；session admission 会在 callback 返回前按 schema、copy trait
和容量把数据复制到 session-owned storage。组件不释放 decoder object，也不会保存
borrowed view。

入站结果：成功复制并进入 external queue 后返回 204；malformed form 为 400，未知
endpoint 为 404，closing/inactive binding 为 410，不支持 media type 为 415，decoder
或 Event-data 验证失败为 422，external mailbox 满为 503，内部不变量失败为 500。
503 是正常的有界背压，调用方可以稍后按自己的策略重试；组件本身不会自动重试。

出站 prepare 满返回 `SCXML_ADAPTER_FULL`。目标缺失、未授权、网络失败和非 2xx 响应
通过现有 adapter 语义产生 `error.communication`；无效 payload/执行失败产生
`error.execution`。ticket commit/discard 只发布或撤销已复制的固定 row，不执行网络
I/O、分配或等待。

销毁 session 会先 close composite adapter，并等待 binding 的 callback/outbound
引用归零。只有 `scxml_session_destroy()` 成功后才能 destroy binding；所有 binding
销毁后再 stop processor。`processor_stop(timeout_ms)` 的所有阶段共享一个总
deadline：`SALTS_ETIMEDOUT` 表示停止尚未完成，必须保留 handle 并重试。

停止期间发现的 transport/server terminal error 会被保留；即使所有清理已完成、
processor 已进入 STOPPED，`processor_stop()` 仍返回该错误。此时调用方应保留该错误
用于诊断，并尝试 `processor_destroy()`；destroy 成功即表示 owner 已释放。若 destroy
返回 `SALTS_EBUSY`，handle 仍有效，调用方须先消除 live binding/未完成停止条件，再次
调用 stop，而不能泄漏或清零 opaque handle。
