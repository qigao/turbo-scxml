#include <voicexml/quickjs.h>

#include "quickjs_sandbox.h"
#include "quickjs_cmeta_bridge.h"
#include "voicexml_allocator.h"
#include "voicexml_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    VXML_QUICKJS_DEFAULT_EXPRESSION_BYTES = 64u * 1024u,
    VXML_QUICKJS_DEFAULT_URI_BYTES = 4096u,
    VXML_QUICKJS_DEFAULT_HEAP_BYTES = 8u * 1024u * 1024u,
    VXML_QUICKJS_DEFAULT_STACK_BYTES = 256u * 1024u,
    VXML_QUICKJS_DEFAULT_EVAL_MILLISECONDS = 50u,
    VXML_QUICKJS_DEFAULT_CONVERSION_DEPTH = 32u,
    VXML_QUICKJS_DEFAULT_PROPERTIES = 4096u,
    VXML_QUICKJS_DEFAULT_ARRAY_ITEMS = 4096u,
    VXML_QUICKJS_DEFAULT_SNAPSHOT_BYTES = 4u * 1024u * 1024u,
    VXML_QUICKJS_DEFAULT_STATE_STRING_BYTES = 1024u * 1024u,
    VXML_QUICKJS_DEFAULT_SCRIPT_SOURCE_BYTES = 1024u * 1024u,
    VXML_QUICKJS_DEFAULT_DATA_ROWS = 128u,
    VXML_QUICKJS_DEFAULT_DATA_NAMELIST_FIELDS = 64u,
    VXML_QUICKJS_DEFAULT_DATA_BYTES = 1024u * 1024u,
    VXML_QUICKJS_DEFAULT_DATA_VALUE_BYTES = 64u * 1024u
};

typedef struct vxml_quickjs_program_data {
    vxml_quickjs_compile_options_v1 options;
} vxml_quickjs_program_data;

typedef struct vxml_quickjs_session_data {
    quickjs_sandbox_runtime runtime;
    quickjs_cmeta_state_scratch committed_root;

    char *dynamic_uri;
    size_t dynamic_uri_capacity;

    vxml_cmeta_data_resource_adapter_v1 data_resources;
    void *data_resource_user;
    bool has_data_resources;
    size_t max_data_bytes;
    size_t max_data_request_value_bytes;
    vxml_cmeta_data_field_v1 *data_fields;
    size_t data_field_capacity;
    char *data_values;
    size_t data_values_bytes;

    vxml_fetch_audio_adapter_v1 data_fetch_audio;
    void *data_fetch_audio_user;
    bool has_data_fetch_audio;

    bool document_data_done;
    size_t resume_form;
    size_t resume_block;
    bool script_pending;
    const char *last_event;
    size_t last_event_size;
} vxml_quickjs_session_data;

static vxml_status quickjs_profile_session_init(
    vxml_session_impl *session, const void *options);
static vxml_status quickjs_profile_session_start(
    vxml_session_impl *session);
static vxml_status quickjs_profile_session_start_at(
    vxml_session_impl *session, size_t form_index);
static void quickjs_profile_session_destroy(
    vxml_session_impl *session);
static void quickjs_profile_program_destroy(
    vxml_program_impl *program);

static size_t compile_options_v1_prefix_size(void) {
    return offsetof(vxml_quickjs_compile_options_v1, root);
}

static size_t compile_options_state_tail_size(void) {
    return offsetof(
        vxml_quickjs_compile_options_v1,
        max_script_source_bytes) + sizeof(size_t);
}

static size_t compile_options_data_tail_size(void) {
    return offsetof(
        vxml_quickjs_compile_options_v1,
        max_data_namelist_fields) + sizeof(size_t);
}

static size_t session_options_v1_prefix_size(void) {
    return offsetof(vxml_quickjs_session_options_v1, initial_state);
}

static size_t session_options_state_tail_size(void) {
    return offsetof(
        vxml_quickjs_session_options_v1,
        initial_state) + sizeof(const void *);
}

static size_t session_options_data_tail_size(void) {
    return offsetof(
        vxml_quickjs_session_options_v1,
        data_fetch_audio_user) + sizeof(void *);
}

static bool compile_options_has_state_tail(
    const vxml_quickjs_compile_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= compile_options_state_tail_size();
}

static bool compile_options_has_data_tail(
    const vxml_quickjs_compile_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= compile_options_data_tail_size();
}

static bool compile_options_data_enabled(
    const vxml_quickjs_compile_options_v1 *options) {
    return compile_options_has_data_tail(options) &&
        options->max_data_rows != 0u &&
        options->max_data_uri_bytes != 0u &&
        options->max_data_namelist_fields != 0u;
}

static bool session_options_has_state_tail(
    const vxml_quickjs_session_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= session_options_state_tail_size();
}

static bool session_options_has_data_tail(
    const vxml_quickjs_session_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= session_options_data_tail_size();
}

static bool compile_options_state_enabled(
    const vxml_quickjs_compile_options_v1 *options) {
    return compile_options_has_state_tail(options) &&
        options->root != NULL;
}

static quickjs_cmeta_limits cmeta_limits(
    const vxml_quickjs_compile_options_v1 *options) {
    return (quickjs_cmeta_limits){
        .max_conversion_depth =
            options != NULL ? options->max_conversion_depth : 0u,
        .max_properties =
            options != NULL ? options->max_properties : 0u,
        .max_array_items =
            options != NULL ? options->max_array_items : 0u,
        .max_snapshot_bytes =
            options != NULL ? options->max_snapshot_bytes : 0u,
        .max_string_bytes =
            options != NULL ? options->max_state_string_bytes : 0u};
}

static bool compile_options_valid(
    const vxml_quickjs_compile_options_v1 *options) {
    if (options == NULL ||
        options->abi_version != VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 ||
        options->struct_size < compile_options_v1_prefix_size() ||
        options->max_expression_bytes == 0u ||
        options->max_dynamic_script_uri_bytes == 0u ||
        options->max_dynamic_script_uri_bytes == SIZE_MAX ||
        options->max_heap_bytes == 0u ||
        options->max_stack_bytes == 0u ||
        options->max_eval_milliseconds == 0u)
        return false;

    if (compile_options_has_state_tail(options)) {
        if (options->max_resolved_script_uri_bytes == 0u ||
            options->max_resolved_script_uri_bytes == SIZE_MAX ||
            options->max_script_source_bytes == 0u)
            return false;
        if (options->root != NULL &&
            (options->max_conversion_depth == 0u ||
             options->max_properties == 0u ||
             options->max_array_items == 0u ||
             options->max_snapshot_bytes == 0u ||
             options->max_state_string_bytes == 0u ||
             !quickjs_cmeta_limits_valid(&(quickjs_cmeta_limits){
                 options->max_conversion_depth,
                 options->max_properties,
                 options->max_array_items,
                 options->max_snapshot_bytes,
                 options->max_state_string_bytes})))
            return false;
    }

    if (compile_options_has_data_tail(options)) {
        const bool any =
            options->max_data_rows != 0u ||
            options->max_data_uri_bytes != 0u ||
            options->max_data_namelist_fields != 0u;
        const bool all =
            options->max_data_rows != 0u &&
            options->max_data_uri_bytes != 0u &&
            options->max_data_namelist_fields != 0u;
        if (any != all ||
            (all && options->max_data_uri_bytes == SIZE_MAX))
            return false;
    }
    return true;
}

static bool session_options_valid(
    const vxml_quickjs_session_options_v1 *options) {
    return options != NULL &&
        options->abi_version == VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= session_options_v1_prefix_size();
}

static const void *session_initial_state(
    const vxml_quickjs_session_options_v1 *options) {
    return session_options_has_state_tail(options)
        ? options->initial_state : NULL;
}


static bool data_resource_adapter_v3_valid(
    const vxml_cmeta_data_resource_adapter_v1 *adapter) {
    const size_t prefix =
        offsetof(vxml_cmeta_data_resource_adapter_v1, open_v3) +
        sizeof(adapter->open_v3);
    return adapter != NULL &&
        adapter->abi_version ==
            VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1 &&
        adapter->struct_size >= prefix &&
        adapter->close != NULL &&
        adapter->open_v3 != NULL;
}

static bool fetch_audio_adapter_valid(
    const vxml_fetch_audio_adapter_v1 *adapter) {
    const size_t prefix =
        offsetof(vxml_fetch_audio_adapter_v1, begin) +
        sizeof(adapter->begin);
    return adapter != NULL &&
        adapter->abi_version == VXML_FETCH_AUDIO_ADAPTER_ABI_V1 &&
        adapter->struct_size >= prefix &&
        adapter->begin != NULL;
}

static bool program_requires_data_fetch_audio(
    const vxml_program_impl *program) {
    size_t index;
    if (program == NULL || program->data_row_count == 0u)
        return false;
    if (program->data_rows == NULL)
        return true;
    for (index = 0u; index < program->data_row_count; ++index)
        if (program->data_rows[index].fetch_policy.fetchaudio_uri != NULL)
            return true;
    return false;
}

