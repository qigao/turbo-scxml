#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
/* Windows RPC/COM can define interface as a macro; CMeta has a descriptor
   field with that exact identifier. Do not rewrite CMeta's public C ABI. */
#ifdef interface
#undef interface
#endif
#endif

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/native_io.h>
#include <salts/native_io_ace_token.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
typedef SOCKET native_race_socket;
#define NATIVE_RACE_INVALID_SOCKET INVALID_SOCKET
#else
typedef int native_race_socket;
#define NATIVE_RACE_INVALID_SOCKET (-1)
#endif

/* One test-owned pair; the NativeIO backend remains the sole progress owner. */
static int native_race_open_pair(native_race_socket sockets[2]) {
#if defined(_WIN32)
    WSADATA wsa = {0};
    SOCKET listener = INVALID_SOCKET;
    struct sockaddr_in address = {0};
    int address_length = (int)sizeof(address);
    u_long nonblocking = 1u;
    sockets[0] = INVALID_SOCKET;
    sockets[1] = INVALID_SOCKET;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    listener = WSASocketW(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u, WSA_FLAG_OVERLAPPED);
    if (listener == INVALID_SOCKET) goto failure;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &address_length) != 0)
        goto failure;
    sockets[1] = WSASocketW(
        AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u, WSA_FLAG_OVERLAPPED);
    if (sockets[1] == INVALID_SOCKET ||
        connect(sockets[1], (const struct sockaddr *)&address,
                (int)sizeof(address)) != 0)
        goto failure;
    sockets[0] = accept(listener, NULL, NULL);
    if (sockets[0] == INVALID_SOCKET ||
        ioctlsocket(sockets[0], FIONBIO, &nonblocking) != 0 ||
        ioctlsocket(sockets[1], FIONBIO, &nonblocking) != 0)
        goto failure;
    (void)closesocket(listener);
    return 0;
failure:
    if (listener != INVALID_SOCKET) (void)closesocket(listener);
    if (sockets[0] != INVALID_SOCKET) (void)closesocket(sockets[0]);
    if (sockets[1] != INVALID_SOCKET) (void)closesocket(sockets[1]);
    sockets[0] = INVALID_SOCKET;
    sockets[1] = INVALID_SOCKET;
    (void)WSACleanup();
    return -1;
#else
    int flags;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return -1;
    for (size_t index = 0u; index < 2u; ++index) {
        flags = fcntl(sockets[index], F_GETFL, 0);
        if (flags < 0 ||
            fcntl(sockets[index], F_SETFL, flags | O_NONBLOCK) != 0) {
            (void)close(sockets[0]);
            (void)close(sockets[1]);
            sockets[0] = -1;
            sockets[1] = -1;
            return -1;
        }
    }
    return 0;
#endif
}

static int native_race_close_socket(native_race_socket value) {
#if defined(_WIN32)
    return closesocket(value);
#else
    return close(value);
#endif
}


/* One real OS-connected UDP pair. Use overlapped WinSock handles for IOCP
 * and nonblocking descriptors for epoll/Kqueue. No second I/O progress owner. */
static int native_race_open_udp_pair(native_race_socket sockets[2]) {
    struct sockaddr_in address = {0};
    native_race_socket tx = NATIVE_RACE_INVALID_SOCKET;
    native_race_socket rx = NATIVE_RACE_INVALID_SOCKET;
    int failure_stage = -1;
#if defined(_WIN32)
    WSADATA wsa = {0};
    int address_length = (int)sizeof(address);
    u_long nonblocking = 1u;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    rx = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0u,
                    WSA_FLAG_OVERLAPPED);
    tx = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0u,
                    WSA_FLAG_OVERLAPPED);
    if (rx == INVALID_SOCKET || tx == INVALID_SOCKET) goto failed;
#else
    socklen_t address_length = (socklen_t)sizeof(address);
    int flags;
    rx = socket(AF_INET, SOCK_DGRAM, 0);
    tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) goto failed;
#endif
    address.sin_family = AF_INET;
    /* sockaddr stores network-order octets; express the loopback address
       explicitly rather than combining an SDK macro with host byte order. */
    {
        const unsigned char loopback[] = {127u, 0u, 0u, 1u};
        memcpy(&address.sin_addr, loopback, sizeof(loopback));
    }
    address.sin_port = 0;
    failure_stage = -2; /* bind local UDP receiver */
    if (bind(rx, (const struct sockaddr *)&address, address_length) != 0)
        goto failed;
    failure_stage = -3; /* query bound ephemeral port */
    if (getsockname(rx, (struct sockaddr *)&address, &address_length) != 0)
        goto failed;
    failure_stage = -4; /* connect UDP sender to actual bound peer */
    if (connect(tx, (const struct sockaddr *)&address, address_length) != 0)
        goto failed;
    failure_stage = -5; /* make both native sockets nonblocking */
#if defined(_WIN32)
    if (ioctlsocket(tx, FIONBIO, &nonblocking) != 0 ||
        ioctlsocket(rx, FIONBIO, &nonblocking) != 0) goto failed;
#else
    flags = fcntl(tx, F_GETFL, 0);
    if (flags < 0 || fcntl(tx, F_SETFL, flags | O_NONBLOCK) != 0)
        goto failed;
    flags = fcntl(rx, F_GETFL, 0);
    if (flags < 0 || fcntl(rx, F_SETFL, flags | O_NONBLOCK) != 0)
        goto failed;
#endif
    sockets[0] = tx;
    sockets[1] = rx;
    return 0;
failed:
    /* Print only sanitized fixture syscall evidence, never socket content.
       The test's failure-stage code stays stable across OS backends. */
#if defined(_WIN32)
    fprintf(stderr, "UDP fixture stage=%d wsa_error=%d\n",
            failure_stage, WSAGetLastError());
#else
    fprintf(stderr, "UDP fixture stage=%d errno=%d\n",
            failure_stage, errno);
#endif
    if (tx != NATIVE_RACE_INVALID_SOCKET)
        (void)native_race_close_socket(tx);
    if (rx != NATIVE_RACE_INVALID_SOCKET)
        (void)native_race_close_socket(rx);
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    sockets[0] = NATIVE_RACE_INVALID_SOCKET;
    sockets[1] = NATIVE_RACE_INVALID_SOCKET;
    return failure_stage;
}

/* Real loopback listener and two initially UNCONNECTED nonblocking clients.
 * Connect progress is performed only by the single NativeIO backend. */
static int native_race_open_connect_fixture(
    native_race_socket *listener, native_race_socket clients[2],
    struct sockaddr_in *remote) {
    native_race_socket server = NATIVE_RACE_INVALID_SOCKET;
    int error = -1;
#if defined(_WIN32)
    WSADATA wsa = {0};
    int address_length = (int)sizeof(*remote);
    u_long nonblocking = 1u;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u,
                        WSA_FLAG_OVERLAPPED);
#else
    socklen_t address_length = (socklen_t)sizeof(*remote);
    server = socket(AF_INET, SOCK_STREAM, 0);
