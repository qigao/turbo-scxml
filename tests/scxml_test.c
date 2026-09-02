#include <scxml/scxml.h>
#include <cflow/executor.h>
#include <cflow/statechart_instance.h>
#include <tlog.h>

#include "tinytest.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <turbo/thread.h>

#define SCXML_LOG_CAPTURE_CAPACITY 4u
#define SCXML_LOG_COMPONENT_CAPACITY 32u
#define SCXML_LOG_MESSAGE_CAPACITY 64u

typedef struct scxml_log_capture {
    size_t count;
    turbo_log_level_t levels[SCXML_LOG_CAPTURE_CAPACITY];
    char components[SCXML_LOG_CAPTURE_CAPACITY]
                   [SCXML_LOG_COMPONENT_CAPACITY];
    char messages[SCXML_LOG_CAPTURE_CAPACITY][SCXML_LOG_MESSAGE_CAPACITY];
} scxml_log_capture;

static scxml_status compile_status(
    const char *source, scxml_program *program,
    scxml_diagnostic *diagnostic);

static void capture_scxml_log(const turbo_log_entry_t *entry,
                              void *user_data) {
    scxml_log_capture *capture = (scxml_log_capture *)user_data;
    size_t index;
    size_t component_size;
    size_t message_size;
    if (entry == NULL || capture == NULL ||
        capture->count >= SCXML_LOG_CAPTURE_CAPACITY) {
        return;
    }
    index = capture->count++;
    capture->levels[index] = entry->level;
    component_size = entry->component != NULL ? strlen(entry->component) : 0u;
    if (component_size >= SCXML_LOG_COMPONENT_CAPACITY)
        component_size = SCXML_LOG_COMPONENT_CAPACITY - 1u;
    if (component_size != 0u)
        memcpy(capture->components[index], entry->component, component_size);
    capture->components[index][component_size] = '\0';
    message_size = entry->message_len;
    if (message_size >= SCXML_LOG_MESSAGE_CAPACITY)
        message_size = SCXML_LOG_MESSAGE_CAPACITY - 1u;
    if (message_size != 0u)
        memcpy(capture->messages[index], entry->message, message_size);
    capture->messages[index][message_size] = '\0';
}

static bool run_log_program(const char *source, bool install_logger,
                            scxml_log_capture *capture, bool *out_done,
                            bool *out_errored) {
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *bindings = NULL;
    size_t binding_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_config config = {0};
    cflow_statechart_instance_stats stats = {0};
    tlog_t *previous_logger = tlog_peek_default();
    tlog_t *logger = NULL;
    turbo_log_sink_t *sink = NULL;
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;

    if (source == NULL || capture == NULL || out_done == NULL ||
        out_errored == NULL) {
        return false;
    }
    memset(capture, 0, sizeof(*capture));
    if (install_logger) {
        const tlog_config_t log_config = {
            .min_level = TURBO_LOG_LEVEL_DEBUG, .buffer_size = 0u};
        logger = tlog_create(&log_config);
        if (logger == NULL) goto cleanup;
        sink = turbo_sink_callback_create(capture_scxml_log, capture);
        if (sink == NULL || tlog_add_sink(logger, sink) != 0) goto cleanup;
        sink = NULL;
        tlog_set_default(logger);
    } else {
        tlog_set_default(NULL);
    }
    if (compile_status(source, &program, &diagnostic) != SCXML_OK ||
        !scxml_program_instance_bindings(
            &program, &bindings, &binding_count) ||
        !cflow_executor_serial_init(&executor)) {
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = scxml_program_statechart(&program),
        .initial_state = scxml_program_initial_state(&program),
        .executables = bindings,
        .executable_count = binding_count,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 16u,
        .executor = &executor};
    if (cflow_statechart_instance_init(&instance, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        goto cleanup;
    }
    instance_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_statechart_instance_get_stats(&instance, &stats)) {
        goto cleanup;
    }
    *out_done = stats.done;
    *out_errored = stats.errored;
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    if (logger != NULL) tlog_flush(logger);
    tlog_set_default(previous_logger);
    if (sink != NULL) turbo_sink_destroy(sink);
    if (logger != NULL) tlog_destroy(logger);
    scxml_program_destroy(&program);
    return succeeded;
}

static scxml_status compile_status(const char *source,
                                         scxml_program *program,
                                         scxml_diagnostic *diagnostic) {
    return scxml_compile(program, source, strlen(source), NULL,
                               diagnostic);
}

static bool run_named_external_event(
    const char *source, const char *event_name,
    bool expected_done, uint64_t expected_completed) {
    scxml_program program = {0};
    scxml_session session = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {0};
    scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata)};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;

    if (source == NULL || event_name == NULL ||
        compile_status(source, &program, &diagnostic) != SCXML_OK ||
        !cflow_executor_serial_init(&executor))
        goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 16u};
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (scxml_session_try_send_named_with_metadata(
            &session, event_name, strlen(event_name), &metadata) !=
            CFLOW_MAILBOX_OK ||
        !cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done == expected_done && !stats.errored &&
        stats.external_accepted == UINT64_C(1) &&
        stats.external_completed == expected_completed &&
        stats.external_failed == UINT64_C(0) &&
        stats.external_pending == 0u && stats.external_in_flight == 0u;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    return succeeded;
}

typedef struct scxml_adapter_probe {
    size_t close_calls;
    size_t prepare_send_calls;
    size_t prepare_cancel_calls;
    size_t commit_calls;
    size_t discard_calls;
    scxml_adapter_status send_status;
    scxml_adapter_status cancel_status;
    scxml_send_request last_send;
    scxml_cancel_request last_cancel;
    bool quiescent;
} scxml_adapter_probe;

static void scxml_adapter_ticket_commit(void *user) {
    scxml_adapter_probe *probe = (scxml_adapter_probe *)user;
    if (probe != NULL) ++probe->commit_calls;
}

static void scxml_adapter_ticket_discard(void *user) {
    scxml_adapter_probe *probe = (scxml_adapter_probe *)user;
    if (probe != NULL) ++probe->discard_calls;
}