static size_t program_max_data_fields(
    const vxml_program_impl *program) {
    size_t result = 0u;
    size_t index;
    if (program == NULL || program->data_rows == NULL)
        return 0u;
    for (index = 0u; index < program->data_row_count; ++index)
        if (program->data_rows[index].namelist_count > result)
            result = program->data_rows[index].namelist_count;
    return result;
}

static quickjs_sandbox_options sandbox_options(
    const vxml_quickjs_compile_options_v1 *options) {
    size_t max_source_bytes =
        options != NULL ? options->max_expression_bytes : 0u;
    if (compile_options_has_state_tail(options) &&
        options->max_script_source_bytes > max_source_bytes)
        max_source_bytes = options->max_script_source_bytes;
    {
        size_t max_string_bytes =
            options != NULL
                ? options->max_dynamic_script_uri_bytes : 0u;
        if (compile_options_data_enabled(options) &&
            options->max_data_uri_bytes > max_string_bytes)
            max_string_bytes = options->max_data_uri_bytes;
        return (quickjs_sandbox_options){
        .max_source_bytes = max_source_bytes,
        .max_string_bytes = max_string_bytes,
        .max_heap_bytes =
            options != NULL ? options->max_heap_bytes : 0u,
        .max_stack_bytes =
            options != NULL ? options->max_stack_bytes : 0u,
        .max_eval_milliseconds =
            options != NULL ? options->max_eval_milliseconds : 0u};
    }
}

static vxml_status sandbox_status_to_vxml(
    quickjs_sandbox_status status) {
    switch (status) {
    case QUICKJS_SANDBOX_OK:
        return VXML_OK;
    case QUICKJS_SANDBOX_ALLOCATION_FAILED:
        return VXML_ALLOCATION_FAILED;
    case QUICKJS_SANDBOX_LIMIT_EXCEEDED:
        return VXML_LIMIT_EXCEEDED;
    case QUICKJS_SANDBOX_EXCEPTION:
    case QUICKJS_SANDBOX_TYPE_MISMATCH:
        return VXML_SEMANTIC_ERROR;
    case QUICKJS_SANDBOX_INVALID_ARGUMENT:
    default:
        return VXML_INVALID_CONTRACT;
    }
}

static void session_set_event(
    vxml_quickjs_session_data *data,
    const char *event, size_t event_size) {
    if (data == NULL) return;
    data->last_event = event;
    data->last_event_size =
        event != NULL ? event_size : 0u;
}

static vxml_status session_fail(
    vxml_session_impl *session,
    vxml_status status,
    const char *event,
    size_t event_size) {
    vxml_quickjs_session_data *data;
    if (session == NULL) return status;
    data = (vxml_quickjs_session_data *)session->profile_data;
    session_set_event(data, event, event_size);
    session->state = VXML_SESSION_FAILED;
    session->error = status;
    return status;
}

static bool range_valid(
    size_t first, size_t count, size_t total) {
    return first <= total && count <= total - first;
}

static bool program_view_valid(
    const vxml_program_impl *program,
    const char *data,
    size_t size) {
    uintptr_t begin;
    uintptr_t end;
    uintptr_t value;
    if (program == NULL || program->storage == NULL ||
        data == NULL || size == 0u)
        return false;
    begin = (uintptr_t)program->storage;
    if (program->storage_size > UINTPTR_MAX - begin)
        return false;
    end = begin + program->storage_size;
    value = (uintptr_t)data;
    if (value < begin || value >= end ||
        size > (size_t)(end - value) ||
        size == (size_t)(end - value))
        return false;
    return data[size] == '\0';
}


static bool quickjs_xml_space(unsigned char value) {
    return value == (unsigned char)' ' ||
        value == (unsigned char)'\t' ||
        value == (unsigned char)'\r' ||
        value == (unsigned char)'\n';
}

static bool quickjs_next_namelist_token(
    const char *data, size_t size, size_t *cursor,
    const char **out_data, size_t *out_size) {
    size_t start;
    if (data == NULL || cursor == NULL ||
        out_data == NULL || out_size == NULL)
        return false;
    while (*cursor < size &&
           quickjs_xml_space((unsigned char)data[*cursor]))
        ++*cursor;
    if (*cursor == size) {
        *out_data = NULL;
        *out_size = 0u;
        return true;
    }
    start = *cursor;
    while (*cursor < size &&
           !quickjs_xml_space((unsigned char)data[*cursor]))
        ++*cursor;
    *out_data = data + start;
    *out_size = *cursor - start;
    return true;
}

static vxml_status validate_dynamic_scripts(
    const vxml_program *program,
    const vxml_quickjs_compile_options_v1 *options,
    vxml_diagnostic *diagnostic) {
    const vxml_program_impl *impl;
    quickjs_sandbox_options sandbox;
    size_t index;

    if (program == NULL || program->impl == NULL ||
        !compile_options_valid(options))
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_program_impl *)program->impl;
    sandbox = sandbox_options(options);

    for (index = 0u; index < impl->action_count; ++index) {
        const vxml_action_row *action = &impl->actions[index];
        char message[VXML_DIAGNOSTIC_CAPACITY] = {0};
        quickjs_sandbox_status sandbox_status;
        vxml_status status;
        if (action->kind != VXML_ACTION_SCRIPT_EXTERNAL ||
            action->script_srcexpr == NULL)
            continue;
        if (action->script_src != NULL ||
            action->script_src_size != 0u ||
            action->script_srcexpr_size == 0u ||
            !program_view_valid(
                impl, action->script_srcexpr,
                action->script_srcexpr_size) ||
            action->script_srcexpr_size >
                options->max_expression_bytes) {
            status = action->script_srcexpr_size >
                    options->max_expression_bytes
                ? VXML_LIMIT_EXCEEDED
                : VXML_INVALID_STRUCTURE;
            if (diagnostic != NULL) {
                memset(diagnostic, 0, sizeof(*diagnostic));
                diagnostic->status = status;
                diagnostic->location = action->script_location;
                (void)snprintf(
                    diagnostic->message,
                    sizeof(diagnostic->message),
                    "%s",
                    status == VXML_LIMIT_EXCEEDED
                        ? "VoiceXML script srcexpr exceeds max_expression_bytes"
                        : "VoiceXML dynamic script descriptor is invalid");
            }
            return status;
        }
        sandbox_status = quickjs_sandbox_validate_expression(
            &sandbox,
            action->script_srcexpr,
            action->script_srcexpr_size,
            "<voicexml-script-srcexpr>",
            message, sizeof(message));
        if (sandbox_status == QUICKJS_SANDBOX_OK)
            continue;
        status = sandbox_status_to_vxml(sandbox_status);
        if (diagnostic != NULL) {
            memset(diagnostic, 0, sizeof(*diagnostic));
            diagnostic->status = status;
            diagnostic->location = action->script_location;
            (void)snprintf(
                diagnostic->message,
                sizeof(diagnostic->message),
                "%s",
                message[0] != '\0'
                    ? message
                    : "VoiceXML script srcexpr validation failed");
        }
        return status;
    }
    return VXML_OK;
}


static void quickjs_compile_diagnostic(
    vxml_diagnostic *diagnostic,
    vxml_status status,
    salts_xml_location location,
    const char *message) {
    if (diagnostic == NULL) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = status;
    diagnostic->location = location;
    (void)snprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        "%s", message != NULL ? message : "VoiceXML QuickJS compile error");
}