#endif
    *listener = NATIVE_RACE_INVALID_SOCKET;
    clients[0] = NATIVE_RACE_INVALID_SOCKET;
    clients[1] = NATIVE_RACE_INVALID_SOCKET;
    if (server == NATIVE_RACE_INVALID_SOCKET) goto failed;
    memset(remote, 0, sizeof(*remote));
    remote->sin_family = AF_INET;
    {
        const unsigned char loopback[4] = {127u, 0u, 0u, 1u};
        memcpy(&remote->sin_addr, loopback, sizeof(loopback));
    }
    remote->sin_port = 0;
    if (bind(server, (const struct sockaddr *)remote, address_length) != 0 ||
        getsockname(server, (struct sockaddr *)remote, &address_length) != 0 ||
        listen(server, 4) != 0) goto failed;
    for (size_t i = 0u; i < 2u; ++i) {
#if defined(_WIN32)
        clients[i] = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u,
                                WSA_FLAG_OVERLAPPED);
        if (clients[i] == INVALID_SOCKET ||
            ioctlsocket(clients[i], FIONBIO, &nonblocking) != 0) goto failed;
#else
        int flags;
        clients[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (clients[i] < 0) goto failed;
        flags = fcntl(clients[i], F_GETFL, 0);
        if (flags < 0 ||
            fcntl(clients[i], F_SETFL, flags | O_NONBLOCK) != 0) goto failed;
#endif
    }
    *listener = server;
    return 0;
failed:
    if (server != NATIVE_RACE_INVALID_SOCKET)
        (void)native_race_close_socket(server);
    for (size_t i = 0u; i < 2u; ++i)
        if (clients[i] != NATIVE_RACE_INVALID_SOCKET)
            (void)native_race_close_socket(clients[i]);
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    return error;
}

/* Test-only real byte pipe: Windows uses overlapped named-pipe handles,
 * POSIX uses two nonblocking file descriptors. This code never owns or
 * advances a second NativeIO request/terminal engine. */
#if defined(_WIN32)
typedef HANDLE native_race_pipe;
#define NATIVE_RACE_INVALID_PIPE INVALID_HANDLE_VALUE
#else
typedef int native_race_pipe;
#define NATIVE_RACE_INVALID_PIPE (-1)
#endif

static int native_race_close_pipe(native_race_pipe handle) {
#if defined(_WIN32)
    return CloseHandle(handle) ? 0 : -1;
#else
    return close(handle);
#endif
}

static int native_race_open_pipe_pair(native_race_pipe pair[2]) {
    pair[0] = NATIVE_RACE_INVALID_PIPE;
    pair[1] = NATIVE_RACE_INVALID_PIPE;
#if defined(_WIN32)
    static LONG sequence = 0;
    char name[128];
    OVERLAPPED connect_overlapped = {0};
    HANDLE event = NULL;
    BOOL pending = FALSE;
    int written = snprintf(
        name, sizeof(name), "\\\\.\\pipe\\turbo-scxml-native-act-%lu-%ld",
        GetCurrentProcessId(), InterlockedIncrement(&sequence));
    if (written < 0 || (size_t)written >= sizeof(name)) return -1;
    /* The server's outbound pipe is [1]; the client read end is [0]. */
    pair[1] = CreateNamedPipeA(
        name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
        4096u, 4096u, 0u, NULL);
    if (pair[1] == INVALID_HANDLE_VALUE) goto failed;
    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (event == NULL) goto failed;
    connect_overlapped.hEvent = event;
    if (!ConnectNamedPipe(pair[1], &connect_overlapped)) {
        const DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) pending = TRUE;
        else if (error != ERROR_PIPE_CONNECTED) goto failed;
    }
    pair[0] = CreateFileA(
        name, GENERIC_READ, 0u, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (pair[0] == INVALID_HANDLE_VALUE) goto failed;
    if (pending) {
        DWORD bytes = 0u;
        if (!GetOverlappedResult(
                pair[1], &connect_overlapped, &bytes, TRUE)) goto failed;
    }
    (void)CloseHandle(event);
    return 0;
failed:
    if (pair[0] != INVALID_HANDLE_VALUE)
        (void)native_race_close_pipe(pair[0]);
    if (pair[1] != INVALID_HANDLE_VALUE)
        (void)native_race_close_pipe(pair[1]);
    if (event != NULL) (void)CloseHandle(event);
    pair[0] = INVALID_HANDLE_VALUE;
    pair[1] = INVALID_HANDLE_VALUE;
    return -1;
#else
    int flags;
    if (pipe(pair) != 0) return -1;
    for (size_t i = 0u; i < 2u; ++i) {
        flags = fcntl(pair[i], F_GETFL, 0);
        if (flags < 0 ||
            fcntl(pair[i], F_SETFL, flags | O_NONBLOCK) != 0) {
            (void)native_race_close_pipe(pair[0]);
            (void)native_race_close_pipe(pair[1]);
            pair[0] = NATIVE_RACE_INVALID_PIPE;
            pair[1] = NATIVE_RACE_INVALID_PIPE;
            return -1;
        }
    }
    return 0;
#endif
}

/* Test-only external-progress host. It owns ONE NativeIO backend. CNet
 * borrows it, and all observation, routing and admission use the same owner.
 * No second request registry or completion engine is introduced. */
enum { REQUEST_TEST_TIMEOUT_MS = 5000, REQUEST_TEST_BATCH = 4,
       REQUEST_TEST_REUSE_ATTEMPTS = 32 };

typedef struct request_probe {
    bool connected;
    bool terminal;
    bool failed;
    size_t sends;
    size_t bytes;
} request_probe;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state, const cnet_error *error) {
    request_probe *probe = (request_probe *)user;
    (void)connection;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        probe->terminal = true;
        if (state == CNET_CONNECTION_FAILED || error != NULL)
            probe->failed = true;
    }
}

static void on_send(void *user, cnet_connection connection, size_t bytes) {
    request_probe *probe = (request_probe *)user;
    (void)connection;
    ++probe->sends;
    probe->bytes += bytes;
}

/* A token binds a real NativeIO request and its caller-owned buffer to the
 * authoritative observed terminal; it does not retain a DSO or settle the
 * Session. DSO/Scope lifetime remains a separate outer obligation. */
typedef struct native_receive_act_context {
    unsigned char *buffer;
    size_t settled;
} native_receive_act_context;

NATIVE_IO_ACE_TOKEN_TYPE(scxml_native_recv_act, native_receive_act_context);

/* A distinct typed ACT for native stream-send: completion remains owned
 * by NativeIO; the caller's send bytes may be released only after observe. */
typedef struct native_send_act_context {
    const unsigned char *buffer;
    size_t settled;
} native_send_act_context;

NATIVE_IO_ACE_TOKEN_TYPE(scxml_native_send_act, native_send_act_context);

/* STREAM_CONNECT borrows an immutable sockaddr through authoritative observe.
 * It is independently typed, without a second ACT or completion runtime. */
typedef struct native_connect_act_context {
    const struct sockaddr_in *address;
    size_t settled;
} native_connect_act_context;

NATIVE_IO_ACE_TOKEN_TYPE(scxml_native_connect_act, native_connect_act_context);

static bool same_request(native_io_request a, native_io_request b) {
    return a.slot == b.slot && a.generation == b.generation;
}