static scxml_adapter_status scxml_adapter_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    if (user == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL) {
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    {
        scxml_adapter_probe *probe = (scxml_adapter_probe *)user;
        ++probe->prepare_send_calls;
        probe->last_send = *request;
        if (probe->send_status != SCXML_ADAPTER_ACCEPTED) {
            *out_error = "injected send failure";
            return probe->send_status;
        }
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        scxml_adapter_ticket_commit, scxml_adapter_ticket_discard, user};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status scxml_adapter_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    if (user == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL) {
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    {
        scxml_adapter_probe *probe = (scxml_adapter_probe *)user;
        ++probe->prepare_cancel_calls;
        probe->last_cancel = *request;
        if (probe->cancel_status != SCXML_ADAPTER_ACCEPTED) {
            *out_error = "injected cancel failure";
            return probe->cancel_status;
        }
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        scxml_adapter_ticket_commit, scxml_adapter_ticket_discard, user};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void scxml_adapter_close(void *user) {
    scxml_adapter_probe *probe = (scxml_adapter_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool scxml_adapter_is_quiescent(void *user) {
    const scxml_adapter_probe *probe =
        (const scxml_adapter_probe *)user;
    return probe != NULL && probe->quiescent;
}

typedef struct scxml_invoke_probe {
    size_t close_calls;
    size_t prepare_start_calls;
    size_t prepare_cancel_calls;
    size_t prepare_forward_calls;
    size_t commit_calls;
    size_t discard_calls;
    uint64_t start_tokens[4];
    uint64_t cancel_tokens[4];
    uint64_t forward_tokens[8];
    cflow_event_id forward_events[8];
    const cmeta_type_desc *forward_types[8];
    bool forward_payloads[8];
    char last_id[64];
    char last_type[64];
    char last_src[64];
    bool last_autoforward;
    bool quiescent;
    scxml_adapter_status start_status;
    scxml_adapter_status cancel_status;
    scxml_adapter_status forward_status;
    const scxml_log_capture *log_capture;
    bool finalize_seen_before_forward;
    bool forward_envelope_valid;
    scxml_content_kind forward_data_kind;
    char forward_name[64];
    char forward_type[32];
    char forward_send_id[64];
    char forward_origin[128];
    char forward_origin_type[128];
    char forward_invoke_id[64];
    char forward_data[64];
} scxml_invoke_probe;

static void scxml_invoke_ticket_commit(void *user) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    if (probe != NULL) ++probe->commit_calls;
}

static void scxml_invoke_ticket_discard(void *user) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    if (probe != NULL) ++probe->discard_calls;
}

static void copy_invoke_field(
    char *destination, size_t capacity,
    const char *source, size_t source_size) {
    const size_t copied = source_size < capacity - 1u
        ? source_size : capacity - 1u;
    if (copied != 0u) memcpy(destination, source, copied);
    destination[copied] = '\0';
}

static scxml_adapter_status scxml_invoke_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == UINT64_C(0))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (probe->prepare_start_calls < 4u)
        probe->start_tokens[probe->prepare_start_calls] = request->token;
    ++probe->prepare_start_calls;
    copy_invoke_field(
        probe->last_id, sizeof(probe->last_id), request->id,
        request->id_size);
    copy_invoke_field(
        probe->last_type, sizeof(probe->last_type), request->type,
        request->type_size);
    copy_invoke_field(
        probe->last_src, sizeof(probe->last_src), request->src,
        request->src_size);
    probe->last_autoforward = request->autoforward;
    if (probe->start_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "injected invoke start failure";
        return probe->start_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        scxml_invoke_ticket_commit, scxml_invoke_ticket_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status scxml_invoke_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == UINT64_C(0))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (probe->prepare_cancel_calls < 4u)
        probe->cancel_tokens[probe->prepare_cancel_calls] = request->token;
    ++probe->prepare_cancel_calls;
    if (probe->cancel_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "injected invoke cancel failure";
        return probe->cancel_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        scxml_invoke_ticket_commit, scxml_invoke_ticket_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status scxml_invoke_prepare_forward(
    void *user, const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    size_t index;
    if (probe == NULL || request == NULL || request->event == NULL ||
        request->envelope == NULL ||
        request->envelope->abi_version != SCXML_EVENT_ENVELOPE_ABI ||
        request->envelope->struct_size != sizeof(scxml_event_envelope_view) ||
        request->event->payload_type == NULL ||
        request->event->payload == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == UINT64_C(0))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    index = probe->prepare_forward_calls;
    if (index < 8u) {
        probe->forward_tokens[index] = request->token;
        probe->forward_events[index] = request->event->id;
        probe->forward_types[index] = request->event->payload_type;
        probe->forward_payloads[index] =
            *(const bool *)request->event->payload;
    }
    copy_invoke_field(
        probe->forward_name, sizeof(probe->forward_name),
        request->envelope->name, request->envelope->name_size);
    copy_invoke_field(
        probe->forward_type, sizeof(probe->forward_type),
        request->envelope->type, request->envelope->type_size);
    copy_invoke_field(
        probe->forward_send_id, sizeof(probe->forward_send_id),
        request->envelope->send_id, request->envelope->send_id_size);
    copy_invoke_field(
        probe->forward_origin, sizeof(probe->forward_origin),
        request->envelope->origin, request->envelope->origin_size);
    copy_invoke_field(
        probe->forward_origin_type, sizeof(probe->forward_origin_type),
        request->envelope->origin_type,
        request->envelope->origin_type_size);
    copy_invoke_field(
        probe->forward_invoke_id, sizeof(probe->forward_invoke_id),
        request->envelope->invoke_id, request->envelope->invoke_id_size);
    probe->forward_data_kind = request->envelope->data.kind;
    copy_invoke_field(
        probe->forward_data, sizeof(probe->forward_data),
        request->envelope->data.bytes,
        request->envelope->data.byte_count);
    probe->forward_envelope_valid = true;
    ++probe->prepare_forward_calls;
    if (probe->log_capture != NULL) {
        tlog_t *logger = tlog_peek_default();
        if (logger != NULL) tlog_flush(logger);
        if (probe->log_capture->count != 0u)
            probe->finalize_seen_before_forward = true;
    }
    if (probe->forward_status != SCXML_ADAPTER_ACCEPTED) {
        *out_error = "injected invoke forward failure";
        return probe->forward_status;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        scxml_invoke_ticket_commit, scxml_invoke_ticket_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void scxml_invoke_close(void *user) {
    scxml_invoke_probe *probe = (scxml_invoke_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool scxml_invoke_is_quiescent(void *user) {
    const scxml_invoke_probe *probe =
        (const scxml_invoke_probe *)user;
    return probe != NULL && probe->quiescent;
}

typedef struct scxml_executor_blocker {
    atomic_bool entered;
    atomic_bool release;
} scxml_executor_blocker;

static void scxml_block_executor(void *user) {
    scxml_executor_blocker *blocker = (scxml_executor_blocker *)user;
    atomic_store(&blocker->entered, true);
    while (!atomic_load(&blocker->release)) turbo_thread_yield();
}

static const cflow_statechart_state *find_state(
    const scxml_program *program, const char *name) {
    const cflow_statechart *statechart =
        scxml_program_statechart(program);
    cflow_machine_state_id id = 0u;
    size_t index;

    if (!scxml_program_state_id(program, name, strlen(name), &id))
        return NULL;
    for (index = 0u; index < cflow_statechart_state_count(statechart); ++index) {
        const cflow_statechart_state *state =
            cflow_statechart_state_at(statechart, index);
        if (state != NULL && state->id == id) return state;
    }
    return NULL;
}

static bool run_condition_program(const char *source, const char *event_name,
                                  size_t *out_guard_count, bool *out_done,
                                  cflow_machine_state_id *out_current_state) {
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *executables = NULL;
    const cflow_statechart_guard_binding *guards = NULL;
    size_t executable_count = 0u;
    size_t guard_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_config config = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_event_view event = {0};
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;

    if (source == NULL || out_guard_count == NULL || out_done == NULL ||
        compile_status(source, &program, &diagnostic) != SCXML_OK ||
        !scxml_program_instance_bindings(
            &program, &executables, &executable_count) ||
        !scxml_program_guard_bindings(
            &program, &guards, &guard_count) ||
        !cflow_executor_serial_init(&executor)) {
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = scxml_program_statechart(&program),
        .initial_state = scxml_program_initial_state(&program),
        .guards = guards,
        .guard_count = guard_count,
        .executables = executables,
        .executable_count = executable_count,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 4u,
        .microstep_limit = 32u,
        .executor = &executor};
    if (cflow_statechart_instance_init(&instance, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        goto cleanup;
    }
    instance_initialized = true;
    if (event_name != NULL) {
        if (!scxml_program_event(
                &program, event_name, strlen(event_name), &event) ||
            cflow_statechart_instance_try_send(&instance, &event) !=
                CFLOW_MAILBOX_OK ||
            !cflow_executor_wait_idle(&executor)) {
            goto cleanup;
        }
    }
    if (!cflow_statechart_instance_get_stats(&instance, &stats)) goto cleanup;
    *out_guard_count = guard_count;
    *out_done = stats.done;
    if (out_current_state != NULL) {
        *out_current_state =
            cflow_statechart_instance_current_state(&instance);
    }
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    return succeeded;
}

suite("SCXML Core to native CFlow Statechart compiler") {
    it("routes an unseen hierarchical external Event through its longest compiled prefix") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='alarm.system' "
            "target='done'/><transition event='alarm' target='fail'/>"
            "</state><final id='done'/><state id='fail'/></scxml>";

        check_true(run_named_external_event(
            source, "alarm.system.disk.full", true, UINT64_C(1)));
    }

    it("routes a wholly unseen external Event only through wildcard transitions") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='*' target='done'/>"
            "</state><final id='done'/></scxml>";

        check_true(run_named_external_event(
            source, "vendor.device.changed", true, UINT64_C(1)));
    }

    it("consumes an unmatched external Event when no wildcard transition exists") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='known' target='done'/>"
            "</state><final id='done'/></scxml>";

        check_true(run_named_external_event(
            source, "vendor.ignored", false, UINT64_C(1)));
    }

    it("keeps private Event routing hidden and rejects invalid named admission") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='*' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_event_view hidden = {0};
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata)};
        scxml_session_config config = {0};
        char oversized[SCXML_EVENT_METADATA_CAPACITY + 2u];

        memset(oversized, 'e', sizeof(oversized));
        check_equal(compile_status(source, &program, &diagnostic), SCXML_OK);
        check_false(scxml_program_event(
            &program, "vendor.hidden", sizeof("vendor.hidden") - 1u,
            &hidden));
        check_true(cflow_executor_serial_init(&executor));
        config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u};
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_try_send_named_with_metadata(
                        &session, NULL, 1u, &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_try_send_named_with_metadata(
                        &session, "", 0u, &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_try_send_named_with_metadata(
                        &session, oversized, sizeof(oversized), &metadata),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_try_send_named_with_metadata(
                        &session, "vendor.hidden",
                        sizeof("vendor.hidden") - 1u, NULL),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("lowers supported structural elements and deterministic name maps") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='null' initial='idle'>\n"
            "  <state id='idle'>\n"
            "    <onentry/>\n"
            "    <transition event='go' target='work'/>\n"
            "    <onexit/>\n"
            "  </state>\n"
            "  <parallel id='work'>\n"
            "    <state id='left'>\n"
            "      <initial><transition target='left_ready'/></initial>\n"
            "      <state id='left_ready'/>\n"
            "    </state>\n"
            "    <state id='right' initial='right_ready'>\n"
            "      <state id='right_ready'/>\n"
            "      <final id='right_done'/>\n"
            "    </state>\n"
            "    <transition event='done.state.work' target='done'/>\n"
            "  </parallel>\n"
            "  <state id='memory'>\n"
            "    <history id='remember' type='deep'>\n"
            "      <transition target='memory_leaf'/>\n"
            "    </history>\n"
            "    <state id='memory_leaf'/>\n"
            "  </state>\n"
            "  <final id='done'/>\n"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_state *state;
        cflow_event_id go = 0u;
        cflow_event_view event = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_not_null(program.impl);
        statechart = scxml_program_statechart(&program);
        check_not_null(statechart);
        check_true(cmeta_type_equal(cflow_statechart_state_type(statechart),
                                    &cmeta_type_bool));

        state = find_state(&program, "idle");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_ATOMIC);
        state = find_state(&program, "work");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_PARALLEL);
        state = find_state(&program, "left");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_COMPOUND);
        state = find_state(&program, "done");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_FINAL);
        state = find_state(&program, "remember");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_HISTORY_DEEP);

        check_true(scxml_program_event_id(&program, "go", 2u, &go));
        check_not_equal(go, (cflow_event_id)0u);
        check_true(scxml_program_event(&program, "go", 2u, &event));
        check_equal(event.id, go);
        check_true(cmeta_type_equal(event.payload_type, &cmeta_type_bool));
        check_false(*(const bool *)event.payload);
        check_not_null(scxml_program_initial_state(&program));
        check_false(*(const bool *)scxml_program_initial_state(&program));

        scxml_program_destroy(&program);
        check_null(program.impl);
        scxml_program_destroy(&program);
    }

    it("lowers root initial IDREFS as one ordered multi-target transition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='left_alt right_alt'>"
            "<parallel id='both'>"
            "<state id='left' initial='left_default'>"
            "<state id='left_default'/><state id='left_alt'/></state>"
            "<state id='right' initial='right_default'>"
            "<state id='right_default'/><state id='right_alt'/></state>"
            "</parallel></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_state *left_alt;
        const cflow_statechart_state *right_alt;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        statechart = scxml_program_statechart(&program);
        left_alt = find_state(&program, "left_alt");
        right_alt = find_state(&program, "right_alt");
        check_not_null(statechart);
        check_not_null(left_alt);
        check_not_null(right_alt);
        check_equal(cflow_statechart_transition_target_count_at(
                        statechart, 0u),
                    (size_t)2u);
        check_equal(cflow_statechart_transition_target_at(
                        statechart, 0u, 0u),
                    left_alt->id);
        check_equal(cflow_statechart_transition_target_at(
                        statechart, 0u, 1u),
                    right_alt->id);
        scxml_program_destroy(&program);
    }

    it("admits omitted null datamodel and default initial child") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='first'/>\n"
            "  <final id='last'/>\n"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_not_null(find_state(&program, "first"));
        scxml_program_destroy(&program);
    }

    it("publishes an empty borrowed runtime binding view for structural programs") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        size_t binding_count = SIZE_MAX;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        check_null(bindings);
        check_equal(binding_count, (size_t)0u);

        bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        binding_count = SIZE_MAX;
        check_false(scxml_program_instance_bindings(
            NULL, &bindings, &binding_count));
        check_true(bindings ==
                   (const cflow_statechart_executable_binding *)(uintptr_t)1u);
        check_equal(binding_count, SIZE_MAX);

        {
            const cflow_statechart_guard_binding *guard_bindings =
                (const cflow_statechart_guard_binding *)(uintptr_t)1u;
            size_t guard_count = SIZE_MAX;
            check_true(scxml_program_guard_bindings(
                &program, &guard_bindings, &guard_count));
            check_null(guard_bindings);
            check_equal(guard_count, (size_t)0u);

            guard_bindings =
                (const cflow_statechart_guard_binding *)(uintptr_t)1u;
            guard_count = SIZE_MAX;
            check_false(scxml_program_guard_bindings(
                NULL, &guard_bindings, &guard_count));
            check_true(
                guard_bindings ==
                (const cflow_statechart_guard_binding *)(uintptr_t)1u);
            check_equal(guard_count, SIZE_MAX);
        }

        scxml_program_destroy(&program);
    }

    it("runs an adapter-free executable program through an owning session") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'><onentry>"
            "<send event='advance' target='#_internal'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_session_config config = {0};
        uint32_t requirements = UINT32_MAX;
        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(
            &program, &requirements));
        check_equal(requirements,
                    (uint32_t)SCXML_REQUIREMENT_NONE);
        check_true(cflow_executor_serial_init(&executor));
        config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u};
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_null(scxml_session_error(&session));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("preserves inline donedata in an owning null-model session") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='parent'><state id='parent' initial='childDone'>"
            "<final id='childDone'><donedata><content>ready</content>"
            "</donedata></final><transition event='done.state.parent' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        const scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 2u,
            .microstep_limit = 8u};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("copies the stable SCXML Event I/O location without partial output") {
        static const char scxml_type[] =
            "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
        static const char basic_type_literal[] =
            "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor";
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session_config config = {0};
        char location[64] = "unchanged";
        char repeated[64] = {0};
        char basic_location[64] = "unchanged";
        char basic_name[] = "basichttp";
        char basic_type[] =
            "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor";
        char basic_uri[] = "http://127.0.0.1:43123/scxml/session-a";
        scxml_ioprocessor_descriptor basic_http = {
            .name = basic_name,
            .name_size = sizeof(basic_name) - 1u,
            .type = basic_type,
            .type_size = sizeof(basic_type) - 1u,
            .location = basic_uri,
            .location_size = sizeof(basic_uri) - 1u};
        size_t required = 0u;
        size_t repeated_required = 0u;
        size_t basic_required = 0u;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .ioprocessors = &basic_http,
            .ioprocessor_count = 1u};
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        basic_name[0] = 'x';
        basic_type[0] = 'x';
        basic_uri[0] = 'x';

        check_equal(scxml_session_copy_location(
                        &session, NULL, 0u, &required),
                    SCXML_LOCATION_TOO_SMALL);
        check_true(required > sizeof("#_scxml_") - 1u);
        check_true(required <= sizeof(location));
        check_equal(scxml_session_copy_location(
                        &session, location, required - 1u,
                        &repeated_required),
                    SCXML_LOCATION_TOO_SMALL);
        check_equal(repeated_required, required);
        check_equal(location, "unchanged");
        check_equal(scxml_session_copy_location(
                        &session, location, sizeof(location), &required),
                    SCXML_LOCATION_OK);
        check_equal(strncmp(location, "#_scxml_", 8u), 0);
        check_equal(scxml_session_copy_location(
                        &session, repeated, sizeof(repeated),
                        &repeated_required),
                    SCXML_LOCATION_OK);
        check_equal(repeated_required, required);
        check_equal(repeated, location);
        check_equal(scxml_session_copy_ioprocessor_location(
                        &session, scxml_type, sizeof(scxml_type) - 1u,
                        repeated, sizeof(repeated), &repeated_required),
                    SCXML_LOCATION_OK);
        check_equal(repeated, location);
        check_equal(scxml_session_copy_ioprocessor_location(
                        &session, basic_type_literal,
                        sizeof(basic_type_literal) - 1u,
                        NULL, 0u, &basic_required),
                    SCXML_LOCATION_TOO_SMALL);
        check_equal(basic_required,
                    sizeof("http://127.0.0.1:43123/scxml/session-a"));
        check_equal(scxml_session_copy_ioprocessor_location(
                        &session, basic_type_literal,
                        sizeof(basic_type_literal) - 1u,
                        basic_location, basic_required - 1u,
                        &repeated_required),
                    SCXML_LOCATION_TOO_SMALL);
        check_equal(basic_location, "unchanged");
        check_equal(scxml_session_copy_ioprocessor_location(
                        &session, basic_type_literal,
                        sizeof(basic_type_literal) - 1u,
                        basic_location, sizeof(basic_location),
                        &basic_required),
                    SCXML_LOCATION_OK);
        check_equal(basic_location,
                    "http://127.0.0.1:43123/scxml/session-a");
        memcpy(basic_location, "unchanged", sizeof("unchanged"));
        repeated_required = 17u;
        check_equal(scxml_session_copy_ioprocessor_location(
                        &session, "urn:test:missing",
                        sizeof("urn:test:missing") - 1u,
                        basic_location, sizeof(basic_location),
                        &repeated_required),
                    SCXML_LOCATION_INVALID_ARGUMENT);
        check_equal(basic_location, "unchanged");
        check_equal(repeated_required, (size_t)17u);

        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_copy_location(
                        &session, repeated, sizeof(repeated),
                        &repeated_required),
                    SCXML_LOCATION_INVALID_ARGUMENT);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("rejects invalid configured Event I/O processor descriptors") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'/></scxml>";
        static const char scxml_type[] =
            "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_ioprocessor_descriptor rows[2] = {
            {
                .name = "basichttp", .name_size = 9u,
                .type = "urn:test:one", .type_size = 12u,
                .location = "http://one", .location_size = 10u},
            {
                .name = "vendor", .name_size = 6u,
                .type = "urn:test:two", .type_size = 12u,
                .location = "http://two", .location_size = 10u}};
        scxml_session_config config = {0};

        check_equal(compile_status(source, &program, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .ioprocessors = rows,
            .ioprocessor_count = 2u};

        rows[1].name = rows[0].name;
        rows[1].name_size = rows[0].name_size;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        check_null(session.impl);
        rows[1].name = "vendor";
        rows[1].name_size = 6u;
        rows[1].type = rows[0].type;
        rows[1].type_size = rows[0].type_size;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        rows[1].type = "urn:test:two";
        rows[1].type_size = 12u;

        config.ioprocessor_count = 1u;
        rows[0].name = "1invalid";
        rows[0].name_size = 8u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        rows[0].name = NULL;
        rows[0].name_size = 9u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        rows[0].name = "scxml";
        rows[0].name_size = 5u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        rows[0].name = "basichttp";
        rows[0].name_size = 9u;
        rows[0].type = scxml_type;
        rows[0].type_size = sizeof(scxml_type) - 1u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        rows[0].type = "urn:test:one";
        rows[0].type_size = 12u;
        rows[0].location_size = SCXML_EVENT_METADATA_CAPACITY + 1u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED);
        rows[0].location_size = 10u;
        config.ioprocessor_count = SIZE_MAX;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED);
        check_null(session.impl);

        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("validates Event I/O ABI and waits for adapter quiescence") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'><transition event='go' target='only'/>"
            "</state></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_adapter_probe probe = {0};
        scxml_event_io_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = scxml_adapter_prepare_send,
            .prepare_cancel = scxml_adapter_prepare_cancel,
            .close = scxml_adapter_close,
            .is_quiescent = scxml_adapter_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .effect_capacity = 1u,
            .adapter_internal_event_capacity = 1u,
            .delayed_send_capacity = 1u,
            .event_io = &adapter,
            .adapter_user = &probe};
        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));

        adapter.abi_version = 0u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        check_null(session.impl);
        adapter.abi_version = SCXML_ADAPTER_ABI;
        adapter.struct_size = sizeof(adapter) - 1u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.struct_size = sizeof(adapter) + 1u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.struct_size = sizeof(adapter);
        adapter.prepare_send = NULL;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.prepare_send = scxml_adapter_prepare_send;
        adapter.capabilities = UINT64_C(1) << 63u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.capabilities = SCXML_EVENT_IO_CAP_DELAYED_SEND;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_DELAYED_SEND |
            SCXML_EVENT_IO_CAP_CANCEL;

        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_WOULD_BLOCK);
        check_not_null(session.impl);
        check_equal(probe.close_calls, (size_t)1u);
        probe.quiescent = true;
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_null(session.impl);
        check_equal(probe.close_calls, (size_t)1u);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("commits literal sends and same-session delayed cancellation") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='now' target='peer' id='immediate'/>"
            "<send event='survive' target='peer' id='survivor' delay='250ms'/>"
            "<send event='later' target='peer' id='job' delay='1.5s'/>"
            "<cancel sendid='job'/><cancel sendid='missing'/>"
            "</onentry></state></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_adapter_probe probe = {.quiescent = true};
        scxml_event_io_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = scxml_adapter_prepare_send,
            .prepare_cancel = scxml_adapter_prepare_cancel,
            .close = scxml_adapter_close,
            .is_quiescent = scxml_adapter_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 2u,
            .event_io = &adapter,
            .adapter_user = &probe};
        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));

        config.effect_capacity = 0u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        config.effect_capacity = 4u;
        config.adapter_internal_event_capacity = 0u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        config.adapter_internal_event_capacity = 2u;
        config.delayed_send_capacity = 0u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        config.delayed_send_capacity = 2u;

        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.prepare_send_calls, (size_t)3u);
        check_equal(probe.prepare_cancel_calls, (size_t)1u);
        check_equal(probe.commit_calls, (size_t)4u);
        check_equal(probe.discard_calls, (size_t)0u);
        check_equal(probe.last_send.delay_ms, UINT64_C(1500));
        check_equal(probe.last_send.id_size, (size_t)3u);
        check_equal(memcmp(probe.last_send.id, "job", 3u), 0);
        check_equal(probe.last_cancel.send_id_size, (size_t)3u);
        check_equal(memcmp(probe.last_cancel.send_id, "job", 3u), 0);
        check_false(scxml_session_report_send_done(
            &session, "job", 3u));
        check_true(scxml_session_report_send_done(
            &session, "survivor", 8u));
        check_false(scxml_session_report_send_done(
            &session, "survivor", 8u));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("materializes the SCXML Event Processor type for an untyped send") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='loopback'/></onentry></state></scxml>";
        static const char event_processor[] =
            "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_adapter_probe probe = {.quiescent = true};
        const scxml_event_io_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = scxml_adapter_prepare_send,
            .close = scxml_adapter_close,
            .is_quiescent = scxml_adapter_is_quiescent};
        const scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .effect_capacity = 1u,
            .adapter_internal_event_capacity = 1u,
            .event_io = &adapter,
            .adapter_user = &probe};

        check_equal(compile_status(source, &program, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.prepare_send_calls, (size_t)1u);
        check_not_null(probe.last_send.type);
        check_equal(probe.last_send.type_size,
                    sizeof(event_processor) - 1u);
        check_equal(memcmp(probe.last_send.type, event_processor,
                           sizeof(event_processor) - 1u),
                    0);
        check_equal(probe.commit_calls, (size_t)1u);

        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("turns synchronous adapter failures into recoverable error events") {
        static const char execution_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='bad' target='peer'/>"
            "<send event='must.not.run' target='peer'/></onentry>"
            "<transition event='error.execution' target='done'/></state>"
            "<final id='done'/></scxml>";
        static const char communication_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='bad' target='peer'/><log label='must.not.run'/>"
            "</onentry><transition event='error.communication' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *sources[] = {execution_source, communication_source};
        const scxml_adapter_status failures[] = {
            SCXML_ADAPTER_ERROR_EXECUTION, SCXML_ADAPTER_FULL};
        size_t index;
        for (index = 0u; index < 2u; ++index) {
            scxml_program program = {0};
            scxml_session session = {0};
            scxml_diagnostic diagnostic = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            scxml_adapter_probe probe = {
                .send_status = failures[index], .quiescent = true};
            scxml_event_io_adapter adapter = {
                .abi_version = SCXML_ADAPTER_ABI,
                .struct_size = sizeof(scxml_event_io_adapter),
                .capabilities = SCXML_EVENT_IO_CAP_SEND,
                .prepare_send = scxml_adapter_prepare_send,
                .close = scxml_adapter_close,
                .is_quiescent = scxml_adapter_is_quiescent};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .event_io = &adapter,
                .adapter_user = &probe};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_executor_wait_idle(&executor));
            check_true(scxml_session_get_stats(&session, &stats));
            check_true(stats.done);
            check_false(stats.errored);
            check_equal(probe.prepare_send_calls, (size_t)1u);
            check_equal(probe.commit_calls, (size_t)0u);
            check_equal(probe.discard_calls, (size_t)0u);
            check_equal(scxml_session_destroy(&session),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("discards prepared sends on rollback and admits async adapter errors") {
        static const char rollback_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='out' target='peer'/><raise event='one'/>"
            "<raise event='two'/></onentry></state></scxml>";
        static const char async_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'>"
            "<transition event='error.communication' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_event_io_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_SEND,
            .prepare_send = scxml_adapter_prepare_send,
            .close = scxml_adapter_close,
            .is_quiescent = scxml_adapter_is_quiescent};
        size_t index;
        const char *sources[] = {rollback_source, async_source};
        for (index = 0u; index < 2u; ++index) {
            scxml_program program = {0};
            scxml_session session = {0};
            scxml_diagnostic diagnostic = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            scxml_adapter_probe probe = {.quiescent = true};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = index == 0u ? 1u : 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .effect_capacity = 1u,
                .adapter_internal_event_capacity = 2u,
                .event_io = &adapter,
                .adapter_user = &probe};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_OK);
            check_true(cflow_executor_serial_init(&executor));
            check_equal(
                scxml_session_init(&session, &config),
                index == 0u
                    ? CFLOW_STATECHART_INSTANCE_INTERNAL_QUEUE_FULL
                    : CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_executor_wait_idle(&executor));
            if (index == 0u) {
                check_null(session.impl);
                check_equal(probe.commit_calls, (size_t)0u);
                check_equal(probe.discard_calls, (size_t)1u);
                check_equal(probe.close_calls, (size_t)1u);
            } else {
                check_equal(scxml_session_report_adapter_error(
                                &session,
                                SCXML_ADAPTER_ERROR_KIND_COMMUNICATION),
                            CFLOW_MAILBOX_OK);
                check_true(cflow_executor_wait_idle(&executor));
                check_true(scxml_session_get_stats(&session, &stats));
                check_true(stats.done);
                check_false(stats.errored);
            }
            check_equal(scxml_session_destroy(&session),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("lowers one null transition condition to a borrowed native guard") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'>"
            "<transition event='go' cond='  In ( active )  ' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_guard *guard;
        const cflow_statechart_transition *conditioned = NULL;
        const cflow_statechart_guard_binding *guard_bindings = NULL;
        size_t guard_count = 0u;
        size_t index;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        statechart = scxml_program_statechart(&program);
        check_not_null(statechart);
        check_equal(cflow_statechart_guard_count(statechart), (size_t)1u);
        guard = cflow_statechart_guard_at(statechart, 0u);
        check_not_null(guard);
        for (index = 0u;
             index < cflow_statechart_transition_count(statechart); ++index) {
            const cflow_statechart_transition *candidate =
                cflow_statechart_transition_at(statechart, index);
            if (candidate != NULL && candidate->guard != 0u) {
                conditioned = candidate;
                break;
            }
        }
        check_not_null(conditioned);
        check_equal(conditioned->guard, guard->id);
        check_true(scxml_program_guard_bindings(
            &program, &guard_bindings, &guard_count));
        check_not_null(guard_bindings);
        check_equal(guard_count, (size_t)1u);
        check_equal(guard_bindings[0].id, guard->id);
        check_null(guard_bindings[0].fn);
        check_not_null(guard_bindings[0].contextual_fn);
        scxml_program_destroy(&program);
    }

    it("diagnoses invalid transition conditions at the cond attribute") {
        static const char malformed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond='ready' target='a'/></state>"
            "</scxml>";
        static const char quoted[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond=\"In('a')\" target='a'/>"
            "</state></scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond='In(missing)' target='a'/>"
            "</state></scxml>";
        static const char initial_default[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<initial><transition cond='In(a)' target='a'/></initial>"
            "<state id='a'/></scxml>";
        static const char history_default[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><history id='memory'>"
            "<transition cond='In(a)' target='leaf'/></history>"
            "<state id='leaf'/></state></scxml>";
        const char *invalid[] = {
            malformed, quoted, unknown, initial_default, history_default};
        const scxml_status expected[] = {
            SCXML_INVALID_STRUCTURE, SCXML_INVALID_STRUCTURE,
            SCXML_UNKNOWN_TARGET, SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_equal(
                diagnostic.location.byte_offset,
                (size_t)(strstr(invalid[index], "cond") - invalid[index]));
            check_null(program.impl);
        }
    }

    it("falls through a false child condition to a true ancestor condition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='parent'><state id='parent' initial='leaf'>"
            "<transition event='go' cond='In(parent)' target='done'/>"
            "<state id='leaf'><transition event='go' cond='In(other)' "
            "target='wrong'/></state></state><state id='other'/>"
            "<final id='done'/><final id='wrong'/></scxml>";
        size_t guard_count = 0u;
        cflow_machine_state_id done_id = 0u;
        cflow_machine_state_id current_state = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, "go", &guard_count, &done, &current_state));
        {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(source, &program, &diagnostic),
                        SCXML_OK);
            check_true(scxml_program_state_id(
                &program, "done", 4u, &done_id));
            scxml_program_destroy(&program);
        }
        check_equal(guard_count, (size_t)2u);
        check_true(done);
        check_equal(current_state, done_id);
    }

    it("selects the first document-ordered transition whose condition is true") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'>"
            "<transition event='go' cond='In(other)' target='wrong'/>"
            "<transition event='go' cond='In(start)' target='done'/>"
            "</state><state id='other'/><final id='wrong'/>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_machine_state_id done_id = 0u;
        cflow_machine_state_id current_state = 0u;
        size_t guard_count = 0u;
        bool done = false;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_state_id(
            &program, "done", 4u, &done_id));
        scxml_program_destroy(&program);
        check_true(run_condition_program(
            source, "go", &guard_count, &done, &current_state));
        check_equal(guard_count, (size_t)2u);
        check_true(done);
        check_equal(current_state, done_id);
    }

    it("stabilizes a true eventless transition condition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'>"
            "<transition cond='In(start)' target='done'/></state>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, NULL, &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)1u);
        check_true(done);
    }

    it("selects a conditioned completion transition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='parent'><state id='parent' initial='child_done'>"
            "<transition event='done.state.parent' cond='In(parent)' "
            "target='done'/><final id='child_done'/></state>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, NULL, &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)1u);
        check_true(done);
    }

    it("expands a parallel condition across multiple Event descriptors") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='both'><parallel id='both'>"
            "<transition event='go retry' cond='In(left)' target='done'/>"
            "<state id='left'/><state id='right'/></parallel>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, "retry", &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)2u);
        check_true(done);
    }

    it("executes an onentry raise block through native runtime bindings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'>"
            "<onentry><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/>"
            "</state>"
            "<final id='done'/>"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        check_not_null(bindings);
        check_equal(binding_count, (size_t)1u);
        check_equal(cflow_statechart_executable_count(
                        scxml_program_statechart(&program)),
                    (size_t)1u);
        check_equal(cflow_statechart_state_action_count(
                        scxml_program_statechart(&program)),
                    (size_t)1u);

        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = scxml_program_statechart(&program),
            .initial_state = scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        check_true(stats.done);
        check_equal(stats.actions, (uint64_t)1u);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("emits a label-only log and continues executable content") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'><onentry>"
            "<log label='entered'/><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, true, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)1u);
        check_equal(capture.levels[0], TURBO_LOG_LEVEL_DEBUG);
        check_equal(capture.components[0], "cflow.scxml");
        check_equal(capture.messages[0], "entered");
    }

    it("emits only the selected conditional log steps in document order") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<log label='before'/><if cond='In(active)'>"
            "<log label='chosen'/><else/><log label='wrong'/></if>"
            "<raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, true, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)2u);
        check_equal(capture.messages[0], "before");
        check_equal(capture.messages[1], "chosen");
    }

    it("treats a missing default logger as a successful log no-op") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'><onentry>"
            "<log/><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, false, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)0u);
    }

    it("marks log and Event I/O executable blocks with the IO effect") {
        static const char log_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log label='effect'/></onentry>"
            "</state></scxml>";
        static const char raise_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><raise event='next'/></onentry>"
            "</state></scxml>";
        static const char send_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='out' target='peer'/></onentry></state></scxml>";
        const char *sources[] = {log_source, raise_source, send_source};
        const bool expected_io[] = {true, false, true};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            const cflow_statechart *statechart;
            const cflow_statechart_executable *executable;
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_OK);
            statechart = scxml_program_statechart(&program);
            check_equal(cflow_statechart_executable_count(statechart),
                        (size_t)1u);
            executable = cflow_statechart_executable_at(statechart, 0u);
            check_not_null(executable);
            check_true((executable->effects & CMETA_EFFECT_STATEFUL) != 0u);
            check_true((executable->effects & CMETA_EFFECT_MAY_FAIL) != 0u);
            check_equal((executable->effects & CMETA_EFFECT_IO) != 0u,
                        expected_io[index]);
            scxml_program_destroy(&program);
        }
    }

    it("rejects unsupported log expressions at the owning attribute") {
        static const char expression_only[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log label='value' expr='1'/>"
            "</onentry></state></scxml>";
        static const char expression_first[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log expr='1' bad='x'/>"
            "</onentry></state></scxml>";
        static const char invalid_first[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log bad='x' expr='1'/>"
            "</onentry></state></scxml>";
        const char *sources[] = {
            expression_only, expression_first, invalid_first};
        const char *owners[] = {"expr=", "expr=", "bad="};
        const scxml_status expected[] = {
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        expected[index]);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("rejects unknown log attributes and non-comment children") {
        static const char unknown_attribute[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log target='sink'/>"
            "</onentry></state></scxml>";
        static const char child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log><raise event='bad'/></log>"
            "</onentry></state></scxml>";
        const char *sources[] = {unknown_attribute, child};
        const char *owners[] = {"target=", "<raise"};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_INVALID_STRUCTURE);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("shares max_name_bytes across state names and retained log labels") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='s'><state id='s'><onentry><log label='x'/>"
            "</onentry></state></scxml>";
        scxml_limits limits = scxml_default_limits();
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        limits.max_name_bytes = 2u;
        check_equal(scxml_compile(&program, source, strlen(source),
                                        &limits, &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic.message, "max_name_bytes"));
        check_null(program.impl);
    }

    it("admits empty inline content and bounds retained XML fragments") {
        static const char empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='s'><onentry><send target='peer'><content/>"
            "</send></onentry></state></scxml>";
        static const char bounded[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='s'><onentry><send target='peer'><content>"
            "<p:value xmlns:p='urn:test'>payload</p:value>"
            "</content></send></onentry></state></scxml>";
        scxml_limits limits = scxml_default_limits();
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(scxml_compile(
                        &program, empty, strlen(empty), NULL, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);

        limits.max_name_bytes = 16u;
        check_equal(scxml_compile(
                        &program, bounded, strlen(bounded), &limits,
                        &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }

    it("preserves exit transition entry and in-block raise order") {
        static const char source_path[] =
            SCXML_FIXTURE_DIR "/raise_trace.scxml";
        static const char expected_path[] =
            SCXML_FIXTURE_DIR "/raise_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        check_equal(binding_count, (size_t)3u);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = scxml_program_statechart(&program),
            .initial_state = scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 32u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size = (size_t)snprintf(
            actual, sizeof(actual), "done %s\nactions %llu\n",
            stats.done ? "true" : "false",
            (unsigned long long)stats.actions);
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        free(expected);
        free(source);
    }

    it("shares one transition block across every event descriptor") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'><transition event='go retry' target='next'>"
            "<raise event='hit'/></transition></state>"
            "<state id='next'><transition event='hit' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_equal(cflow_statechart_executable_count(
                        scxml_program_statechart(&program)),
                    (size_t)1u);
        check_equal(cflow_statechart_transition_action_count(
                        scxml_program_statechart(&program)),
                    (size_t)2u);
        scxml_program_destroy(&program);
    }

    it("expands exact prefix and wildcard descriptors without duplicates") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'>"
            "<state id='start'><transition event='alpha alpha. alpha.*' "
            "target='done'/></state>"
            "<state id='catalog'><transition event='alpha.beta'/></state>"
            "<state id='all'><transition event='*' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_state *start;
        const cflow_statechart_state *all;
        size_t start_events = 0u;
        size_t all_events = 0u;
        size_t index;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        statechart = scxml_program_statechart(&program);
        start = find_state(&program, "start");
        all = find_state(&program, "all");
        check_not_null(start);
        check_not_null(all);
        for (index = 0u;
             index < cflow_statechart_transition_count(statechart); ++index) {
            const cflow_statechart_transition *transition =
                cflow_statechart_transition_at(statechart, index);
            if (transition->trigger != CFLOW_STATECHART_TRIGGER_EVENT)
                continue;
            if (transition->source == start->id) ++start_events;
            if (transition->source == all->id) ++all_events;
        }
        check_equal(start_events, (size_t)2u);
        check_equal(all_events, (size_t)3u);
        scxml_program_destroy(&program);
    }

    it("executes raise blocks on initial and history default transitions") {
        static const char initial_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<initial><transition target='ready'>"
            "<raise event='initialized'/></transition></initial>"
            "<state id='ready'><transition event='initialized' "
            "target='done'/></state><final id='done'/></scxml>";
        static const char history_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='parent'>"
            "<history id='memory'><transition target='ready'>"
            "<raise event='restored'/></transition></history>"
            "<state id='ready'>"
            "<transition event='restore' target='memory'/>"
            "<transition event='restored' target='done'/>"
            "</state><final id='done'/></state></scxml>";
        const char *sources[] = {initial_source, history_source};
        const char *trigger_names[] = {NULL, "restore"};
        size_t index;
        for (index = 0u; index < 2u; ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            const cflow_statechart_executable_binding *bindings = NULL;
            size_t binding_count = 0u;
            cflow_executor executor = {0};
            cflow_statechart_instance instance = {0};
            cflow_statechart_instance_config config = {0};
            cflow_statechart_instance_stats stats = {0};
            cflow_event_view trigger = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_OK);
            check_true(scxml_program_instance_bindings(
                &program, &bindings, &binding_count));
            check_equal(binding_count, (size_t)1u);
            check_true(cflow_executor_serial_init(&executor));
            config = (cflow_statechart_instance_config){
                .statechart = scxml_program_statechart(&program),
                .initial_state = scxml_program_initial_state(&program),
                .executables = bindings,
                .executable_count = binding_count,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .executor = &executor};
            check_equal(cflow_statechart_instance_init(&instance, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
            if (trigger_names[index] != NULL) {
                check_true(scxml_program_event(
                    &program, trigger_names[index],
                    strlen(trigger_names[index]), &trigger));
                check_equal(cflow_statechart_instance_try_send(
                                &instance, &trigger),
                            CFLOW_MAILBOX_OK);
                check_true(cflow_executor_wait_idle(&executor));
            }
            check_true(cflow_statechart_instance_get_stats(&instance, &stats));
            check_true(stats.done);
            check_equal(cflow_statechart_instance_destroy(&instance),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("rolls back a selected conditional branch when its Event queue is "
       "full") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'><transition event='go' target='next'>"
            "<if cond='In(start)'><raise event='wrong'/><else/>"
            "<raise event='first'/><raise event='second'/></if>"
            "</transition></state><state id='next'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        const cflow_statechart_state *start;
        cflow_machine_state_id states[2] = {0};
        size_t state_count = 0u;
        uint64_t version = 0u;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = scxml_program_statechart(&program),
            .initial_state = scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 1u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        start = find_state(&program, "start");
        check_not_null(start);
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        check_true(stats.errored);
        check_equal(stats.last_status,
                    CFLOW_STATECHART_INSTANCE_INTERNAL_QUEUE_FULL);
        check_equal(stats.internal_pending, (size_t)0u);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(version, UINT64_C(1));
        check_equal(states[state_count - 1u], start->id);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("executes the first matching conditional partition and nested blocks") {
        static const char first_true[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><raise event='hit'/>"
            "<elseif cond='In(active)'/><raise event='wrong'/></if>"
            "</onentry><transition event='hit' target='done'/>"
            "</state><state id='other'/><final id='done'/></scxml>";
        static const char else_match[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(other)'><raise event='wrong'/>"
            "<else/><raise event='hit'/></if></onentry>"
            "<transition event='hit' target='done'/></state>"
            "<state id='other'/><final id='done'/></scxml>";
        static const char no_match[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(other)'><raise event='wrong'/></if>"
            "</onentry></state><state id='other'/></scxml>";
        static const char nested[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><if cond='In(other)'>"
            "<raise event='wrong'/><else/><raise event='hit'/>"
            "</if></if></onentry><transition event='hit' target='done'/>"
            "</state><state id='other'/><final id='done'/></scxml>";
        static const char first_true_empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><elseif cond='In(other)'/>"
            "<raise event='wrong'/><else/><raise event='wrong'/></if>"
            "</onentry></state><state id='other'/></scxml>";
        const char *sources[] = {first_true, else_match, no_match, nested,
                                 first_true_empty};
        const bool expected_done[] = {true, true, false, true, false};
        size_t index;
        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            const cflow_statechart_executable_binding *bindings = NULL;
            size_t binding_count = 0u;
            cflow_executor executor = {0};
            cflow_statechart_instance instance = {0};
            cflow_statechart_instance_config config = {0};
            cflow_statechart_instance_stats stats = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        SCXML_OK);
            check_true(scxml_program_instance_bindings(&program, &bindings,
                                                            &binding_count));
            check_equal(binding_count, (size_t)1u);
            check_true(cflow_executor_serial_init(&executor));
            config = (cflow_statechart_instance_config){
                .statechart = scxml_program_statechart(&program),
                .initial_state = scxml_program_initial_state(&program),
                .executables = bindings,
                .executable_count = binding_count,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .executor = &executor};
            check_equal(cflow_statechart_instance_init(&instance, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_statechart_instance_get_stats(&instance, &stats));
            check_equal(stats.done, expected_done[index]);
            check_equal(stats.actions, UINT64_C(1));
            check_equal(cflow_statechart_instance_destroy(&instance),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
            scxml_program_destroy(&program);
        }
    }

    it("observes action-time configuration in every executable phase") {
        static const char source_path[] =
            SCXML_FIXTURE_DIR "/conditional_trace.scxml";
        static const char expected_path[] =
            SCXML_FIXTURE_DIR "/conditional_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_instance_bindings(&program, &bindings,
                                                        &binding_count));
        check_equal(binding_count, (size_t)6u);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = scxml_program_statechart(&program),
            .initial_state = scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 8u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size = (size_t)snprintf(
            actual, sizeof(actual), "done %s\nactions %llu\n",
            stats.done ? "true" : "false", (unsigned long long)stats.actions);
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);
        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        free(expected);
        free(source);
    }

    it("diagnoses null-model conditions and conditional marker structure") {
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if/></onentry></state></scxml>";
        static const char quoted[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond=\"In('a')\"/>"
            "</onentry></state></scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(missing)'/>"
            "</onentry></state></scxml>";
        static const char after_else[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(a)'><else/>"
            "<elseif cond='In(a)'/></if></onentry></state></scxml>";
        static const char marker_outside[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><else/></onentry></state></scxml>";
        static const char marker_child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(a)'>"
            "<else><raise event='bad'/></else></if>"
            "</onentry></state></scxml>";
        static const char whitespace[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='  In ( a )  '/>"
            "</onentry></state></scxml>";
        const char *invalid[] = {missing, quoted, unknown, after_else,
                                 marker_outside, marker_child};
        const scxml_status expected[] = {
            SCXML_INVALID_STRUCTURE, SCXML_INVALID_STRUCTURE,
            SCXML_UNKNOWN_TARGET, SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;
        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_null(program.impl);
        }
        {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(whitespace, &program, &diagnostic),
                        SCXML_OK);
            scxml_program_destroy(&program);
        }
    }

    it("treats declared pseudo states as inactive in null conditions") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='a'><state id='a' initial='leaf'>"
            "<history id='memory'><transition target='leaf'/></history>"
            "<state id='leaf'><onentry><if cond='In(memory)'>"
            "<raise event='wrong'/><else/><raise event='advance'/></if>"
            "</onentry><transition event='advance' cond='In(memory)' "
            "target='fail'/><transition event='advance' target='pass'/>"
            "<transition event='wrong' target='fail'/></state></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, NULL, &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)1u);
        check_true(done);
    }

    it("admits literal send and cancel profiles with deterministic requirements") {
        static const char internal_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='advance' target='#_internal'/>"
            "</onentry><transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        static const char adapter_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry>"
            "<send event='out' target='peer' type='urn:test' id='job' "
            "delay='1.5s'/><cancel sendid='job'/>"
            "</onentry><transition event='before' target='done'/></state>"
            "<final id='done'/></scxml>";
        const cflow_statechart_executable_binding *sentinel_bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        size_t sentinel_count = 91u;
        cflow_event_id before = 0u;
        cflow_event_id out = 0u;
        cflow_event_id execution = 0u;
        cflow_event_id communication = 0u;
        uint32_t requirements = UINT32_MAX;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(internal_source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(&program, &requirements));
        check_equal(requirements, SCXML_REQUIREMENT_NONE);
        check_true(scxml_program_event_id(
            &program, "advance", 7u, &out));
        scxml_program_destroy(&program);

        check_equal(compile_status(adapter_source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(&program, &requirements));
        check_equal(requirements,
                    SCXML_REQUIREMENT_EVENT_IO |
                        SCXML_REQUIREMENT_DELAYED_SEND |
                        SCXML_REQUIREMENT_CANCEL);
        check_false(scxml_program_instance_bindings(
            &program, &sentinel_bindings, &sentinel_count));
        check_true(
            sentinel_bindings ==
            (const cflow_statechart_executable_binding *)(uintptr_t)1u);
        check_equal(sentinel_count, (size_t)91u);
        check_true(scxml_program_event_id(
            &program, "before", 6u, &before));
        check_true(scxml_program_event_id(&program, "out", 3u, &out));
        check_true(scxml_program_event_id(
            &program, "error.execution", 15u, &execution));
        check_true(scxml_program_event_id(
            &program, "error.communication", 19u, &communication));
        check_equal(out, (cflow_event_id)1u);
        check_equal(before, (cflow_event_id)2u);
        check_equal(execution, (cflow_event_id)3u);
        check_equal(communication, (cflow_event_id)4u);
        scxml_program_destroy(&program);
    }

    it("admits literal invoke and finalize with deterministic IR") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><onentry><log label='entry'/></onentry>"
            "<onexit><log label='exit'/></onexit>"
            "<invoke id='job' type='worker.type' "
            "src='worker://one' autoforward='true'><finalize>"
            "<log label='finalizing'/><if cond='In(worker)'>"
            "<log label='active'/></if></finalize></invoke>"
            "<transition event='done.invoke.job' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        size_t binding_count = 99u;
        cflow_event_id done_event = 0u;
        uint32_t requirements = 0u;
        const cflow_statechart *statechart;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_requirements(
            &program, &requirements));
        check_true((requirements & SCXML_REQUIREMENT_INVOKE) != 0u);
        check_true(scxml_program_event_id(
            &program, "done.invoke.job", 15u, &done_event));
        check_true(done_event != 0u);
        statechart = scxml_program_statechart(&program);
        check_equal(cflow_statechart_executable_count(statechart),
                    (size_t)4u);
        check_equal(cflow_statechart_state_action_count(statechart),
                    (size_t)4u);
        check_equal(cflow_statechart_state_action_at(statechart, 0u)->kind,
                    CFLOW_STATECHART_STATE_ACTION_ENTRY);
        check_equal(cflow_statechart_state_action_at(statechart, 0u)->order,
                    (uint32_t)0u);
        check_equal(cflow_statechart_state_action_at(statechart, 1u)->kind,
                    CFLOW_STATECHART_STATE_ACTION_ENTRY);
        check_equal(cflow_statechart_state_action_at(statechart, 1u)->order,
                    (uint32_t)1u);
        check_equal(cflow_statechart_state_action_at(statechart, 2u)->kind,
                    CFLOW_STATECHART_STATE_ACTION_EXIT);
        check_equal(cflow_statechart_state_action_at(statechart, 2u)->order,
                    (uint32_t)0u);
        check_equal(cflow_statechart_state_action_at(statechart, 3u)->kind,
                    CFLOW_STATECHART_STATE_ACTION_EXIT);
        check_equal(cflow_statechart_state_action_at(statechart, 3u)->order,
                    (uint32_t)1u);
        check_false(scxml_program_instance_bindings(
            &program, &bindings, &binding_count));
        check_true(bindings ==
                   (const cflow_statechart_executable_binding *)
                       (uintptr_t)1u);
        check_equal(binding_count, (size_t)99u);
        scxml_program_destroy(&program);
    }

    it("generates stable invocation IDs and done Events in document order") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<parallel id='worker'><invoke/><invoke autoforward='false'/>"
            "<state id='left'/><state id='right'/></parallel></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_event_id first = 0u;
        cflow_event_id second = 0u;

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(scxml_program_event_id(
            &program, "done.invoke.worker.invoke.1", 27u, &first));
        check_true(scxml_program_event_id(
            &program, "done.invoke.worker.invoke.2", 27u, &second));
        check_true(first != 0u);
        check_equal(second, first + 1u);
        check_equal(cflow_statechart_executable_count(
                        scxml_program_statechart(&program)),
                    (size_t)4u);
        check_equal(cflow_statechart_state_action_count(
                        scxml_program_statechart(&program)),
                    (size_t)4u);
        scxml_program_destroy(&program);
    }

    it("rejects unsupported invoke data forms and malformed finalize structure") {
        static const char root_invoke[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<invoke id='job'/><state id='worker'/></scxml>";
        static const char orphan_finalize[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><finalize/></state></scxml>";
        static const char duplicate_id[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='job'/><invoke id='job'/>"
            "</state></scxml>";
        static const char invalid_id[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='1job'/></state></scxml>";
        static const char expression[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke typeexpr='kind'/></state></scxml>";
        static const char idlocation[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke idlocation='slot'/></state></scxml>";
        static const char srcexpr[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke srcexpr='target'/></state></scxml>";
        static const char namelist[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke namelist='x'/></state></scxml>";
        static const char parameter[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><param name='x' expr='1'/>"
            "</invoke></state></scxml>";
        static const char content[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><content expr='payload'/>"
            "</invoke></state></scxml>";
        static const char finalize_raise[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><finalize><raise event='bad'/>"
            "</finalize></invoke></state></scxml>";
        static const char finalize_send[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><finalize><send event='bad'/>"
            "</finalize></invoke></state></scxml>";
        static const char finalize_cancel[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><finalize><cancel sendid='bad'/>"
            "</finalize></invoke></state></scxml>";
        static const char duplicate_finalize[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke><finalize/><finalize/>"
            "</invoke></state></scxml>";
        static const char bad_autoforward[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke autoforward='yes'/>"
            "</state></scxml>";
        const char *invalid[] = {
            root_invoke, orphan_finalize, duplicate_id, invalid_id,
            expression, idlocation, srcexpr, namelist, parameter, content,
            duplicate_finalize, bad_autoforward};
        const char *valid_finalize[] = {
            finalize_raise, finalize_send, finalize_cancel};
        const scxml_status expected[] = {
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_DUPLICATE_ID,
            SCXML_INVALID_STRUCTURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_null(program.impl);
            check_true(diagnostic.location.byte_offset != 0u);
        }

        for (index = 0u;
             index < sizeof(valid_finalize) / sizeof(valid_finalize[0]);
             ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(valid_finalize[index], &program,
                                       &diagnostic),
                        SCXML_OK);
            check_not_null(program.impl);
            scxml_program_destroy(&program);
        }
    }

    it("validates invocation adapter ABI capacity and quiescent ownership") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='job'/></state></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_invoke_probe probe = {0};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_FORWARD,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .prepare_forward = scxml_invoke_prepare_forward,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 1u,
            .invoke = &adapter,
            .invoke_user = &probe};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config.invocation_capacity = 0u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.abi_version = 0u;
        config.invocation_capacity = 1u;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT);
        adapter.abi_version = SCXML_ADAPTER_ABI;
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_WOULD_BLOCK);
        check_equal(probe.close_calls, (size_t)1u);
        probe.quiescent = true;
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("starts only stable invocations and cancels the committed exit") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='transient'><state id='transient'>"
            "<invoke id='short'/><transition target='worker'/></state>"
            "<state id='worker'><invoke id='job' type='worker.type' "
            "src='worker://one' autoforward='true'/>"
            "<transition event='leave' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_invoke_probe probe = {.quiescent = true};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_FORWARD,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .prepare_forward = scxml_invoke_prepare_forward,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 2u,
            .invoke = &adapter,
            .invoke_user = &probe};
        cflow_event_view leave = {0};
        static const char event_send_id[] = "send-230";
        static const char event_origin[] = "scxml://parent/session";
        static const char event_origin_type[] =
            "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
        static const char event_invoke_id[] = "source-invoke-230";
        static const char event_data[] = "payload-230";
        const scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata),
            .send_id = event_send_id,
            .send_id_size = sizeof(event_send_id) - 1u,
            .origin = event_origin,
            .origin_size = sizeof(event_origin) - 1u,
            .origin_type = event_origin_type,
            .origin_type_size = sizeof(event_origin_type) - 1u,
            .invoke_id = event_invoke_id,
            .invoke_id_size = sizeof(event_invoke_id) - 1u,
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = event_data,
                .byte_count = sizeof(event_data) - 1u}};
        scxml_invoke_stats invoke_stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_start_calls, (size_t)1u);
        check_equal(probe.commit_calls, (size_t)1u);
        check_equal(probe.last_id, "job");
        check_equal(probe.last_type, "worker.type");
        check_equal(probe.last_src, "worker://one");
        check_true(probe.last_autoforward);
        check_true(probe.start_tokens[0] != UINT64_C(0));
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.active, (size_t)1u);
        check_equal(invoke_stats.started, UINT64_C(1));

        check_true(scxml_program_event(
            &program, "leave", 5u, &leave));
        check_equal(scxml_session_try_send_with_metadata(
                        &session, &leave, &metadata),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(probe.prepare_cancel_calls, (size_t)1u);
        check_equal(probe.cancel_tokens[0], probe.start_tokens[0]);
        check_equal(probe.prepare_forward_calls, (size_t)1u);
        check_true(probe.forward_envelope_valid);
        check_equal(probe.forward_name, "leave");
        check_equal(probe.forward_type, "external");
        check_equal(probe.forward_send_id, event_send_id);
        check_equal(probe.forward_origin, event_origin);
        check_equal(probe.forward_origin_type, event_origin_type);
        check_equal(probe.forward_invoke_id, event_invoke_id);
        check_equal(probe.forward_data_kind, SCXML_CONTENT_TEXT_UTF8);
        check_equal(probe.forward_data, event_data);
        check_equal(probe.commit_calls, (size_t)3u);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.active, (size_t)0u);
        check_equal(invoke_stats.cancelled, UINT64_C(1));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("discards invocation lifecycle intents when the microstep rolls back") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='one'><state id='one'><invoke id='first'/>"
            "<transition event='swap' target='two'/></state>"
            "<state id='two'><invoke id='second'/></state></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_invoke_probe probe = {.quiescent = true};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .effect_capacity = 1u,
            .adapter_internal_event_capacity = 2u,
            .invocation_capacity = 2u,
            .invoke = &adapter,
            .invoke_user = &probe};
        cflow_event_view swap = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_invoke_stats invoke_stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_start_calls, (size_t)1u);
        check_true(scxml_program_event(
            &program, "swap", 4u, &swap));
        check_equal(scxml_session_try_send(&session, &swap),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.errored);
        check_equal(stats.last_status,
                    CFLOW_STATECHART_INSTANCE_EFFECT_JOURNAL_FULL);
        check_equal(probe.prepare_cancel_calls, (size_t)0u);
        check_equal(probe.prepare_start_calls, (size_t)1u);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.active, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("finalizes matching returned Events before forwarding and selection") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='first' autoforward='true'>"
            "<finalize><log label='finalizing'/><if cond='In(worker)'>"
            "<log label='active'/></if></finalize></invoke>"
            "<invoke id='second' autoforward='true'/>"
            "<transition event='tick'/>"
            "<transition event='done.invoke.first'>"
            "<log label='transition'/></transition>"
            "<transition event='finish' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_log_capture capture = {0};
        scxml_invoke_probe probe = {
            .quiescent = true, .log_capture = &capture};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_FORWARD,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .prepare_forward = scxml_invoke_prepare_forward,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .effect_capacity = 8u,
            .adapter_internal_event_capacity = 4u,
            .invocation_capacity = 2u,
            .invoke = &adapter,
            .invoke_user = &probe};
        const tlog_config_t log_config = {
            .min_level = TURBO_LOG_LEVEL_DEBUG, .buffer_size = 0u};
        tlog_t *previous_logger = tlog_peek_default();
        tlog_t *logger = tlog_create(&log_config);
        turbo_log_sink_t *sink = turbo_sink_callback_create(
            capture_scxml_log, &capture);
        cflow_event_view tick = {0};
        cflow_event_view done = {0};
        cflow_event_view finish = {0};
        cflow_statechart_instance_stats runtime_stats = {0};
        scxml_invoke_stats invoke_stats = {0};

        check_not_null(logger);
        check_not_null(sink);
        check_equal(tlog_add_sink(logger, sink), 0);
        sink = NULL;
        tlog_set_default(logger);
        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.prepare_start_calls, (size_t)2u);
        check_true(scxml_program_event(&program, "tick", 4u, &tick));
        check_equal(scxml_session_try_send(&session, &tick),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(capture.count, (size_t)0u);
        check_equal(probe.prepare_forward_calls, (size_t)2u);
        check_equal(probe.forward_tokens[0], probe.start_tokens[0]);
        check_equal(probe.forward_tokens[1], probe.start_tokens[1]);

        check_true(scxml_program_event(
            &program, "done.invoke.first", 17u, &done));
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[0]),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        tlog_flush(logger);
        check_equal(capture.count, (size_t)3u);
        check_equal(capture.messages[0], "finalizing");
        check_equal(capture.messages[1], "active");
        check_equal(capture.messages[2], "transition");
        check_true(probe.finalize_seen_before_forward);
        check_equal(probe.prepare_forward_calls, (size_t)3u);
        check_equal(probe.forward_tokens[2], probe.start_tokens[1]);
        check_equal(probe.forward_events[2], done.id);
        check_true(probe.forward_types[2] == done.payload_type);
        check_equal(probe.forward_payloads[2], false);
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[1]),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_program_event(
            &program, "finish", 6u, &finish));
        check_equal(scxml_session_try_send(&session, &finish),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(
            &session, &runtime_stats));
        check_true(runtime_stats.done);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.returned_accepted, UINT64_C(2));
        check_equal(invoke_stats.returned_rejected, UINT64_C(0));
        check_equal(invoke_stats.completed, UINT64_C(2));
        check_equal(invoke_stats.forwarded, UINT64_C(3));
        check_equal(invoke_stats.active, (size_t)0u);
        check_equal(invoke_stats.cancelled, UINT64_C(0));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        tlog_set_default(previous_logger);
        if (sink != NULL) turbo_sink_destroy(sink);
        tlog_destroy(logger);
    }

    it("drops admitted invoke results whose token became stale") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='job'/>"
            "<transition event='leave' target='idle'/></state>"
            "<state id='idle'><transition event='done.invoke.job' "
            "target='bad'/></state><final id='bad'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_executor_blocker blocker;
        scxml_invoke_probe probe = {.quiescent = true};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 4u,
            .invocation_capacity = 1u,
            .invoke = &adapter,
            .invoke_user = &probe};
        cflow_event_view leave = {0};
        cflow_statechart_instance_stats runtime_stats = {0};
        scxml_invoke_stats invoke_stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(
            &program, "leave", 5u, &leave));
        atomic_init(&blocker.entered, false);
        atomic_init(&blocker.release, false);
        check_equal(cflow_executor_try_post(
                        &executor, scxml_block_executor, &blocker),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&blocker.entered)) turbo_thread_yield();
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[0]),
                    CFLOW_MAILBOX_OK);
        atomic_store(&blocker.release, true);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(
            &session, &runtime_stats));
        check_false(runtime_stats.done);
        check_equal(probe.prepare_cancel_calls, (size_t)1u);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.returned_accepted, UINT64_C(1));
        check_equal(invoke_stats.returned_rejected, UINT64_C(1));
        check_equal(scxml_session_report_invoke_done(
                        &session, probe.start_tokens[0]),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.returned_rejected, UINT64_C(2));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("keeps the current external Event ahead of forward failures") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='job' autoforward='true'/>"
            "<transition event='tick' target='seen'/></state>"
            "<state id='seen'><transition event='error.communication' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_invoke_probe probe = {
            .quiescent = true,
            .forward_status = SCXML_ADAPTER_ERROR_COMMUNICATION};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_FORWARD,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .prepare_forward = scxml_invoke_prepare_forward,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 4u,
            .invocation_capacity = 1u,
            .invoke = &adapter,
            .invoke_user = &probe};
        cflow_event_view tick = {0};
        cflow_statechart_instance_stats runtime_stats = {0};
        scxml_invoke_stats invoke_stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(&program, "tick", 4u, &tick));
        check_equal(scxml_session_try_send(&session, &tick),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(
            &session, &runtime_stats));
        check_true(runtime_stats.done);
        check_equal(probe.prepare_forward_calls, (size_t)1u);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.forwarded, UINT64_C(0));
        check_equal(invoke_stats.forward_failed, UINT64_C(1));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("records committed-exit cancellation failures before recovery") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='worker'><invoke id='job'/>"
            "<transition event='leave' target='waiting'/></state>"
            "<state id='waiting'><transition event='error.communication' "
            "target='done'/></state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_invoke_probe probe = {
            .quiescent = true,
            .cancel_status = SCXML_ADAPTER_ERROR_COMMUNICATION};
        scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL,
            .prepare_start = scxml_invoke_prepare_start,
            .prepare_cancel = scxml_invoke_prepare_cancel,
            .close = scxml_invoke_close,
            .is_quiescent = scxml_invoke_is_quiescent};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .effect_capacity = 4u,
            .adapter_internal_event_capacity = 4u,
            .invocation_capacity = 1u,
            .invoke = &adapter,
            .invoke_user = &probe};
        cflow_event_view leave = {0};
        cflow_statechart_instance_stats runtime_stats = {0};
        scxml_invoke_stats invoke_stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_session_init(&session, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(scxml_program_event(
            &program, "leave", 5u, &leave));
        check_equal(scxml_session_try_send(&session, &leave),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(
            &session, &runtime_stats));
        check_true(runtime_stats.done);
        check_true(scxml_session_get_invoke_stats(
            &session, &invoke_stats));
        check_equal(invoke_stats.cancelled, UINT64_C(0));
        check_equal(invoke_stats.cancel_failed, UINT64_C(1));
        check_equal(invoke_stats.adapter_error_rejected, UINT64_C(0));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("rejects non-literal and malformed send and cancel forms") {
        static const char prefix[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry>";
        static const char suffix[] =
            "</onentry></state></scxml>";
        const char *forms[] = {
            "<send/>",
            "<send event='two words'/>",
            "<send eventexpr='dynamic'/>",
            "<send event='x' delay='-1s'/>",
            "<send event='x' delay='1'/>",
            "<send event='x' delay='1s'/>",
            "<send event='x' delayexpr='dynamic'/>",
            "<send event='x'><raise event='nested'/></send>",
            "<cancel/>",
            "<cancel sendidexpr='dynamic'/>",
            "<cancel sendid='not valid'/>",
            "<cancel sendid='job'><log/></cancel>"};
        const scxml_status statuses[] = {
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_UNSUPPORTED_FEATURE,
            SCXML_INVALID_STRUCTURE,
            SCXML_INVALID_STRUCTURE};
        size_t index;
        for (index = 0u; index < sizeof(forms) / sizeof(forms[0]); ++index) {
            char source[512];
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            (void)snprintf(source, sizeof(source), "%s%s%s", prefix,
                           forms[index], suffix);
            check_equal(compile_status(source, &program, &diagnostic),
                        statuses[index]);
            check_null(program.impl);
        }
    }

    it("diagnoses invalid raise syntax at the owning token") {
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise/></onentry></state></scxml>";
        static const char empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event=''/></onentry></state></scxml>";
        static const char whitespace[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='two words'/>"
            "</onentry></state></scxml>";
        static const char child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='x'><raise event='y'/>"
            "</raise></onentry></state></scxml>";
        static const char attribute[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='x' extra='bad'/>"
            "</onentry></state></scxml>";
        const char *sources[] = {missing, empty, whitespace, child, attribute};
        const char *owners[] = {"<raise/>", "event=", "event=", "<raise event='y'", "extra="};
        size_t index;
        for (index = 0u; index < 5u; ++index) {
            scxml_program program = {0};
            scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        index == 4u ? SCXML_UNSUPPORTED_FEATURE
                                    : SCXML_INVALID_STRUCTURE);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("reports namespace version and data-model admission precisely") {
        static const char wrong_namespace[] =
            "<scxml xmlns='urn:not-scxml' version='1.0'><state id='x'/></scxml>";
        static const char wrong_version[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='2.0'>"
            "<state id='x'/></scxml>";
        static const char wrong_model[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='ecmascript'><state id='x'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(wrong_namespace, &program, &diagnostic),
                    SCXML_INVALID_NAMESPACE);
        check_equal(diagnostic.location.byte_offset, (size_t)0u);
        check_null(program.impl);

        check_equal(compile_status(wrong_version, &program, &diagnostic),
                    SCXML_INVALID_VERSION);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(wrong_version, "version") - wrong_version));

        check_equal(compile_status(wrong_model, &program, &diagnostic),
                    SCXML_UNSUPPORTED_DATAMODEL);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(wrong_model, "datamodel") - wrong_model));
    }

    it("admits a bounded SCXML machine name as one XML NMTOKEN") {
        static const char named[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "name='xy'><state id='s'/></scxml>";
        static const char invalid_name[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "name='not valid'><state id='s'/></scxml>";
        scxml_limits limits = scxml_default_limits();
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        limits.max_name_bytes = 3u;
        check_equal(scxml_compile(
                        &program, named, strlen(named), &limits, &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);

        limits.max_name_bytes = 2u;
        check_equal(scxml_compile(
                        &program, named, strlen(named), &limits, &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic.message, "max_name_bytes"));
        check_null(program.impl);

        limits = scxml_default_limits();
        check_equal(scxml_compile(
                        &program, invalid_name, strlen(invalid_name), &limits,
                        &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_not_null(strstr(diagnostic.message, "NMTOKEN"));
        check_null(program.impl);
    }

    it("rejects duplicate IDs and unknown targets at the owning attribute") {
        static const char duplicate[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='same'/>\n"
            "  <state id='same'/>\n"
            "</scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><transition target='missing'/></state>\n"
            "</scxml>";
        static const char phase_order[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><transition target='missing'/></state>\n"
            "  <state id='a'/>\n"
            "</scxml>";
        static const char duplicate_document_order[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='z'/>\n"
            "  <state id='a'/>\n"
            "  <state id='z'/>\n"
            "  <state id='z'/>\n"
            "  <state id='a'/>\n"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(duplicate, &program, &diagnostic),
                    SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)3u);
        check_null(program.impl);

        check_equal(compile_status(unknown, &program, &diagnostic),
                    SCXML_UNKNOWN_TARGET);
        check_equal(diagnostic.location.line, (uint32_t)2u);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(unknown, "target") - unknown));

        check_equal(compile_status(phase_order, &program, &diagnostic),
                    SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)3u);

        check_equal(compile_status(duplicate_document_order, &program,
                                   &diagnostic),
                    SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)4u);
    }

    it("requires state IDs to be XML NCNames") {
        static const char leading_digit[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='1bad'/></scxml>";
        static const char embedded_space[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='bad id'/></scxml>";
        static const char colon[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='bad:id'/></scxml>";
        static const char unicode_name[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='\xe7\x8a\xb6\xe6\x80\x81'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(leading_digit, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(leading_digit, "id=") - leading_digit));
        check_equal(compile_status(embedded_space, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_equal(compile_status(colon, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_equal(compile_status(unicode_name, &program, &diagnostic),
                    SCXML_OK);
        check_not_null(program.impl);
        scxml_program_destroy(&program);
    }

    it("fails fast for unsupported behavior instead of discarding it") {
        static const char executable[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><onentry><log expr='x'/></onentry></state>\n"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(executable, &program, &diagnostic),
                    SCXML_UNSUPPORTED_FEATURE);
        check_equal(diagnostic.location.line, (uint32_t)2u);
    }

    it("rejects an unknown history type at its attribute") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='parent' initial='leaf'>\n"
            "    <history id='memory' type='branch'>"
            "<transition target='leaf'/></history>\n"
            "    <state id='leaf'/>\n"
            "  </state>\n"
            "</scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(source, "type") - source));
        check_null(program.impl);
    }

    it("leaves output empty when XML syntax or configured limits fail") {
        static const char malformed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='x'></scxml>";
        static const char valid[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='xy'/></scxml>";
        static const char exact_descriptor[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_limits limits = scxml_default_limits();

        check_equal(limits.max_events,
                    (size_t)(CFLOW_MACHINE_MAX_EVENTS - 1u));

        check_equal(compile_status(malformed, &program, &diagnostic),
                    SCXML_XML_ERROR);
        check_null(program.impl);

        limits.max_states = 1u;
        check_equal(scxml_compile(&program, valid, strlen(valid), &limits,
                                        &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);

        limits = scxml_default_limits();
        limits.max_name_bytes = 1u;
        check_equal(scxml_compile(&program, valid, strlen(valid), &limits,
                                        &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic.message, "max_name_bytes"));
        check_null(program.impl);

        limits = scxml_default_limits();
        limits.max_transitions = 4u;
        check_equal(scxml_compile(
                        &program, exact_descriptor,
                        sizeof(exact_descriptor) - 1u, &limits,
                        &diagnostic),
                    SCXML_OK);
        scxml_program_destroy(&program);
    }

    it("executes an independent SCXML fixture as the expected native trace") {
        static const char source_path[] =
            SCXML_FIXTURE_DIR "/core_trace.scxml";
        static const char expected_path[] =
            SCXML_FIXTURE_DIR "/core_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        cflow_machine_state_id states[2] = {0u, 0u};
        size_t state_count = 0u;
        uint64_t version = 0u;
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = scxml_program_statechart(&program),
            .initial_state = scxml_program_initial_state(&program),
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 64u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(state_count, (size_t)2u);
        actual_size = (size_t)snprintf(actual, sizeof(actual),
                                       "v%llu %u %u\n",
                                       (unsigned long long)version,
                                       (unsigned int)states[0],
                                       (unsigned int)states[1]);

        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(state_count, (size_t)2u);
        actual_size += (size_t)snprintf(
            actual + actual_size, sizeof(actual) - actual_size,
            "v%llu %u %u\n",
            (unsigned long long)version,
            (unsigned int)states[0], (unsigned int)states[1]);
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size += (size_t)snprintf(
            actual + actual_size, sizeof(actual) - actual_size,
            "done %s\n", stats.done ? "true" : "false");
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        free(expected);
        free(source);
    }
}