static vxml_status validate_data_rows(
    const vxml_program *program,
    const vxml_quickjs_compile_options_v1 *options,
    vxml_diagnostic *diagnostic) {
    const vxml_program_impl *impl;
    quickjs_sandbox_options sandbox;
    size_t index;

    if (program == NULL || program->impl == NULL ||
        !compile_options_valid(options))
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_program_impl *)program->impl;
    if (!compile_options_data_enabled(options))
        return impl->data_row_count == 0u
            ? VXML_OK : VXML_INVALID_CONTRACT;
    if (impl->data_row_count > options->max_data_rows) {
        const salts_xml_location location =
            impl->data_row_count != 0u && impl->data_rows != NULL
                ? impl->data_rows[options->max_data_rows].location
                : (salts_xml_location){0};
        quickjs_compile_diagnostic(
            diagnostic, VXML_LIMIT_EXCEEDED, location,
            "VoiceXML data row count exceeds max_data_rows");
        return VXML_LIMIT_EXCEEDED;
    }
    if (impl->data_row_count != 0u && impl->data_rows == NULL)
        return VXML_INVALID_CONTRACT;
    sandbox = sandbox_options(options);

    for (index = 0u; index < impl->data_row_count; ++index) {
        const vxml_data_row *row = &impl->data_rows[index];
        const bool has_uri =
            row->uri != NULL || row->uri_size != 0u;
        const bool has_expression =
            row->uri_expression != NULL ||
            row->uri_expression_size != 0u;
        char message[VXML_DIAGNOSTIC_CAPACITY] = {0};
        quickjs_sandbox_status sandbox_status;
        vxml_status status;

        if ((row->placement != VXML_DATA_DOCUMENT &&
             row->placement != VXML_DATA_FORM &&
             row->placement != VXML_DATA_EXECUTABLE) ||
            (row->placement == VXML_DATA_DOCUMENT &&
             row->owner_form != SIZE_MAX) ||
            (row->placement != VXML_DATA_DOCUMENT &&
             row->owner_form >= impl->form_count) ||
            has_uri == has_expression ||
            (row->name == NULL) != (row->name_size == 0u) ||
            (row->namelist == NULL) != (row->namelist_size == 0u) ||
            (row->fetch_policy.fetchaudio_uri == NULL) !=
                (row->fetch_policy.fetchaudio_uri_size == 0u) ||
            (row->method != VXML_SUBMIT_METHOD_GET &&
             row->method != VXML_SUBMIT_METHOD_POST) ||
            (row->enctype != VXML_SUBMIT_ENCTYPE_URLENCODED &&
             row->enctype != VXML_SUBMIT_ENCTYPE_MULTIPART_FORM_DATA) ||
            (row->fetch_policy.fetch_hint !=
                 VXML_CMETA_DATA_FETCH_HINT_UNSPECIFIED &&
             row->fetch_policy.fetch_hint !=
                 VXML_CMETA_DATA_FETCH_HINT_PREFETCH &&
             row->fetch_policy.fetch_hint !=
                 VXML_CMETA_DATA_FETCH_HINT_SAFE)) {
            quickjs_compile_diagnostic(
                diagnostic, VXML_INVALID_STRUCTURE, row->location,
                "VoiceXML data descriptor is invalid");
            return VXML_INVALID_STRUCTURE;
        }
        if (row->name != NULL &&
            !program_view_valid(impl, row->name, row->name_size)) {
            quickjs_compile_diagnostic(
                diagnostic, VXML_INVALID_STRUCTURE, row->location,
                "VoiceXML data name view is invalid");
            return VXML_INVALID_STRUCTURE;
        }
        if (row->uri != NULL) {
            if (!program_view_valid(impl, row->uri, row->uri_size) ||
                row->uri_size > options->max_data_uri_bytes) {
                status = row->uri_size > options->max_data_uri_bytes
                    ? VXML_LIMIT_EXCEEDED : VXML_INVALID_STRUCTURE;
                quickjs_compile_diagnostic(
                    diagnostic, status, row->location,
                    status == VXML_LIMIT_EXCEEDED
                        ? "VoiceXML data src exceeds max_data_uri_bytes"
                        : "VoiceXML data src view is invalid");
                return status;
            }
        } else {
            if (!program_view_valid(
                    impl, row->uri_expression,
                    row->uri_expression_size) ||
                row->uri_expression_size >
                    options->max_expression_bytes) {
                status =
                    row->uri_expression_size >
                        options->max_expression_bytes
                    ? VXML_LIMIT_EXCEEDED
                    : VXML_INVALID_STRUCTURE;
                quickjs_compile_diagnostic(
                    diagnostic, status, row->location,
                    status == VXML_LIMIT_EXCEEDED
                        ? "VoiceXML data srcexpr exceeds max_expression_bytes"
                        : "VoiceXML data srcexpr view is invalid");
                return status;
            }
            sandbox_status = quickjs_sandbox_validate_expression(
                &sandbox,
                row->uri_expression,
                row->uri_expression_size,
                "<voicexml-data-srcexpr>",
                message, sizeof(message));
            if (sandbox_status != QUICKJS_SANDBOX_OK) {
                status = sandbox_status_to_vxml(sandbox_status);
                quickjs_compile_diagnostic(
                    diagnostic, status, row->location,
                    message[0] != '\0'
                        ? message
                        : "VoiceXML data srcexpr validation failed");
                return status;
            }
        }
        if (row->namelist_count != 0u &&
            !compile_options_state_enabled(options)) {
            quickjs_compile_diagnostic(
                diagnostic, VXML_INVALID_CONTRACT,
                row->location,
                "VoiceXML QuickJS data namelist requires typed committed state");
            return VXML_INVALID_CONTRACT;
        }
        if (row->namelist_count >
            options->max_data_namelist_fields) {
            quickjs_compile_diagnostic(
                diagnostic, VXML_LIMIT_EXCEEDED, row->location,
                "VoiceXML data namelist exceeds max_data_namelist_fields");
            return VXML_LIMIT_EXCEEDED;
        }
        if (row->namelist_count == 0u) {
            if (row->namelist != NULL || row->namelist_size != 0u) {
                quickjs_compile_diagnostic(
                    diagnostic, VXML_INVALID_STRUCTURE, row->location,
                    "VoiceXML data namelist descriptor is invalid");
                return VXML_INVALID_STRUCTURE;
            }
        } else {
            size_t cursor = 0u;
            size_t field = 0u;
            if (!program_view_valid(
                    impl, row->namelist, row->namelist_size)) {
                quickjs_compile_diagnostic(
                    diagnostic, VXML_INVALID_STRUCTURE, row->location,
                    "VoiceXML data namelist view is invalid");
                return VXML_INVALID_STRUCTURE;
            }
            while (field < row->namelist_count) {
                const char *name = NULL;
                size_t name_size = 0u;
                if (!quickjs_next_namelist_token(
                        row->namelist, row->namelist_size,
                        &cursor, &name, &name_size) ||
                    name == NULL || name_size == 0u) {
                    quickjs_compile_diagnostic(
                        diagnostic, VXML_INVALID_STRUCTURE,
                        row->location,
                        "VoiceXML QuickJS data namelist token is invalid");
                    return VXML_INVALID_STRUCTURE;
                }
                ++field;
            }
            {
                const char *extra = NULL;
                size_t extra_size = 0u;
                if (!quickjs_next_namelist_token(
                        row->namelist, row->namelist_size,
                        &cursor, &extra, &extra_size) ||
                    extra != NULL) {
                    quickjs_compile_diagnostic(
                        diagnostic, VXML_INVALID_STRUCTURE,
                        row->location,
                        "VoiceXML data namelist count is inconsistent");
                    return VXML_INVALID_STRUCTURE;
                }
            }
        }
        if (row->fetch_policy.fetchaudio_uri != NULL &&
            (!program_view_valid(
                 impl,
                 row->fetch_policy.fetchaudio_uri,
                 row->fetch_policy.fetchaudio_uri_size) ||
             row->fetch_policy.fetchaudio_uri_size >
                 options->max_data_uri_bytes)) {
            status =
                row->fetch_policy.fetchaudio_uri_size >
                    options->max_data_uri_bytes
                ? VXML_LIMIT_EXCEEDED
                : VXML_INVALID_STRUCTURE;
            quickjs_compile_diagnostic(
                diagnostic, status, row->location,
                status == VXML_LIMIT_EXCEEDED
                    ? "VoiceXML data fetchaudio exceeds max_data_uri_bytes"
                    : "VoiceXML data fetchaudio view is invalid");
            return status;
        }
    }

    for (index = 0u; index < impl->action_count; ++index) {
        const vxml_action_row *action = &impl->actions[index];
        if (action->kind != VXML_ACTION_DATA)
            continue;
        if (action->data_index >= impl->data_row_count ||
            impl->data_rows[action->data_index].placement !=
                VXML_DATA_EXECUTABLE) {
            quickjs_compile_diagnostic(
                diagnostic, VXML_INVALID_STRUCTURE,
                action->data_index < impl->data_row_count
                    ? impl->data_rows[action->data_index].location
                    : (salts_xml_location){0},
                "VoiceXML executable data action is invalid");
            return VXML_INVALID_STRUCTURE;
        }
    }
    return VXML_OK;
}

static bool execution_request_valid(
    const vxml_quickjs_script_execution_v1 *execution) {
    const size_t prefix =
        offsetof(vxml_quickjs_script_execution_v1,
                 base_document_uri_size) +
        sizeof(execution->base_document_uri_size);
    return execution != NULL &&
        execution->abi_version ==
            VXML_QUICKJS_SCRIPT_EXECUTION_ABI_V1 &&
        execution->struct_size >= prefix &&
        execution->resolver != NULL &&
        execution->script_resources != NULL &&
        execution->script_resources->abi_version ==
            VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1 &&
        execution->script_resources->struct_size >=
            offsetof(vxml_script_resource_adapter_v1, close) +
                sizeof(execution->script_resources->close) &&
        execution->script_resources->open != NULL &&
        execution->script_resources->close != NULL &&
        (execution->base_document_uri_size == 0u ||
         (execution->base_document_uri != NULL &&
          memchr(
              execution->base_document_uri, '\0',
              execution->base_document_uri_size) == NULL));
}

static vxml_status script_resource_status_to_vxml(
    vxml_script_resource_status status) {
    switch (status) {
    case VXML_SCRIPT_RESOURCE_OK:
        return VXML_OK;
    case VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED:
        return VXML_ALLOCATION_FAILED;
    case VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED:
        return VXML_LIMIT_EXCEEDED;
    case VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT:
        return VXML_INVALID_ARGUMENT;
    case VXML_SCRIPT_RESOURCE_INVALID_URI:
    case VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET:
    case VXML_SCRIPT_RESOURCE_PROVIDER_ERROR:
    case VXML_SCRIPT_RESOURCE_INVALID_DATA:
    default:
        return VXML_SEMANTIC_ERROR;
    }
}