static int external_progress(cnet_client *client, native_io_backend *backend,
                             uint32_t timeout_ms, native_io_request watched,
                             native_io_completion *watched_completion,
                             bool *found) {
    native_io_completion batch[REQUEST_TEST_BATCH] = {{0}};
    size_t count = 0u, events = 0u;
    int status = cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;
    status = native_io_backend_observe(
        backend, batch, REQUEST_TEST_BATCH, timeout_ms, &count);
    if (status == SALTS_ETIMEDOUT)
        return cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;

    for (size_t i = 0u; i < count; ++i) {
        bool consumed = false;
        size_t routed_events = 0u;
        if (found != NULL && !*found &&
            same_request(batch[i].request, watched)) {
            *found = true;
            if (watched_completion != NULL)
                *watched_completion = batch[i]; /* authentic observed packet */
        }
        status = cnet_client_route_external_completion(
            client, &batch[i], &consumed, &routed_events);
        if (status != SALTS_OK) return status;
        if (!consumed) return SALTS_EPROTO;
    }
    return SALTS_OK;
}

static int submit_one(cnet_client *client, cnet_connection connection,
                      unsigned char value, native_io_request *request) {
    cnet_external_request_snapshot snapshots[REQUEST_TEST_BATCH] = {{0}};
    mem_buffer_t *buffer;
    size_t count = 0u, events = 0u;
    int status;

    *request = (native_io_request){0};
    buffer = mem_get_buffer(mem_global(), 1u);
    if (buffer == NULL) return SALTS_ENOMEM;
    *(unsigned char *)mem_buffer_data(buffer) = value;
    mem_set_used(buffer, 1u);
    status = cnet_send_buffer(client, connection, buffer);
    mem_buffer_release(buffer);
    if (status != SALTS_OK) return status;
    status = cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;
    status = cnet_client_external_request_snapshots(
        client, connection, snapshots, REQUEST_TEST_BATCH, &count);
    if (status != SALTS_OK) return status;
    for (size_t i = 0u; i < count; ++i) {
        if (snapshots[i].operation_kind == NATIVE_IO_OPERATION_STREAM_SEND) {
            if (native_io_request_valid(*request)) return SALTS_EPROTO;
            *request = snapshots[i].request;
        }
    }
    return native_io_request_valid(*request) ? SALTS_OK : SALTS_ENOENT;
}

/* The producer performs ordinary kernel I/O on the peer socket. Only the
 * owner thread calls NativeIO methods; concurrent calls into the backend are
 * explicitly forbidden by its contract. */
typedef struct native_cancel_producer {
    atomic_int ready;
    atomic_int fire;
    atomic_int written;
    native_race_socket peer;
    unsigned char byte;
} native_cancel_producer;

static void native_cancel_write_peer(void *user) {
    native_cancel_producer *race = (native_cancel_producer *)user;
    const uint64_t deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
    atomic_store_explicit(&race->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&race->fire, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline) {
    }
    if (!atomic_load_explicit(&race->fire, memory_order_acquire)) {
        atomic_store_explicit(&race->written, -1, memory_order_release);
        return;
    }
    atomic_store_explicit(&race->written,
        send(race->peer, (const char *)&race->byte, 1, 0) == 1 ? 1 : -1,
        memory_order_release);
}