static vxml_status quickjs_prepare_committed_context(
    vxml_session_impl *session,
    quickjs_cmeta_bridge *bridge) {
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    quickjs_cmeta_limits limits;
    quickjs_sandbox_status sandbox_status;
    if (session == NULL || bridge == NULL ||
        session->program == NULL ||
        session->program->profile_data == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program_data =
        (const vxml_quickjs_program_data *)
            session->program->profile_data;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (!compile_options_state_enabled(&program_data->options) ||
        !data->committed_root.live ||
        data->committed_root.value == NULL)
        return VXML_INVALID_CONTRACT;
    sandbox_status = quickjs_sandbox_context_recreate(
        &data->runtime, NULL, 0u);
    if (sandbox_status != QUICKJS_SANDBOX_OK)
        return sandbox_status_to_vxml(sandbox_status);
    limits = cmeta_limits(&program_data->options);
    if (!quickjs_cmeta_bridge_init(
            bridge, &data->runtime,
            program_data->options.root,
            &limits, NULL, NULL))
        return VXML_INVALID_CONTRACT;
    bridge->properties = 0u;
    if (!quickjs_cmeta_import_root(
            bridge, data->committed_root.value))
        return VXML_SEMANTIC_ERROR;
    return VXML_OK;
}


static vxml_status quickjs_prepare_data_context(
    vxml_session_impl *session) {
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    quickjs_sandbox_status sandbox_status;
    if (session == NULL || session->program == NULL ||
        session->program->profile_data == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program_data =
        (const vxml_quickjs_program_data *)
            session->program->profile_data;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (compile_options_state_enabled(&program_data->options)) {
        quickjs_cmeta_bridge bridge = {0};
        return quickjs_prepare_committed_context(
            session, &bridge);
    }
    sandbox_status = quickjs_sandbox_context_recreate(
        &data->runtime, NULL, 0u);
    return sandbox_status_to_vxml(sandbox_status);
}

static vxml_status quickjs_prepare_data_request(
    vxml_session_impl *session,
    const vxml_data_row *row,
    const char **out_uri,
    size_t *out_uri_size,
    const vxml_cmeta_data_field_v1 **out_fields,
    size_t *out_field_count) {
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    const char *uri = NULL;
    size_t uri_size = 0u;
    size_t field_index = 0u;
    size_t cursor = 0u;
    size_t value_bytes_used = 0u;
    bool context_ready = false;
    vxml_status status;

    if (out_uri != NULL) *out_uri = NULL;
    if (out_uri_size != NULL) *out_uri_size = 0u;
    if (out_fields != NULL) *out_fields = NULL;
    if (out_field_count != NULL) *out_field_count = 0u;
    if (session == NULL || row == NULL ||
        out_uri == NULL || out_uri_size == NULL ||
        out_fields == NULL || out_field_count == NULL ||
        session->program == NULL ||
        session->program->profile_data == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program_data =
        (const vxml_quickjs_program_data *)
            session->program->profile_data;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (!compile_options_data_enabled(&program_data->options) ||
        !data->has_data_resources ||
        row->namelist_count > data->data_field_capacity)
        return VXML_INVALID_CONTRACT;

    if (row->uri_expression != NULL ||
        row->namelist_count != 0u) {
        status = quickjs_prepare_data_context(session);
        if (status != VXML_OK)
            return status;
        context_ready = true;
    }

    if (row->uri_expression != NULL) {
        const char *value = NULL;
        size_t value_size = 0u;
        char diagnostic[VXML_DIAGNOSTIC_CAPACITY] = {0};
        quickjs_sandbox_status sandbox_status;
        if (!program_view_valid(
                session->program,
                row->uri_expression,
                row->uri_expression_size))
            return VXML_INVALID_STRUCTURE;
        sandbox_status = quickjs_sandbox_eval_expression_string(
            &data->runtime,
            row->uri_expression,
            row->uri_expression_size,
            "<voicexml-data-srcexpr>",
            program_data->options.max_eval_milliseconds,
            &value, &value_size,
            diagnostic, sizeof(diagnostic));
        status = sandbox_status_to_vxml(sandbox_status);
        if (status != VXML_OK)
            return status;
        if (value == NULL || value_size == 0u ||
            value_size > program_data->options.max_data_uri_bytes ||
            value_size >= data->dynamic_uri_capacity ||
            memchr(value, '\0', value_size) != NULL)
            return value_size >
                    program_data->options.max_data_uri_bytes
                ? VXML_LIMIT_EXCEEDED
                : VXML_SEMANTIC_ERROR;
        memcpy(data->dynamic_uri, value, value_size);
        data->dynamic_uri[value_size] = '\0';
        uri = data->dynamic_uri;
        uri_size = value_size;
    } else {
        if (!program_view_valid(
                session->program, row->uri, row->uri_size) ||
            row->uri_size >
                program_data->options.max_data_uri_bytes)
            return row->uri_size >
                    program_data->options.max_data_uri_bytes
                ? VXML_LIMIT_EXCEEDED
                : VXML_INVALID_STRUCTURE;
        uri = row->uri;
        uri_size = row->uri_size;
    }

    if (row->namelist_count != 0u && !context_ready)
        return VXML_INVALID_CONTRACT;
    while (field_index < row->namelist_count) {
        const char *name = NULL;
        size_t name_size = 0u;
        const char *value = NULL;
        size_t value_size = 0u;
        char *owned_value;
        char diagnostic[VXML_DIAGNOSTIC_CAPACITY] = {0};
        quickjs_sandbox_status sandbox_status;
        if (!quickjs_next_namelist_token(
                row->namelist, row->namelist_size,
                &cursor, &name, &name_size) ||
            name == NULL || name_size == 0u)
            return VXML_INVALID_STRUCTURE;
        sandbox_status =
            quickjs_sandbox_get_global_scalar_string(
                &data->runtime,
                name, name_size,
                program_data->options.max_eval_milliseconds,
                &value, &value_size,
                diagnostic, sizeof(diagnostic));
        status = sandbox_status_to_vxml(sandbox_status);
        if (status != VXML_OK)
            return status;
        if (value == NULL ||
            value_bytes_used > data->data_values_bytes ||
            value_size >
                data->data_values_bytes - value_bytes_used)
            return value != NULL
                ? VXML_LIMIT_EXCEEDED
                : VXML_SEMANTIC_ERROR;
        owned_value =
            data->data_values + value_bytes_used;
        if (value_size != 0u)
            memcpy(owned_value, value, value_size);
        data->data_fields[field_index] =
            (vxml_cmeta_data_field_v1){
                .name = name,
                .name_size = name_size,
                .value = owned_value,
                .value_size = value_size};
        value_bytes_used += value_size;
        ++field_index;
    }
    if (row->namelist_count != 0u) {
        const char *extra = NULL;
        size_t extra_size = 0u;
        if (!quickjs_next_namelist_token(
                row->namelist, row->namelist_size,
                &cursor, &extra, &extra_size) ||
            extra != NULL)
            return VXML_INVALID_STRUCTURE;
    }

    *out_uri = uri;
    *out_uri_size = uri_size;
    *out_fields =
        field_index != 0u ? data->data_fields : NULL;
    *out_field_count = field_index;
    return VXML_OK;
}

static bool quickjs_data_resource_published(
    const vxml_cmeta_data_resource_v1 *resource) {
    return resource != NULL &&
        (resource->lease != NULL ||
         resource->data != NULL ||
         resource->size != 0u);
}

static void quickjs_close_data_resource_if_published(
    vxml_quickjs_session_data *data,
    vxml_cmeta_data_resource_v1 *resource) {
    if (data == NULL || resource == NULL ||
        !data->has_data_resources ||
        !quickjs_data_resource_published(resource))
        return;
    data->data_resources.close(
        data->data_resource_user, resource);
    *resource = (vxml_cmeta_data_resource_v1){0};
}

static void quickjs_finish_data_fetch_audio(
    bool started,
    vxml_fetch_audio_ticket_v1 *ticket) {
    if (!started || ticket == NULL || ticket->finish == NULL)
        return;
    ticket->finish(ticket->user);
    *ticket = (vxml_fetch_audio_ticket_v1){0};
}

static vxml_status quickjs_execute_data_row(
    vxml_session_impl *session,
    const vxml_data_row *row) {
    static const char semantic_event[] = "error.semantic";
    static const char badfetch_event[] = "error.badfetch";
    static const char unsupported_name_event[] =
        "error.unsupported.data.name";
    vxml_quickjs_session_data *data;
    const char *uri = NULL;
    size_t uri_size = 0u;
    const vxml_cmeta_data_field_v1 *fields = NULL;
    size_t field_count = 0u;
    vxml_cmeta_data_request_v3 request =
        VXML_CMETA_DATA_REQUEST_V3_INIT;
    vxml_cmeta_data_resource_v1 resource = {0};
    vxml_fetch_audio_ticket_v1 audio_ticket = {0};
    bool audio_started = false;
    vxml_status status;

    if (session == NULL || row == NULL ||
        session->program == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (!data->has_data_resources ||
        !data_resource_adapter_v3_valid(
            &data->data_resources))
        return session_fail(
            session, VXML_INVALID_CONTRACT,
            semantic_event, sizeof(semantic_event) - 1u);

    if (row->name != NULL || row->name_size != 0u)
        return session_fail(
            session, VXML_UNSUPPORTED_FEATURE,
            unsupported_name_event,
            sizeof(unsupported_name_event) - 1u);

    status = quickjs_prepare_data_request(
        session, row,
        &uri, &uri_size,
        &fields, &field_count);
    if (status != VXML_OK)
        return session_fail(
            session, status,
            semantic_event, sizeof(semantic_event) - 1u);

    if (row->fetch_policy.fetchaudio_uri != NULL) {
        vxml_fetch_audio_request_v1 audio_request = {
            .abi_version = VXML_FETCH_AUDIO_REQUEST_ABI_V1,
            .struct_size =
                sizeof(vxml_fetch_audio_request_v1),
            .uri = row->fetch_policy.fetchaudio_uri,
            .uri_size =
                row->fetch_policy.fetchaudio_uri_size,
            .has_delay =
                row->fetch_policy.has_fetchaudio_delay,
            .delay_us =
                row->fetch_policy.fetchaudio_delay_us,
            .has_minimum =
                row->fetch_policy.has_fetchaudio_minimum,
            .minimum_us =
                row->fetch_policy.fetchaudio_minimum_us};
        vxml_fetch_audio_begin_result begin_result;
        if (!data->has_data_fetch_audio ||
            !fetch_audio_adapter_valid(
                &data->data_fetch_audio))
            return session_fail(
                session, VXML_INVALID_CONTRACT,
                badfetch_event, sizeof(badfetch_event) - 1u);
        begin_result = data->data_fetch_audio.begin(
            data->data_fetch_audio_user,
            &audio_request, &audio_ticket);
        if (begin_result == VXML_FETCH_AUDIO_STARTED) {
            if (audio_ticket.finish == NULL)
                return session_fail(
                    session, VXML_INVALID_CONTRACT,
                    badfetch_event, sizeof(badfetch_event) - 1u);
            audio_started = true;
        } else if (begin_result == VXML_FETCH_AUDIO_SKIPPED) {
            if (audio_ticket.finish != NULL ||
                audio_ticket.user != NULL) {
                quickjs_finish_data_fetch_audio(
                    audio_ticket.finish != NULL,
                    &audio_ticket);
                return session_fail(
                    session, VXML_INVALID_CONTRACT,
                    badfetch_event, sizeof(badfetch_event) - 1u);
            }
        } else {
            quickjs_finish_data_fetch_audio(
                audio_ticket.finish != NULL,
                &audio_ticket);
            return session_fail(
                session, VXML_INVALID_CONTRACT,
                badfetch_event, sizeof(badfetch_event) - 1u);
        }
    }

    request.uri = uri;
    request.uri_size = uri_size;
    request.max_bytes = data->max_data_bytes;
    request.has_timeout =
        row->fetch_policy.has_timeout;
    request.timeout_us =
        row->fetch_policy.timeout_us;
    request.fetch_hint =
        row->fetch_policy.fetch_hint;
    request.has_max_age =
        row->fetch_policy.has_max_age;
    request.max_age_seconds =
        row->fetch_policy.max_age_seconds;
    request.has_max_stale =
        row->fetch_policy.has_max_stale;
    request.max_stale_seconds =
        row->fetch_policy.max_stale_seconds;
    request.method = row->method;
    request.enctype = row->enctype;
    request.fields = fields;
    request.field_count = field_count;

    status = data->data_resources.open_v3(
        data->data_resource_user,
        &request, &resource);
    if (status != VXML_OK) {
        quickjs_close_data_resource_if_published(
            data, &resource);
        quickjs_finish_data_fetch_audio(
            audio_started, &audio_ticket);
        return session_fail(
            session,
            status == VXML_INVALID_ARGUMENT
                ? VXML_INVALID_CONTRACT : status,
            badfetch_event, sizeof(badfetch_event) - 1u);
    }

    if (resource.lease == NULL ||
        resource.size > data->max_data_bytes ||
        (resource.size != 0u &&
         resource.data == NULL)) {
        status = resource.size > data->max_data_bytes
            ? VXML_LIMIT_EXCEEDED
            : VXML_INVALID_CONTRACT;
        quickjs_close_data_resource_if_published(
            data, &resource);
        quickjs_finish_data_fetch_audio(
            audio_started, &audio_ticket);
        return session_fail(
            session, status,
            badfetch_event, sizeof(badfetch_event) - 1u);
    }

    quickjs_close_data_resource_if_published(
        data, &resource);
    quickjs_finish_data_fetch_audio(
        audio_started, &audio_ticket);
    return VXML_OK;
}

static vxml_status quickjs_execute_data_initializers(
    vxml_session_impl *session,
    vxml_data_placement placement,
    size_t form_index) {
    const vxml_program_impl *program;
    size_t index;
    if (session == NULL || session->program == NULL)
        return VXML_INVALID_ARGUMENT;
    program = session->program;
    if (program->data_row_count == 0u)
        return VXML_OK;
    if (program->data_rows == NULL)
        return VXML_INVALID_CONTRACT;
    for (index = 0u; index < program->data_row_count; ++index) {
        const vxml_data_row *row = &program->data_rows[index];
        vxml_status status;
        if (row->placement != placement)
            continue;
        if (placement == VXML_DATA_FORM &&
            row->owner_form != form_index)
            continue;
        if (placement == VXML_DATA_DOCUMENT &&
            row->owner_form != SIZE_MAX)
            return VXML_INVALID_CONTRACT;
        status = quickjs_execute_data_row(
            session, row);
        if (status != VXML_OK)
            return status;
    }
    return VXML_OK;
}

static vxml_status quickjs_prepare_script_transaction(
    vxml_session_impl *session,
    const vxml_script_source *source,
    quickjs_cmeta_state_scratch *scratch) {
    static const char semantic_event[] = "error.semantic";
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    quickjs_cmeta_bridge bridge = {0};
    quickjs_sandbox_status sandbox_status;
    vxml_status status;
    if (session == NULL || source == NULL || scratch == NULL ||
        session->program == NULL ||
        session->program->profile_data == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program_data =
        (const vxml_quickjs_program_data *)
            session->program->profile_data;
    data = (vxml_quickjs_session_data *)session->profile_data;
    status = quickjs_prepare_committed_context(
        session, &bridge);
    if (status != VXML_OK)
        return session_fail(
            session, status,
            semantic_event, sizeof(semantic_event) - 1u);
    if (!quickjs_cmeta_state_snapshot(
            scratch,
            program_data->options.root,
            data->committed_root.value,
            program_data->options.max_snapshot_bytes))
        return session_fail(
            session, VXML_ALLOCATION_FAILED,
            semantic_event, sizeof(semantic_event) - 1u);

    if (source->size != 0u) {
        if (source->data == NULL ||
            source->size >
                program_data->options.max_script_source_bytes) {
            quickjs_cmeta_state_scratch_destroy(
                scratch, program_data->options.root);
            return session_fail(
                session, VXML_LIMIT_EXCEEDED,
                semantic_event, sizeof(semantic_event) - 1u);
        }
        sandbox_status = quickjs_sandbox_runtime_eval(
            &data->runtime,
            (const char *)source->data,
            source->size,
            "<voicexml-external-script>",
            program_data->options.max_eval_milliseconds,
            NULL, 0u);
        status = sandbox_status_to_vxml(sandbox_status);
        if (status != VXML_OK) {
            quickjs_cmeta_state_scratch_destroy(
                scratch, program_data->options.root);
            return session_fail(
                session, status,
                semantic_event, sizeof(semantic_event) - 1u);
        }
    }

    bridge.properties = 0u;
    if (!quickjs_cmeta_export_root(
            &bridge, scratch->value)) {
        quickjs_cmeta_state_scratch_destroy(
            scratch, program_data->options.root);
        return session_fail(
            session, VXML_SEMANTIC_ERROR,
            semantic_event, sizeof(semantic_event) - 1u);
    }
    return VXML_OK;
}

static vxml_status project_script_target(
    vxml_session_impl *session,
    const vxml_action_row *action) {
    static const char semantic_event[] = "error.semantic";
    const vxml_program_impl *program;
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    const char *reference = NULL;
    size_t reference_size = 0u;
    char diagnostic[VXML_DIAGNOSTIC_CAPACITY] = {0};
    quickjs_sandbox_status sandbox_status;
    vxml_status status;

    if (session == NULL || action == NULL ||
        session->program == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program = session->program;
    program_data =
        (const vxml_quickjs_program_data *)program->profile_data;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (program_data == NULL ||
        action->kind != VXML_ACTION_SCRIPT_EXTERNAL ||
        !program_view_valid(
            program, action->script_charset,
            action->script_charset_size) ||
        ((action->script_src == NULL) ==
         (action->script_srcexpr == NULL)))
        return session_fail(
            session, VXML_INVALID_STRUCTURE,
            semantic_event, sizeof(semantic_event) - 1u);

    if (action->script_srcexpr != NULL) {
        const char *value = NULL;
        size_t value_size = 0u;
        if (compile_options_state_enabled(&program_data->options)) {
            quickjs_cmeta_bridge bridge = {0};
            status = quickjs_prepare_committed_context(
                session, &bridge);
            if (status != VXML_OK)
                return session_fail(
                    session, status,
                    semantic_event,
                    sizeof(semantic_event) - 1u);
        }
        if (!program_view_valid(
                program, action->script_srcexpr,
                action->script_srcexpr_size))
            return session_fail(
                session, VXML_INVALID_STRUCTURE,
                semantic_event, sizeof(semantic_event) - 1u);
        sandbox_status = quickjs_sandbox_eval_expression_string(
            &data->runtime,
            action->script_srcexpr,
            action->script_srcexpr_size,
            "<voicexml-script-srcexpr>",
            program_data->options.max_eval_milliseconds,
            &value, &value_size,
            diagnostic, sizeof(diagnostic));
        status = sandbox_status_to_vxml(sandbox_status);
        if (status != VXML_OK)
            return session_fail(
                session, status,
                semantic_event, sizeof(semantic_event) - 1u);
        if (value == NULL || value_size == 0u ||
            value_size >
                program_data->options.max_dynamic_script_uri_bytes ||
            value_size >= data->dynamic_uri_capacity ||
            memchr(value, '\0', value_size) != NULL)
            return session_fail(
                session,
                value_size >
                    program_data->options.max_dynamic_script_uri_bytes
                    ? VXML_LIMIT_EXCEEDED
                    : VXML_SEMANTIC_ERROR,
                semantic_event, sizeof(semantic_event) - 1u);
        memcpy(data->dynamic_uri, value, value_size);
        data->dynamic_uri[value_size] = '\0';
        reference = data->dynamic_uri;
        reference_size = value_size;
    } else {
        if (!program_view_valid(
                program, action->script_src,
                action->script_src_size))
            return session_fail(
                session, VXML_INVALID_STRUCTURE,
                semantic_event, sizeof(semantic_event) - 1u);
        reference = action->script_src;
        reference_size = action->script_src_size;
    }

    session->script_src = reference;
    session->script_src_size = reference_size;
    session->script_charset = action->script_charset;
    session->script_charset_size = action->script_charset_size;
    session->state = VXML_SESSION_SCRIPTING;
    return VXML_OK;
}

static vxml_status quickjs_run_from(
    vxml_session_impl *session,
    size_t form_index,
    size_t first_block) {
    const vxml_program_impl *program;
    vxml_quickjs_session_data *data;
    size_t transitions = 0u;
    size_t next_block = first_block;
    vxml_status status;
    if (session == NULL || session->program == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program = session->program;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (program->forms == NULL || program->form_count == 0u)
        return session_fail(
            session, VXML_INVALID_STRUCTURE, NULL, 0u);
    if (form_index >= program->form_count)
        return session_fail(
            session, VXML_INVALID_STRUCTURE, NULL, 0u);

    if (!data->document_data_done) {
        status = quickjs_execute_data_initializers(
            session, VXML_DATA_DOCUMENT, SIZE_MAX);
        if (status != VXML_OK)
            return status;
        data->document_data_done = true;
    }

    for (;;) {
        const vxml_form_row *form;
        size_t block_index;
        size_t form_end;
        bool jumped = false;
        if (form_index >= program->form_count)
            return session_fail(
                session, VXML_INVALID_STRUCTURE, NULL, 0u);
        form = &program->forms[form_index];
        if (form->block_count == 0u ||
            program->blocks == NULL ||
            !range_valid(
                form->first_block, form->block_count,
                program->block_count))
            return session_fail(
                session, VXML_INVALID_STRUCTURE, NULL, 0u);
        form_end = form->first_block + form->block_count;
        if (next_block == SIZE_MAX) {
            status = quickjs_execute_data_initializers(
                session, VXML_DATA_FORM, form_index);
            if (status != VXML_OK)
                return status;
            next_block = form->first_block;
        }
        if (next_block < form->first_block ||
            next_block > form_end)
            return session_fail(
                session, VXML_INVALID_STRUCTURE, NULL, 0u);

        for (block_index = next_block;
             block_index < form_end;
             ++block_index) {
            const vxml_block_row *block =
                &program->blocks[block_index];
            const vxml_action_row *action;
            if (block->action_count > 1u ||
                !range_valid(
                    block->first_action,
                    block->action_count,
                    program->action_count) ||
                (block->action_count != 0u &&
                 program->actions == NULL))
                return session_fail(
                    session, VXML_INVALID_STRUCTURE, NULL, 0u);
            if (block->action_count == 0u)
                continue;
            action = &program->actions[block->first_action];

            if (action->kind == VXML_ACTION_DATA) {
                if (action->data_index >=
                        program->data_row_count ||
                    program->data_rows == NULL ||
                    program->data_rows[
                        action->data_index].placement !=
                        VXML_DATA_EXECUTABLE ||
                    program->data_rows[
                        action->data_index].owner_form !=
                        form_index)
                    return session_fail(
                        session, VXML_INVALID_STRUCTURE,
                        NULL, 0u);
                status = quickjs_execute_data_row(
                    session,
                    &program->data_rows[action->data_index]);
                if (status != VXML_OK)
                    return status;
                continue;
            }

            if (action->kind == VXML_ACTION_SCRIPT_EXTERNAL) {
                data->resume_form = form_index;
                data->resume_block = block_index + 1u;
                data->script_pending = true;
                status = project_script_target(session, action);
                if (status != VXML_OK) {
                    data->resume_form = SIZE_MAX;
                    data->resume_block = SIZE_MAX;
                    data->script_pending = false;
                }
                return status;
            }

            if (action->kind == VXML_ACTION_EXIT) {
                session->state = VXML_SESSION_EXITED;
                return VXML_OK;
            }
            if (action->kind == VXML_ACTION_SUBMIT) {
                if (!program_view_valid(
                        program, action->target_uri,
                        action->target_uri_size) ||
                    (action->submit_method != VXML_SUBMIT_METHOD_GET &&
                     action->submit_method != VXML_SUBMIT_METHOD_POST) ||
                    action->submit_enctype !=
                        VXML_SUBMIT_ENCTYPE_URLENCODED)
                    return session_fail(
                        session, VXML_INVALID_STRUCTURE, NULL, 0u);
                session->submit_uri = action->target_uri;
                session->submit_uri_size = action->target_uri_size;
                session->submit_method = action->submit_method;
                session->submit_enctype = action->submit_enctype;
                session->submit_fields = NULL;
                session->submit_field_count = 0u;
                session->submit_has_timeout = false;
                session->submit_timeout_us = UINT64_C(0);
                session->submit_fetchaudio_uri = NULL;
                session->submit_fetchaudio_uri_size = 0u;
                session->submit_has_fetchaudio_delay = false;
                session->submit_fetchaudio_delay_us = UINT64_C(0);
                session->submit_has_fetchaudio_minimum = false;
                session->submit_fetchaudio_minimum_us = UINT64_C(0);
                session->state = VXML_SESSION_SUBMITTING;
                return VXML_OK;
            }
            if (action->kind == VXML_ACTION_GOTO_EXTERNAL) {
                if (!program_view_valid(
                        program, action->target_uri,
                        action->target_uri_size) ||
                    ((action->fetchaudio_uri == NULL) !=
                     (action->fetchaudio_uri_size == 0u)) ||
                    (action->fetchaudio_uri != NULL &&
                     !program_view_valid(
                         program,
                         action->fetchaudio_uri,
                         action->fetchaudio_uri_size)))
                    return session_fail(
                        session, VXML_INVALID_STRUCTURE, NULL, 0u);
                session->navigation_uri = action->target_uri;
                session->navigation_uri_size = action->target_uri_size;
                session->navigation_fetchaudio_uri =
                    action->fetchaudio_uri;
                session->navigation_fetchaudio_uri_size =
                    action->fetchaudio_uri_size;
                session->navigation_has_fetchaudio_delay = false;
                session->navigation_fetchaudio_delay_us = UINT64_C(0);
                session->navigation_has_fetchaudio_minimum = false;
                session->navigation_fetchaudio_minimum_us = UINT64_C(0);
                session->state = VXML_SESSION_NAVIGATING;
                return VXML_OK;
            }
            if (action->kind != VXML_ACTION_GOTO ||
                action->target_form >= program->form_count)
                return session_fail(
                    session, VXML_INVALID_STRUCTURE, NULL, 0u);
            if (transitions >= program->form_count)
                return session_fail(
                    session, VXML_INVALID_STRUCTURE, NULL, 0u);
            ++transitions;
            form_index = action->target_form;
            next_block = SIZE_MAX;
            jumped = true;
            break;
        }

        if (!jumped) {
            session->state = VXML_SESSION_EXITED;
            return VXML_OK;
        }
    }
}

static vxml_status quickjs_run_at(
    vxml_session_impl *session, size_t form_index) {
    return quickjs_run_from(session, form_index, SIZE_MAX);
}

static void quickjs_session_data_release(
    const vxml_quickjs_program_data *program_data,
    vxml_quickjs_session_data *data) {
    if (data == NULL) return;
    if (program_data != NULL &&
        compile_options_state_enabled(&program_data->options))
        quickjs_cmeta_state_scratch_destroy(
            &data->committed_root,
            program_data->options.root);
    quickjs_sandbox_runtime_destroy(&data->runtime);
    vxml_free(data->data_values);
    vxml_free(data->data_fields);
    vxml_free(data->dynamic_uri);
    vxml_free(data);
}

static vxml_status quickjs_profile_session_init(
    vxml_session_impl *session, const void *options_value) {
    const vxml_quickjs_session_options_v1 *options =
        (const vxml_quickjs_session_options_v1 *)options_value;
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data = NULL;
    quickjs_sandbox_options sandbox;
    quickjs_sandbox_status sandbox_status;
    size_t dynamic_uri_bytes;
    size_t max_fields = 0u;
    bool has_data;

    if (session == NULL || session->program == NULL ||
        session->program->profile_data == NULL ||
        !session_options_valid(options))
        return VXML_INVALID_ARGUMENT;
    program_data =
        (const vxml_quickjs_program_data *)session->program->profile_data;
    if (!compile_options_valid(&program_data->options))
        return VXML_INVALID_CONTRACT;
    if (compile_options_state_enabled(&program_data->options) !=
        (session_initial_state(options) != NULL))
        return VXML_INVALID_ARGUMENT;

    has_data = session->program->data_row_count != 0u;
    if (has_data) {
        if (!compile_options_data_enabled(&program_data->options) ||
            session->program->data_rows == NULL ||
            !session_options_has_data_tail(options) ||
            !data_resource_adapter_v3_valid(options->data_resources) ||
            options->max_data_bytes == 0u ||
            options->max_data_bytes == SIZE_MAX ||
            options->max_data_request_value_bytes == 0u ||
            options->max_data_request_value_bytes == SIZE_MAX)
            return VXML_INVALID_ARGUMENT;
        if (program_requires_data_fetch_audio(session->program) &&
            !fetch_audio_adapter_valid(options->data_fetch_audio))
            return VXML_INVALID_ARGUMENT;
        max_fields = program_max_data_fields(session->program);
        if (max_fields >
            program_data->options.max_data_namelist_fields)
            return VXML_INVALID_CONTRACT;
    }

    dynamic_uri_bytes =
        program_data->options.max_dynamic_script_uri_bytes;
    if (compile_options_data_enabled(&program_data->options) &&
        program_data->options.max_data_uri_bytes > dynamic_uri_bytes)
        dynamic_uri_bytes =
            program_data->options.max_data_uri_bytes;
    if (dynamic_uri_bytes == SIZE_MAX)
        return VXML_INVALID_CONTRACT;

    data = (vxml_quickjs_session_data *)vxml_calloc(1u, sizeof(*data));
    if (data == NULL)
        return VXML_ALLOCATION_FAILED;
    data->dynamic_uri =
        (char *)vxml_malloc(dynamic_uri_bytes + 1u);
    if (data->dynamic_uri == NULL) {
        quickjs_session_data_release(program_data, data);
        return VXML_ALLOCATION_FAILED;
    }
    data->dynamic_uri_capacity = dynamic_uri_bytes + 1u;
    data->resume_form = SIZE_MAX;
    data->resume_block = SIZE_MAX;
    data->script_pending = false;
    data->document_data_done = false;

    if (has_data) {
        data->data_resources = *options->data_resources;
        data->data_resource_user = options->data_resource_user;
        data->has_data_resources = true;
        data->max_data_bytes = options->max_data_bytes;
        data->max_data_request_value_bytes =
            options->max_data_request_value_bytes;
        data->data_field_capacity = max_fields;
        if (max_fields != 0u) {
            data->data_fields =
                (vxml_cmeta_data_field_v1 *)vxml_calloc(
                    max_fields, sizeof(*data->data_fields));
            data->data_values_bytes =
                options->max_data_request_value_bytes;
            data->data_values =
                (char *)vxml_malloc(data->data_values_bytes);
            if (data->data_fields == NULL ||
                data->data_values == NULL) {
                quickjs_session_data_release(
                    program_data, data);
                return VXML_ALLOCATION_FAILED;
            }
        }
        if (options->data_fetch_audio != NULL) {
            if (!fetch_audio_adapter_valid(
                    options->data_fetch_audio)) {
                quickjs_session_data_release(
                    program_data, data);
                return VXML_INVALID_ARGUMENT;
            }
            data->data_fetch_audio =
                *options->data_fetch_audio;
            data->data_fetch_audio_user =
                options->data_fetch_audio_user;
            data->has_data_fetch_audio = true;
        }
    }

    if (compile_options_state_enabled(&program_data->options) &&
        !quickjs_cmeta_state_snapshot(
            &data->committed_root,
            program_data->options.root,
            session_initial_state(options),
            program_data->options.max_snapshot_bytes)) {
        quickjs_session_data_release(program_data, data);
        return VXML_ALLOCATION_FAILED;
    }

    sandbox = sandbox_options(&program_data->options);
    if (has_data &&
        data->max_data_request_value_bytes >
            sandbox.max_string_bytes)
        sandbox.max_string_bytes =
            data->max_data_request_value_bytes;
    sandbox_status = quickjs_sandbox_runtime_init(
        &data->runtime, &sandbox, NULL, 0u);
    if (sandbox_status != QUICKJS_SANDBOX_OK) {
        const vxml_status status =
            sandbox_status_to_vxml(sandbox_status);
        quickjs_session_data_release(program_data, data);
        return status;
    }
    session->profile_data = data;
    return VXML_OK;
}

static vxml_status quickjs_profile_session_start(
    vxml_session_impl *session) {
    if (session == NULL || session->profile_data == NULL)
        return VXML_INVALID_STATE;
    session_set_event(
        (vxml_quickjs_session_data *)session->profile_data,
        NULL, 0u);
    return quickjs_run_at(session, 0u);
}

static vxml_status quickjs_profile_session_start_at(
    vxml_session_impl *session, size_t form_index) {
    if (session == NULL || session->profile_data == NULL)
        return VXML_INVALID_STATE;
    session_set_event(
        (vxml_quickjs_session_data *)session->profile_data,
        NULL, 0u);
    return quickjs_run_at(session, form_index);
}

static void quickjs_profile_session_destroy(
    vxml_session_impl *session) {
    const vxml_quickjs_program_data *program_data = NULL;
    vxml_quickjs_session_data *data;
    if (session == NULL || session->profile_data == NULL)
        return;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (session->program != NULL &&
        session->program->profile_data != NULL)
        program_data =
            (const vxml_quickjs_program_data *)
                session->program->profile_data;
    quickjs_session_data_release(program_data, data);
    session->profile_data = NULL;
}

static void quickjs_profile_program_destroy(
    vxml_program_impl *program) {
    if (program == NULL) return;
    vxml_free(program->profile_data);
    program->profile_data = NULL;
}

vxml_quickjs_compile_options_v1
vxml_quickjs_default_compile_options(void) {
    return (vxml_quickjs_compile_options_v1){
        .abi_version = VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_quickjs_compile_options_v1),
        .max_expression_bytes = VXML_QUICKJS_DEFAULT_EXPRESSION_BYTES,
        .max_dynamic_script_uri_bytes = VXML_QUICKJS_DEFAULT_URI_BYTES,
        .max_heap_bytes = VXML_QUICKJS_DEFAULT_HEAP_BYTES,
        .max_stack_bytes = VXML_QUICKJS_DEFAULT_STACK_BYTES,
        .max_eval_milliseconds =
            VXML_QUICKJS_DEFAULT_EVAL_MILLISECONDS,
        .root = NULL,
        .max_conversion_depth =
            VXML_QUICKJS_DEFAULT_CONVERSION_DEPTH,
        .max_properties =
            VXML_QUICKJS_DEFAULT_PROPERTIES,
        .max_array_items =
            VXML_QUICKJS_DEFAULT_ARRAY_ITEMS,
        .max_snapshot_bytes =
            VXML_QUICKJS_DEFAULT_SNAPSHOT_BYTES,
        .max_state_string_bytes =
            VXML_QUICKJS_DEFAULT_STATE_STRING_BYTES,
        .max_resolved_script_uri_bytes =
            VXML_QUICKJS_DEFAULT_URI_BYTES,
        .max_script_source_bytes =
            VXML_QUICKJS_DEFAULT_SCRIPT_SOURCE_BYTES,
        .max_data_rows =
            VXML_QUICKJS_DEFAULT_DATA_ROWS,
        .max_data_uri_bytes =
            VXML_QUICKJS_DEFAULT_URI_BYTES,
        .max_data_namelist_fields =
            VXML_QUICKJS_DEFAULT_DATA_NAMELIST_FIELDS};
}

vxml_quickjs_session_options_v1
vxml_quickjs_default_session_options(void) {
    return (vxml_quickjs_session_options_v1){
        .abi_version = VXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_quickjs_session_options_v1),
        .initial_state = NULL,
        .data_resources = NULL,
        .data_resource_user = NULL,
        .max_data_bytes = VXML_QUICKJS_DEFAULT_DATA_BYTES,
        .max_data_request_value_bytes =
            VXML_QUICKJS_DEFAULT_DATA_VALUE_BYTES,
        .data_fetch_audio = NULL,
        .data_fetch_audio_user = NULL};
}

vxml_status vxml_compile_quickjs_script_profile(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_quickjs_compile_options_v1 *options,
    vxml_program *out,
    vxml_diagnostic *diagnostic) {
    vxml_quickjs_compile_options_v1 active;
    vxml_quickjs_program_data *profile = NULL;
    vxml_program_impl *impl;
    vxml_status status;

    if (out != NULL) out->impl = NULL;
    if (diagnostic != NULL)
        memset(diagnostic, 0, sizeof(*diagnostic));
    if (options == NULL) {
        active = vxml_quickjs_default_compile_options();
    } else {
        size_t copy_size;
        memset(&active, 0, sizeof(active));
        if (options->struct_size < compile_options_v1_prefix_size())
            return VXML_INVALID_ARGUMENT;
        copy_size = options->struct_size < sizeof(active)
            ? options->struct_size : sizeof(active);
        memcpy(&active, options, copy_size);
    }
    if (!compile_options_valid(&active))
        return VXML_INVALID_ARGUMENT;

    {
        uint64_t features =
            VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT |
            VXML_COMPILE_FEATURE_SCRIPT_SRCEXPR;
        if (compile_options_data_enabled(&active))
            features |= VXML_COMPILE_FEATURE_DATA_REQUEST;
        status = vxml_compile_with_features(
            bytes, size, limits,
            features,
            out, diagnostic);
    }
    if (status != VXML_OK)
        return status;
    status = validate_dynamic_scripts(
        out, &active, diagnostic);
    if (status != VXML_OK) {
        vxml_program_destroy(out);
        return status;
    }
    status = validate_data_rows(
        out, &active, diagnostic);
    if (status != VXML_OK) {
        vxml_program_destroy(out);
        return status;
    }
    if (compile_options_state_enabled(&active)) {
        const quickjs_cmeta_limits limits = cmeta_limits(&active);
        if (!quickjs_cmeta_schema_supported(
                active.root, &limits, NULL, NULL, 0u)) {
            if (diagnostic != NULL) {
                memset(diagnostic, 0, sizeof(*diagnostic));
                diagnostic->status = VXML_INVALID_CONTRACT;
                (void)snprintf(
                    diagnostic->message,
                    sizeof(diagnostic->message),
                    "%s",
                    "VoiceXML QuickJS CMeta root is outside the bounded bridge contract");
            }
            vxml_program_destroy(out);
            return VXML_INVALID_CONTRACT;
        }
    }

    profile = (vxml_quickjs_program_data *)vxml_malloc(sizeof(*profile));
    if (profile == NULL) {
        vxml_program_destroy(out);
        return VXML_ALLOCATION_FAILED;
    }
    profile->options = active;
    impl = (vxml_program_impl *)out->impl;
    impl->profile_kind = VXML_PROFILE_QUICKJS;
    impl->profile_data = profile;
    impl->profile_session_init = quickjs_profile_session_init;
    impl->profile_session_start = quickjs_profile_session_start;
    impl->profile_session_start_at = quickjs_profile_session_start_at;
    impl->profile_session_raise_event = NULL;
    impl->profile_session_destroy = quickjs_profile_session_destroy;
    impl->profile_program_destroy = quickjs_profile_program_destroy;
    return VXML_OK;
}

vxml_status vxml_session_init_quickjs(
    vxml_session *session,
    const vxml_program *program,
    const vxml_quickjs_session_options_v1 *options) {
    const vxml_program_impl *impl;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (program == NULL || program->impl == NULL ||
        !session_options_valid(options))
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_program_impl *)program->impl;
    if (impl->profile_kind != VXML_PROFILE_QUICKJS ||
        impl->profile_session_init != quickjs_profile_session_init)
        return VXML_INVALID_CONTRACT;
    return vxml_session_init_profile(session, program, options);
}

vxml_status vxml_quickjs_session_execute_script(
    vxml_session *session,
    const vxml_quickjs_script_execution_v1 *execution) {
    static const char semantic_event[] = "error.semantic";
    vxml_session_impl *impl;
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    vxml_script_request_v1 request =
        VXML_SCRIPT_REQUEST_V1_INIT;
    vxml_script_source source = {0};
    quickjs_cmeta_state_scratch scratch = {0};
    vxml_script_resource_status resource_status;
    vxml_script_resource_status close_status;
    vxml_status status;
    const char *event = NULL;
    size_t event_size = 0u;
    size_t resume_form;
    size_t resume_block;

    if (session == NULL ||
        !execution_request_valid(execution))
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_INVALID_STATE;
    if (impl->state != VXML_SESSION_SCRIPTING)
        return VXML_INVALID_STATE;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_QUICKJS ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    program_data =
        (const vxml_quickjs_program_data *)
            impl->program->profile_data;
    data = (vxml_quickjs_session_data *)impl->profile_data;
    if (!compile_options_state_enabled(&program_data->options) ||
        !data->script_pending ||
        data->resume_form == SIZE_MAX ||
        data->resume_block == SIZE_MAX ||
        impl->script_src == NULL ||
        impl->script_src_size == 0u ||
        impl->script_charset == NULL ||
        impl->script_charset_size == 0u)
        return VXML_INVALID_CONTRACT;

    request.base_document_uri =
        execution->base_document_uri;
    request.base_document_uri_size =
        execution->base_document_uri_size;
    request.reference = impl->script_src;
    request.reference_size = impl->script_src_size;
    request.charset = impl->script_charset;
    request.charset_size = impl->script_charset_size;
    request.max_uri_bytes =
        program_data->options.max_resolved_script_uri_bytes;
    request.max_source_bytes =
        program_data->options.max_script_source_bytes;

    resource_status = vxml_script_resource_acquire(
        execution->resolver,
        execution->script_resources,
        execution->script_resource_user,
        &request, &source);
    if (resource_status != VXML_SCRIPT_RESOURCE_OK) {
        status = script_resource_status_to_vxml(
            resource_status);
        if (resource_status ==
            VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT)
            return VXML_INVALID_ARGUMENT;
        event = vxml_script_resource_failure_event(
            resource_status, &event_size);
        return session_fail(
            impl, status, event, event_size);
    }

    status = quickjs_prepare_script_transaction(
        impl, &source, &scratch);

    close_status = vxml_script_resource_close(
        execution->script_resources,
        execution->script_resource_user,
        &source);
    if (status != VXML_OK) {
        if (source.lease != NULL)
            (void)vxml_script_resource_close(
                execution->script_resources,
                execution->script_resource_user,
                &source);
        return status;
    }
    if (close_status != VXML_SCRIPT_RESOURCE_OK) {
        quickjs_cmeta_state_scratch_destroy(
            &scratch, program_data->options.root);
        return session_fail(
            impl, VXML_INVALID_CONTRACT,
            semantic_event, sizeof(semantic_event) - 1u);
    }

    if (!quickjs_cmeta_state_publish(
            program_data->options.root,
            data->committed_root.value,
            &scratch)) {
        quickjs_cmeta_state_scratch_destroy(
            &scratch, program_data->options.root);
        return session_fail(
            impl, VXML_INVALID_CONTRACT,
            semantic_event, sizeof(semantic_event) - 1u);
    }
    quickjs_cmeta_state_scratch_destroy(
        &scratch, program_data->options.root);

    resume_form = data->resume_form;
    resume_block = data->resume_block;
    data->resume_form = SIZE_MAX;
    data->resume_block = SIZE_MAX;
    data->script_pending = false;
    session_set_event(data, NULL, 0u);
    impl->script_src = NULL;
    impl->script_src_size = 0u;
    impl->script_charset = NULL;
    impl->script_charset_size = 0u;
    impl->state = VXML_SESSION_RUNNING;
    impl->error = VXML_OK;
    return quickjs_run_from(
        impl, resume_form, resume_block);
}

vxml_status vxml_quickjs_session_state(
    const vxml_session *session,
    const void **out_state) {
    const vxml_session_impl *impl;
    const vxml_quickjs_program_data *program_data;
    const vxml_quickjs_session_data *data;
    if (out_state != NULL) *out_state = NULL;
    if (session == NULL || out_state == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_INVALID_STATE;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_QUICKJS ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    program_data =
        (const vxml_quickjs_program_data *)
            impl->program->profile_data;
    data = (const vxml_quickjs_session_data *)
        impl->profile_data;
    if (!compile_options_state_enabled(
            &program_data->options) ||
        !data->committed_root.live ||
        data->committed_root.value == NULL)
        return VXML_INVALID_CONTRACT;
    *out_state = data->committed_root.value;
    return VXML_OK;
}

vxml_status vxml_quickjs_session_last_event(
    const vxml_session *session,
    const char **out_event,
    size_t *out_event_size) {
    const vxml_session_impl *impl;
    const vxml_quickjs_session_data *data;
    if (out_event != NULL) *out_event = NULL;
    if (out_event_size != NULL) *out_event_size = 0u;
    if (session == NULL || out_event == NULL ||
        out_event_size == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_INVALID_STATE;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_QUICKJS ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    data = (const vxml_quickjs_session_data *)impl->profile_data;
    if (data->last_event == NULL ||
        data->last_event_size == 0u)
        return VXML_INVALID_STATE;
    *out_event = data->last_event;
    *out_event_size = data->last_event_size;
    return VXML_OK;
}