spec("CNet external NativeIO slot/generation and stale terminal ownership") {
    it("reuses a real native request slot without accepting the old terminal") {
        const native_io_backend_config io_conf = {
            .kind = test_backend(), .endpoint_capacity = 2u,
            .request_capacity = REQUEST_TEST_BATCH,
            .completion_batch_capacity = REQUEST_TEST_BATCH
        };
        const cnet_client_config cnet_conf = {
            .backend = test_backend(), .connection_capacity = 1u,
            .command_capacity = 8u, .request_capacity = REQUEST_TEST_BATCH,
            .completion_batch_capacity = REQUEST_TEST_BATCH,
            .event_capacity = 8u, .max_send_bytes = 8u,
            .receive_buffer_bytes = 8u,
            .connect_timeout_ms = 1000u, .write_timeout_ms = 1000u
        };
        const cnet_observer observer = {
            .on_state = on_state, .on_send = on_send
        };
        native_io_backend io = {0};
        cnet_client client = {0};
        cnet_listener listener = {0}, outbound = {0};
        cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
        cnet_stream_endpoint remote = CNET_STREAM_ENDPOINT_INIT;
        cnet_connection connection = {0};
        request_probe probe = {0};
        cnet_observer bound_observer = observer;
        native_io_request first = {0}, next = {0};
        native_io_completion old_packet = {0};
        native_io_backend_stats stats = {0};
        uint64_t deadline;
        size_t events = 0u;
        bool captured = false, reused = false, consumed = true;
        unsigned char value = 'A';

        bound_observer.user = &probe;
        check_equal(native_io_backend_init(&io, &io_conf), SALTS_OK);
        check_equal(cnet_client_init_external(&client, &cnet_conf, &io),
                    SALTS_OK);
        bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
        bind.address[0] = 127u; bind.address[3] = 1u;
        check_equal(cnet_listener_open(
            &listener, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4), SALTS_OK);
        check_equal(cnet_listener_bind_open_endpoint(&listener, &bind), SALTS_OK);
        check_equal(cnet_listener_local_endpoint(&listener, &remote), SALTS_OK);
        check_true(remote.port != 0u);
        check_equal(cnet_listener_listen(&listener, 8u), SALTS_OK);
        check_equal(cnet_listener_open(
            &outbound, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4), SALTS_OK);
        check_equal(cnet_listener_connect_endpoint(
            &outbound, &client, &remote, &bound_observer, &connection), SALTS_OK);
        check_null(outbound.impl);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (!probe.connected && !probe.failed &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(external_progress(
                &client, &io, 1u, (native_io_request){0}, NULL, NULL), SALTS_OK);
        }
        check_true(probe.connected);
        check_false(probe.failed);

        check_equal(submit_one(&client, connection, value++, &first), SALTS_OK);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (probe.sends < 1u && cmeta_monotonic_ms() < deadline)
            check_equal(external_progress(
                &client, &io, 1u, first, &old_packet, &captured), SALTS_OK);
        check_true(captured);
        check_equal(probe.sends, (size_t)1u);
        check_true(same_request(first, old_packet.request));
        check_equal(old_packet.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(native_io_backend_cancel(&io, first), SALTS_ENOENT);

        for (size_t round = 0u; round < REQUEST_TEST_REUSE_ATTEMPTS; ++round) {
            size_t old_sends = probe.sends, routed_events = 99u;
            check_equal(submit_one(&client, connection, value++, &next),
                        SALTS_OK);
            if (next.slot == first.slot) {
                /* Same real NativeIO slot, but with a new generation.
                   A stale cancel must not cancel the new NativeIO request. */
                check_not_equal(next.generation, first.generation);
                check_equal(native_io_backend_cancel(&io, first), SALTS_ENOENT);
                check_true(native_io_backend_get_stats(&io, &stats));
                check_equal(stats.active_requests, (size_t)1u);

                /* Duplicate a real already-routed completion while the
                   replacement request is live; not a synthetic packet. */
                consumed = true;
                check_equal(cnet_client_route_external_completion(
                    &client, &old_packet, &consumed, &routed_events), SALTS_OK);
                check_false(consumed);
                check_equal(routed_events, (size_t)0u);
                check_equal(probe.sends, old_sends);
                check_true(native_io_backend_get_stats(&io, &stats));
                check_equal(stats.active_requests, (size_t)1u);
                reused = true;
            }
            deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
            while (probe.sends == old_sends &&
                   !probe.terminal && cmeta_monotonic_ms() < deadline)
                check_equal(external_progress(
                    &client, &io, 1u, next, NULL, NULL), SALTS_OK);
            check_equal(probe.sends, old_sends + 1u);
            check_false(probe.terminal);
            if (reused) {
                /* Even after the genuine replacement terminal is routed,
                   a replay of that packet cannot settle anything twice. */
                break;
            }
        }
        check_true(reused);
        check_equal(probe.bytes, probe.sends);
        check_equal(cnet_close(&client, connection), SALTS_OK);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (!probe.terminal && cmeta_monotonic_ms() < deadline)
            check_equal(external_progress(
                &client, &io, 1u, (native_io_request){0}, NULL, NULL), SALTS_OK);
        check_true(probe.terminal);
        check_false(probe.failed);

        check_equal(cnet_client_stop_external(&client), SALTS_OK);
        check_equal(cnet_client_destroy(&client), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
    }
    it("settles real NativeIO STREAM_SEND after cancel and rejects recycled-slot ABA") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint endpoint = {0};
        native_io_operation operation = {0};
        native_io_request old_request = {0}, next_request = {0};
        native_io_completion old_terminal = {0}, next_terminal = {0};
        native_io_backend_stats stats = {0};
        native_send_act_context old_ctx = {0}, next_ctx = {0};
        native_send_act_context *settled = NULL;
        scxml_native_send_act old_act = {0}, next_act = {0};
        native_race_socket sockets[2] = {
            NATIVE_RACE_INVALID_SOCKET, NATIVE_RACE_INVALID_SOCKET
        };
        unsigned char old_byte = 'S', next_byte = 'T';
        size_t count = 0u;
        int cancel_status;

        check_equal(native_race_open_pair(sockets), 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)sockets[0], &endpoint), SALTS_OK);
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_STREAM_SEND,
            .endpoint = endpoint, .buffer = &old_byte,
            .length = 1u, .user_data = (uintptr_t)101u
        };
        check_equal(native_io_backend_submit(
            &io, &operation, &old_request), SALTS_OK);
        check_true(native_io_request_valid(old_request));
        old_ctx.buffer = &old_byte;
        check_equal(scxml_native_send_act_bind(
            &old_act, old_request, endpoint, (uintptr_t)101u, &old_ctx),
            SALTS_OK);

        /* A real socket write may already be completing when the owner
           requests cancellation. Neither OK nor EALREADY is an ACT terminal. */
        cancel_status = native_io_backend_cancel(&io, old_request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(old_act.active);
        check_equal(old_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_EBUSY);

        check_equal(native_io_backend_observe(
            &io, &old_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(old_terminal.request, old_request));
        check_equal(old_terminal.user_data, (uintptr_t)101u);
        check_true(old_terminal.kind == NATIVE_IO_COMPLETION_OK ||
                   old_terminal.kind == NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(old_terminal.bytes,
                    old_terminal.kind == NATIVE_IO_COMPLETION_OK
                        ? (size_t)1u : (size_t)0u);
        check_equal(scxml_native_send_act_settle(
            &old_act, &old_terminal, &settled), SALTS_OK);
        check_true(settled == &old_ctx);
        check_true(settled->buffer == &old_byte);
        ++settled->settled;
        check_equal(old_ctx.settled, (size_t)1u);
        check_equal(scxml_native_send_act_settle(
            &old_act, &old_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(native_io_backend_cancel(&io, old_request), SALTS_ENOENT);

        /* Capacity one forces a physical slot reuse. A copied real old
           terminal and old cancellation cannot consume the new send ACT. */
        operation.buffer = &next_byte;
        operation.user_data = (uintptr_t)102u;
        check_equal(native_io_backend_submit(
            &io, &operation, &next_request), SALTS_OK);
        check_equal(next_request.slot, old_request.slot);
        check_not_equal(next_request.generation, old_request.generation);
        next_ctx.buffer = &next_byte;
        check_equal(scxml_native_send_act_bind(
            &next_act, next_request, endpoint, (uintptr_t)102u, &next_ctx),
            SALTS_OK);
        check_equal(scxml_native_send_act_settle(
            &next_act, &old_terminal, &settled), SALTS_ENOENT);
        check_null(settled);
        check_true(next_act.active);
        check_equal(next_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_cancel(&io, old_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)1u);

        count = 0u;
        check_equal(native_io_backend_observe(
            &io, &next_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(next_terminal.request, next_request));
        check_equal(next_terminal.user_data, (uintptr_t)102u);
        check_equal(next_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(next_terminal.bytes, (size_t)1u);
        check_equal(scxml_native_send_act_settle(
            &next_act, &next_terminal, &settled), SALTS_OK);
        check_true(settled == &next_ctx);
        check_true(settled->buffer == &next_byte);
        ++settled->settled;
        check_equal(next_ctx.settled, (size_t)1u);
        check_equal(scxml_native_send_act_settle(
            &next_act, &next_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(native_io_backend_cancel(&io, next_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(native_race_close_socket(sockets[0]), 0);
        check_equal(native_race_close_socket(sockets[1]), 0);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }
    it("settles connected UDP_SEND_TO only after genuine terminal and rejects ABA") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint endpoint = {0};
        native_io_operation operation = {0};
        native_io_request old_request = {0}, next_request = {0};
        native_io_completion old_terminal = {0}, next_terminal = {0};
        native_io_backend_stats stats = {0};
        native_send_act_context old_ctx = {0}, next_ctx = {0};
        native_send_act_context *settled = NULL;
        scxml_native_send_act old_act = {0}, next_act = {0};
        native_race_socket sockets[2] = {
            NATIVE_RACE_INVALID_SOCKET, NATIVE_RACE_INVALID_SOCKET
        };
        unsigned char first = 'U', second = 'V';
        size_t count = 0u;
        int cancel_status;

        check_equal(native_race_open_udp_pair(sockets), 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)sockets[0], &endpoint), SALTS_OK);
        /* The peer is a real bound UDP socket, and sender is OS-connected.
           Zero address fields explicitly select connected send semantics. */
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_UDP_SEND_TO, .endpoint = endpoint,
            .buffer = &first, .length = 1u, .user_data = (uintptr_t)201u
        };
        check_equal(native_io_backend_submit(
            &io, &operation, &old_request), SALTS_OK);
        old_ctx.buffer = &first;
        check_equal(scxml_native_send_act_bind(
            &old_act, old_request, endpoint, (uintptr_t)201u, &old_ctx),
            SALTS_OK);

        cancel_status = native_io_backend_cancel(&io, old_request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(old_act.active);
        check_equal(old_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_EBUSY);
        check_equal(native_io_backend_observe(
            &io, &old_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(old_terminal.request, old_request));
        check_equal(old_terminal.user_data, (uintptr_t)201u);
        check_true(old_terminal.kind == NATIVE_IO_COMPLETION_OK ||
                   old_terminal.kind == NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(old_terminal.bytes,
                    old_terminal.kind == NATIVE_IO_COMPLETION_OK
                        ? (size_t)1u : (size_t)0u);
        check_equal(scxml_native_send_act_settle(
            &old_act, &old_terminal, &settled), SALTS_OK);
        check_true(settled == &old_ctx);
        check_true(settled->buffer == &first);
        ++settled->settled;
        check_equal(scxml_native_send_act_settle(
            &old_act, &old_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(old_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(&io, old_request), SALTS_ENOENT);

        operation.buffer = &second;
        operation.user_data = (uintptr_t)202u;
        check_equal(native_io_backend_submit(
            &io, &operation, &next_request), SALTS_OK);
        check_equal(next_request.slot, old_request.slot);
        check_not_equal(next_request.generation, old_request.generation);
        next_ctx.buffer = &second;
        check_equal(scxml_native_send_act_bind(
            &next_act, next_request, endpoint, (uintptr_t)202u, &next_ctx),
            SALTS_OK);
        check_equal(scxml_native_send_act_settle(
            &next_act, &old_terminal, &settled), SALTS_ENOENT);
        check_null(settled);
        check_true(next_act.active);
        check_equal(next_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_cancel(&io, old_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)1u);

        count = 0u;
        check_equal(native_io_backend_observe(
            &io, &next_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(next_terminal.request, next_request));
        check_equal(next_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(next_terminal.bytes, (size_t)1u);
        check_equal(next_terminal.user_data, (uintptr_t)202u);
        check_equal(scxml_native_send_act_settle(
            &next_act, &next_terminal, &settled), SALTS_OK);
        check_true(settled == &next_ctx);
        check_true(settled->buffer == &second);
        ++settled->settled;
        check_equal(scxml_native_send_act_settle(
            &next_act, &next_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(next_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(&io, next_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(native_race_close_socket(sockets[0]), 0);
        check_equal(native_race_close_socket(sockets[1]), 0);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }
    it("arbitrates UDP_RECV_FROM cancellation and reuses its payload/address borrow safely") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint endpoint = {0};
        native_io_operation operation = {0};
        native_io_request old_request = {0}, next_request = {0};
        native_io_completion old_terminal = {0}, next_terminal = {0};
        native_io_backend_stats stats = {0};
        native_receive_act_context old_ctx = {0}, next_ctx = {0};
        native_receive_act_context *settled = NULL;
        scxml_native_recv_act old_act = {0}, next_act = {0};
        native_race_socket sockets[2] = {
            NATIVE_RACE_INVALID_SOCKET, NATIVE_RACE_INVALID_SOCKET
        };
        struct sockaddr_storage old_from = {0}, next_from = {0};
        unsigned char first = 0u, second = 0u;
        size_t count = 0u;
        int cancel_status;

        /* The OS-bound UDP receiver is unconnected, so each successful
           recv-from must populate its independently borrowed source address.
           The connected sender is only an external packet producer. */
        check_equal(native_race_open_udp_pair(sockets), 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)sockets[1], &endpoint), SALTS_OK);
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_UDP_RECV_FROM, .endpoint = endpoint,
            .buffer = &first, .length = 1u, .user_data = (uintptr_t)301u,
            .address = &old_from, .address_capacity = sizeof(old_from)
        };
        check_equal(native_io_backend_submit(
            &io, &operation, &old_request), SALTS_OK);
        old_ctx.buffer = &first;
        check_equal(scxml_native_recv_act_bind(
            &old_act, old_request, endpoint, (uintptr_t)301u, &old_ctx),
            SALTS_OK);
        cancel_status = native_io_backend_cancel(&io, old_request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(old_act.active);
        check_equal(old_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_EBUSY);
        /* Only observe, never cancellation acknowledgement, ends the
           payload AND sockaddr borrow and authorizes one ACT settlement. */
        check_equal(native_io_backend_observe(
            &io, &old_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(old_terminal.request, old_request));
        check_equal(old_terminal.kind, NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(old_terminal.user_data, (uintptr_t)301u);
        check_equal(old_terminal.bytes, (size_t)0u);
        check_equal(scxml_native_recv_act_settle(
            &old_act, &old_terminal, &settled), SALTS_OK);
        check_true(settled == &old_ctx);
        check_true(settled->buffer == &first);
        ++settled->settled;
        check_equal(scxml_native_recv_act_settle(
            &old_act, &old_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(old_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, old_request), SALTS_ENOENT);

        /* Re-admit a new generation in the same physical slot with
           distinct caller-owned payload and sockaddr buffers. */
        operation.buffer = &second;
        operation.address = &next_from;
        operation.user_data = (uintptr_t)302u;
        check_equal(native_io_backend_submit(
            &io, &operation, &next_request), SALTS_OK);
        check_equal(next_request.slot, old_request.slot);
        check_not_equal(next_request.generation, old_request.generation);
        next_ctx.buffer = &second;
        check_equal(scxml_native_recv_act_bind(
            &next_act, next_request, endpoint, (uintptr_t)302u, &next_ctx),
            SALTS_OK);
        check_equal(scxml_native_recv_act_settle(
            &next_act, &old_terminal, &settled), SALTS_ENOENT);
        check_null(settled);
        check_true(next_act.active);
        check_equal(native_io_backend_cancel(
            &io, old_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)1u);

        check_equal(send(sockets[0], "V", 1, 0), 1);
        count = 0u;
        check_equal(native_io_backend_observe(
            &io, &next_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(next_terminal.request, next_request));
        check_equal(next_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(next_terminal.user_data, (uintptr_t)302u);
        check_equal(next_terminal.bytes, (size_t)1u);
        check_equal(second, (unsigned char)'V');
        check_true(next_terminal.address_length >=
                   sizeof(struct sockaddr_in));
        check_true(next_terminal.address_length <= sizeof(next_from));
        check_equal(((const struct sockaddr *)&next_from)->sa_family,
                    AF_INET);
        check_equal(scxml_native_recv_act_settle(
            &next_act, &next_terminal, &settled), SALTS_OK);
        check_true(settled == &next_ctx);
        check_true(settled->buffer == &second);
        ++settled->settled;
        check_equal(scxml_native_recv_act_settle(
            &next_act, &next_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(next_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, next_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(native_race_close_socket(sockets[0]), 0);
        check_equal(native_race_close_socket(sockets[1]), 0);
        check_equal(native_io_backend_release_socket(
            &io, endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }
    it("settles real STREAM_CONNECT only at terminal and fences reused request/address") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint old_endpoint = {0}, next_endpoint = {0};
        native_io_operation operation = {0};
        native_io_request old_request = {0}, next_request = {0};
        native_io_completion old_terminal = {0}, next_terminal = {0};
        native_io_backend_stats stats = {0};
        native_connect_act_context old_ctx = {0}, next_ctx = {0};
        native_connect_act_context *settled = NULL;
        scxml_native_connect_act old_act = {0}, next_act = {0};
        native_race_socket listener = NATIVE_RACE_INVALID_SOCKET;
        native_race_socket clients[2] = {
            NATIVE_RACE_INVALID_SOCKET, NATIVE_RACE_INVALID_SOCKET
        };
        struct sockaddr_in destination = {0}, old_address = {0},
                           next_address = {0};
        size_t count = 0u;
        int cancel_status;

        check_equal(native_race_open_connect_fixture(
            &listener, clients, &destination), 0);
        check_true(destination.sin_port != 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)clients[0], &old_endpoint), SALTS_OK);
        old_address = destination;
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_STREAM_CONNECT,
            .endpoint = old_endpoint, .user_data = (uintptr_t)401u,
            .address = &old_address,
            .address_capacity = sizeof(old_address),
            .address_length = sizeof(old_address)
        };
        check_true(native_io_operation_valid(&operation));
        check_equal(native_io_backend_submit(
            &io, &operation, &old_request), SALTS_OK);
        old_ctx.address = &old_address;
        check_equal(scxml_native_connect_act_bind(
            &old_act, old_request, old_endpoint,
            (uintptr_t)401u, &old_ctx), SALTS_OK);
        /* Cancel may lose to the already completing real kernel connect.
           Neither cancellation acknowledgement nor loopback connection
           readiness is permission to free the borrowed sockaddr. */
        cancel_status = native_io_backend_cancel(&io, old_request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(old_act.active);
        check_equal(old_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_release_socket(
            &io, old_endpoint), SALTS_EBUSY);
        check_equal(native_io_backend_observe(
            &io, &old_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(old_terminal.request, old_request));
        check_equal(old_terminal.user_data, (uintptr_t)401u);
        check_true(old_terminal.kind == NATIVE_IO_COMPLETION_OK ||
                   old_terminal.kind == NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(old_terminal.bytes, (size_t)0u);
        check_equal(scxml_native_connect_act_settle(
            &old_act, &old_terminal, &settled), SALTS_OK);
        check_true(settled == &old_ctx);
        check_true(settled->address == &old_address);
        ++settled->settled;
        check_equal(scxml_native_connect_act_settle(
            &old_act, &old_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(old_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, old_request), SALTS_ENOENT);

        /* Only an observed terminal frees both the request and endpoint.
           Releasing this endpoint does NOT terminate the independent
           listener, nor does it authorize a recycled request identity. */
        check_equal(native_race_close_socket(clients[0]), 0);
        clients[0] = NATIVE_RACE_INVALID_SOCKET;
        check_equal(native_io_backend_release_socket(
            &io, old_endpoint), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)clients[1], &next_endpoint), SALTS_OK);
        check_equal(next_endpoint.slot, old_endpoint.slot);
        check_not_equal(next_endpoint.generation, old_endpoint.generation);
        next_address = destination;
        operation.endpoint = next_endpoint;
        operation.address = &next_address;
        operation.user_data = (uintptr_t)402u;
        check_equal(native_io_backend_submit(
            &io, &operation, &next_request), SALTS_OK);
        check_equal(next_request.slot, old_request.slot);
        check_not_equal(next_request.generation, old_request.generation);
        next_ctx.address = &next_address;
        check_equal(scxml_native_connect_act_bind(
            &next_act, next_request, next_endpoint,
            (uintptr_t)402u, &next_ctx), SALTS_OK);
        check_equal(scxml_native_connect_act_settle(
            &next_act, &old_terminal, &settled), SALTS_ENOENT);
        check_null(settled);
        check_true(next_act.active);
        check_equal(next_ctx.settled, (size_t)0u);
        check_equal(native_io_backend_cancel(
            &io, old_request), SALTS_ENOENT);
        check_equal(native_io_backend_release_socket(
            &io, next_endpoint), SALTS_EBUSY);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)1u);

        /* A genuine new connection, not a synthetic completion, settles the
           replacement. Keep its sockaddr alive until observe returns. */
        count = 0u;
        check_equal(native_io_backend_observe(
            &io, &next_terminal, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(next_terminal.request, next_request));
        check_equal(next_terminal.user_data, (uintptr_t)402u);
        check_equal(next_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(next_terminal.bytes, (size_t)0u);
        check_equal(scxml_native_connect_act_settle(
            &next_act, &next_terminal, &settled), SALTS_OK);
        check_true(settled == &next_ctx);
        check_true(settled->address == &next_address);
        ++settled->settled;
        check_equal(scxml_native_connect_act_settle(
            &next_act, &next_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(next_ctx.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, next_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(native_race_close_socket(clients[1]), 0);
        clients[1] = NATIVE_RACE_INVALID_SOCKET;
        check_equal(native_io_backend_release_socket(
            &io, next_endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
        check_equal(native_race_close_socket(listener), 0);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }
    it("qualifies native PIPE_READ and PIPE_WRITE terminals with one reused ACT slot") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 2u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint read_endpoint = {0}, write_endpoint = {0};
        native_io_operation operation = {0};
        native_io_request cancelled_read = {0}, written = {0},
                          received = {0}, cancelled_write = {0};
        native_io_completion terminal_read = {0}, terminal_write = {0},
                             terminal_receive = {0}, terminal_cancel_write = {0};
        native_io_backend_stats stats = {0};
        native_receive_act_context read_cancel_context = {0},
                                   receive_context = {0};
        native_send_act_context write_context = {0}, write_cancel_context = {0};
        native_receive_act_context *read_settled = NULL;
        native_send_act_context *write_settled = NULL;
        scxml_native_recv_act read_cancel_act = {0}, receive_act = {0};
        scxml_native_send_act write_act = {0}, write_cancel_act = {0};
        native_race_pipe pipes[2] = {
            NATIVE_RACE_INVALID_PIPE, NATIVE_RACE_INVALID_PIPE
        };
        unsigned char cancelled_byte = 0u, received_byte = 0u;
        unsigned char payload = 'P', extra = 'Q';
        size_t count = 0u;
        int cancel_status;

        /* A single backend/owner owns both native pipe endpoints and every
           kernel terminal. No worker, polling fallback, or second registry. */
        check_true(native_io_backend_kind_supports_pipe(test_backend()));
        check_equal(native_race_open_pipe_pair(pipes), 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_pipe(
            &io, (uintptr_t)pipes[0], NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
            &read_endpoint), SALTS_OK);
        check_equal(native_io_backend_attach_pipe(
            &io, (uintptr_t)pipes[1], NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
            &write_endpoint), SALTS_OK);

        /* No writer has submitted data: a real pending PIPE_READ is
           cancelled, but ACT and borrowed read storage remain live until
           the authoritative kernel terminal is observed. */
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_PIPE_READ,
            .endpoint = read_endpoint, .buffer = &cancelled_byte,
            .length = 1u, .user_data = (uintptr_t)501u
        };
        check_equal(native_io_backend_submit(
            &io, &operation, &cancelled_read), SALTS_OK);
        read_cancel_context.buffer = &cancelled_byte;
        check_equal(scxml_native_recv_act_bind(
            &read_cancel_act, cancelled_read, read_endpoint,
            (uintptr_t)501u, &read_cancel_context), SALTS_OK);
        check_equal(native_io_backend_release_pipe(
            &io, read_endpoint), SALTS_EBUSY);
        cancel_status = native_io_backend_cancel(&io, cancelled_read);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(read_cancel_act.active);
        check_equal(read_cancel_context.settled, (size_t)0u);
        check_equal(native_io_backend_observe(
            &io, &terminal_read, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(terminal_read.request, cancelled_read));
        check_equal(terminal_read.kind, NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(terminal_read.bytes, (size_t)0u);
        check_equal(terminal_read.user_data, (uintptr_t)501u);
        check_equal(scxml_native_recv_act_settle(
            &read_cancel_act, &terminal_read, &read_settled), SALTS_OK);
        check_true(read_settled == &read_cancel_context);
        check_true(read_settled->buffer == &cancelled_byte);
        ++read_settled->settled;
        check_equal(scxml_native_recv_act_settle(
            &read_cancel_act, &terminal_read, &read_settled), SALTS_EALREADY);
        check_null(read_settled);
        check_equal(read_cancel_context.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, cancelled_read), SALTS_ENOENT);

        /* The exact same single NativeIO request slot is now used by a
           genuine kernel PIPE_WRITE. Its immutable payload remains borrowed
           until observed, not until submit returns or a callback is guessed. */
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_PIPE_WRITE,
            .endpoint = write_endpoint, .buffer = &payload,
            .length = 1u, .user_data = (uintptr_t)502u
        };
        count = 0u;
        check_equal(native_io_backend_submit(
            &io, &operation, &written), SALTS_OK);
        check_equal(written.slot, cancelled_read.slot);
        check_not_equal(written.generation, cancelled_read.generation);
        write_context.buffer = &payload;
        check_equal(scxml_native_send_act_bind(
            &write_act, written, write_endpoint,
            (uintptr_t)502u, &write_context), SALTS_OK);
        check_equal(scxml_native_send_act_settle(
            &write_act, &terminal_read, &write_settled), SALTS_ENOENT);
        check_null(write_settled);
        check_equal(native_io_backend_cancel(
            &io, cancelled_read), SALTS_ENOENT);
        check_equal(native_io_backend_release_pipe(
            &io, write_endpoint), SALTS_EBUSY);
        check_equal(native_io_backend_observe(
            &io, &terminal_write, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(terminal_write.request, written));
        check_equal(terminal_write.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(terminal_write.bytes, (size_t)1u);
        check_equal(terminal_write.user_data, (uintptr_t)502u);
        check_equal(scxml_native_send_act_settle(
            &write_act, &terminal_write, &write_settled), SALTS_OK);
        check_true(write_settled == &write_context);
        check_true(write_settled->buffer == &payload);
        ++write_settled->settled;
        check_equal(scxml_native_send_act_settle(
            &write_act, &terminal_write, &write_settled), SALTS_EALREADY);
        check_null(write_settled);
        check_equal(write_context.settled, (size_t)1u);

        /* A new PIPE_READ sees the byte written by the previous genuine
           PIPE_WRITE. Replayed read/write completions cannot settle it. */
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_PIPE_READ,
            .endpoint = read_endpoint, .buffer = &received_byte,
            .length = 1u, .user_data = (uintptr_t)503u
        };
        count = 0u;
        check_equal(native_io_backend_submit(
            &io, &operation, &received), SALTS_OK);
        check_equal(received.slot, written.slot);
        check_not_equal(received.generation, written.generation);
        receive_context.buffer = &received_byte;
        check_equal(scxml_native_recv_act_bind(
            &receive_act, received, read_endpoint,
            (uintptr_t)503u, &receive_context), SALTS_OK);
        check_equal(scxml_native_recv_act_settle(
            &receive_act, &terminal_read, &read_settled), SALTS_ENOENT);
        check_null(read_settled);
        check_equal(scxml_native_recv_act_settle(
            &receive_act, &terminal_write, &read_settled), SALTS_ENOENT);
        check_null(read_settled);
        check_equal(native_io_backend_cancel(
            &io, written), SALTS_ENOENT);
        check_equal(native_io_backend_observe(
            &io, &terminal_receive, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(terminal_receive.request, received));
        check_equal(terminal_receive.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(terminal_receive.bytes, (size_t)1u);
        check_equal(received_byte, payload);
        check_equal(scxml_native_recv_act_settle(
            &receive_act, &terminal_receive, &read_settled), SALTS_OK);
        check_true(read_settled == &receive_context);
        ++read_settled->settled;
        check_equal(scxml_native_recv_act_settle(
            &receive_act, &terminal_receive, &read_settled), SALTS_EALREADY);
        check_null(read_settled);
        check_equal(receive_context.settled, (size_t)1u);

        /* WRITE cancellation is best effort: immediate writes can win on
           epoll/Kqueue, and IOCP may deliver OK or CANCELLED. Neither a
           cancel acknowledgement nor an implicit retry settles its ACT. */
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_PIPE_WRITE,
            .endpoint = write_endpoint, .buffer = &extra,
            .length = 1u, .user_data = (uintptr_t)504u
        };
        count = 0u;
        check_equal(native_io_backend_submit(
            &io, &operation, &cancelled_write), SALTS_OK);
        check_equal(cancelled_write.slot, received.slot);
        check_not_equal(cancelled_write.generation, received.generation);
        write_cancel_context.buffer = &extra;
        check_equal(scxml_native_send_act_bind(
            &write_cancel_act, cancelled_write, write_endpoint,
            (uintptr_t)504u, &write_cancel_context), SALTS_OK);
        cancel_status = native_io_backend_cancel(&io, cancelled_write);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(write_cancel_act.active);
        check_equal(write_cancel_context.settled, (size_t)0u);
        check_equal(native_io_backend_release_pipe(
            &io, write_endpoint), SALTS_EBUSY);
        check_equal(native_io_backend_observe(
            &io, &terminal_cancel_write, 1u, REQUEST_TEST_TIMEOUT_MS, &count),
            SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(
            terminal_cancel_write.request, cancelled_write));
        check_equal(terminal_cancel_write.user_data, (uintptr_t)504u);
        check_true(terminal_cancel_write.kind == NATIVE_IO_COMPLETION_OK ||
                   terminal_cancel_write.kind == NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(terminal_cancel_write.bytes,
                    terminal_cancel_write.kind == NATIVE_IO_COMPLETION_OK
                        ? (size_t)1u : (size_t)0u);
        check_equal(scxml_native_send_act_settle(
            &write_cancel_act, &terminal_cancel_write, &write_settled),
            SALTS_OK);
        check_true(write_settled == &write_cancel_context);
        check_true(write_settled->buffer == &extra);
        ++write_settled->settled;
        check_equal(scxml_native_send_act_settle(
            &write_cancel_act, &terminal_cancel_write, &write_settled),
            SALTS_EALREADY);
        check_null(write_settled);
        check_equal(write_cancel_context.settled, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &io, cancelled_write), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);
        check_equal(stats.request_capacity, (size_t)1u);

        /* NativeIO owns terminal identity, while the test owns OS handles.
           Only quiescent endpoints may be released; neither API closes the
           other's resources implicitly. */
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_race_close_pipe(pipes[0]), 0);
        check_equal(native_race_close_pipe(pipes[1]), 0);
        check_equal(native_io_backend_release_pipe(
            &io, read_endpoint), SALTS_OK);
        check_equal(native_io_backend_release_pipe(
            &io, write_endpoint), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
    }
    it("arbitrates real peer completion versus cancellation once on the NativeIO owner") {
        const native_io_backend_config config = {
            .kind = test_backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        native_io_backend io = {0};
        native_io_endpoint endpoint = {0};
        native_io_request request = {0}, replacement = {0};
        native_io_completion first = {0}, second = {0};
        native_receive_act_context first_context = {0}, next_context = {0};
        native_receive_act_context *settled_context = NULL;
        scxml_native_recv_act first_act = {0}, next_act = {0};
        native_io_backend_stats stats = {0};
        native_io_operation operation = {0};
        native_cancel_producer race = {0};
        cmeta_thread_t thread = NULL;
        native_race_socket sockets[2] = {
            NATIVE_RACE_INVALID_SOCKET, NATIVE_RACE_INVALID_SOCKET
        };
        int cancel_status;
        size_t count = 0u;
        unsigned char first_byte = 0u, second_byte = 0u;
        uint64_t deadline;

        check_equal(native_race_open_pair(sockets), 0);
        check_equal(native_io_backend_init(&io, &config), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &io, (uintptr_t)sockets[0], &endpoint), SALTS_OK);
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_STREAM_RECV,
            .endpoint = endpoint, .buffer = &first_byte,
            .length = 1u, .user_data = (uintptr_t)41u
        };
        check_equal(native_io_backend_submit(&io, &operation, &request),
                    SALTS_OK);
        check_true(native_io_request_valid(request));
        first_context.buffer = &first_byte;
        check_equal(scxml_native_recv_act_bind(
            &first_act, request, endpoint, (uintptr_t)41u, &first_context),
            SALTS_OK);

        /* Producer races an actual socket write with an owner-lane native
           cancellation request. Cancellation acknowledgement is NOT a
           terminal; only observe decides between OK and CANCELLED. */
        atomic_init(&race.ready, 0);
        atomic_init(&race.fire, 0);
        atomic_init(&race.written, 0);
        race.peer = sockets[1];
        race.byte = 'A';
        check_equal(cmeta_thread_create(
            &thread, native_cancel_write_peer, &race), SALTS_OK);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (!atomic_load_explicit(&race.ready, memory_order_acquire) &&
               cmeta_monotonic_ms() < deadline) {
        }
        check_equal(atomic_load_explicit(
            &race.ready, memory_order_acquire), 1);
        atomic_store_explicit(&race.fire, 1, memory_order_release);
        cancel_status = native_io_backend_cancel(&io, request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        /* A cancel acknowledgement is not the ACT terminal: the borrowed
           buffer and caller-owned association remain live until observe. */
        check_true(first_act.active);
        check_equal(first_context.settled, (size_t)0u);
        check_equal(cmeta_thread_join(&thread), SALTS_OK);
        cmeta_thread_destroy(&thread);
        check_equal(atomic_load_explicit(
            &race.written, memory_order_acquire), 1);
        check_equal(native_io_backend_release_socket(&io, endpoint),
                    SALTS_EBUSY);

        check_equal(native_io_backend_observe(
            &io, &first, 1u, REQUEST_TEST_TIMEOUT_MS, &count), SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(first.request, request));
        check_true(first.kind == NATIVE_IO_COMPLETION_CANCELLED ||
                   first.kind == NATIVE_IO_COMPLETION_OK);
        {
            /* A shallow-copied ACT cannot consume the genuine completion or
               steal the live owner association. */
            scxml_native_recv_act copied = first_act;
            check_equal(scxml_native_recv_act_settle(
                &copied, &first, &settled_context), SALTS_EINVAL);
            check_null(settled_context);
            check_true(first_act.active);
        }
        check_equal(scxml_native_recv_act_settle(
            &first_act, &first, &settled_context), SALTS_OK);
        check_true(settled_context == &first_context);
        check_true(settled_context->buffer == &first_byte);
        ++settled_context->settled;
        check_equal(first_context.settled, (size_t)1u);
        check_equal(scxml_native_recv_act_settle(
            &first_act, &first, &settled_context), SALTS_EALREADY);
        check_null(settled_context);
        if (first.kind == NATIVE_IO_COMPLETION_CANCELLED) {
            check_equal(first.bytes, (size_t)0u);
        } else {
            check_equal(first.bytes, (size_t)1u);
            check_equal(first_byte, (unsigned char)'A');
        }
        check_equal(native_io_backend_cancel(&io, request), SALTS_ENOENT);
        count = 99u;
        check_equal(native_io_backend_observe(
            &io, &second, 1u, 0u, &count), SALTS_ETIMEDOUT);
        check_equal(count, (size_t)0u);

        /* request_capacity==1 forces a REAL slot recycle; the stale cancel
           must not invalidate the new generation or release its buffer. */
        operation.buffer = &second_byte;
        operation.user_data = (uintptr_t)42u;
        check_equal(native_io_backend_submit(&io, &operation, &replacement),
                    SALTS_OK);
        check_equal(replacement.slot, request.slot);
        check_not_equal(replacement.generation, request.generation);
        next_context.buffer = &second_byte;
        check_equal(scxml_native_recv_act_bind(
            &next_act, replacement, endpoint, (uintptr_t)42u,
            &next_context), SALTS_OK);
        /* This is an actual observed terminal for the numerically recycled
           slot's older generation. It cannot settle the replacement ACT. */
        check_equal(scxml_native_recv_act_settle(
            &next_act, &first, &settled_context), SALTS_ENOENT);
        check_null(settled_context);
        check_true(next_act.active);
        check_equal(next_context.settled, (size_t)0u);
        check_equal(native_io_backend_cancel(&io, request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)1u);
        if (first.kind == NATIVE_IO_COMPLETION_OK) {
            const unsigned char next_byte = 'B';
            check_equal(send(sockets[1], (const char *)&next_byte, 1, 0), 1);
        }
        count = 0u;
        second = (native_io_completion){0};
        check_equal(native_io_backend_observe(
            &io, &second, 1u, REQUEST_TEST_TIMEOUT_MS, &count), SALTS_OK);
        check_equal(count, (size_t)1u);
        check_true(same_request(second.request, replacement));
        check_equal(second.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(scxml_native_recv_act_settle(
            &next_act, &second, &settled_context), SALTS_OK);
        check_true(settled_context == &next_context);
        check_true(settled_context->buffer == &second_byte);
        ++settled_context->settled;
        check_equal(next_context.settled, (size_t)1u);
        check_equal(scxml_native_recv_act_settle(
            &next_act, &second, &settled_context), SALTS_EALREADY);
        check_null(settled_context);
        check_equal(second.bytes, (size_t)1u);
        check_equal(second_byte, (unsigned char)(
            first.kind == NATIVE_IO_COMPLETION_CANCELLED ? 'A' : 'B'));
        check_equal(native_io_backend_cancel(&io, replacement), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&io, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(native_race_close_socket(sockets[0]), 0);
        check_equal(native_race_close_socket(sockets[1]), 0);
        check_equal(native_io_backend_release_socket(&io, endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }
}
