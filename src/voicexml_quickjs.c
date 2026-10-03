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
    VXML_QUICKJS_DEFAULT_SCRIPT_SOURCE_BYTES = 1024u * 1024u
};

typedef struct vxml_quickjs_program_data {
    vxml_quickjs_compile_options_v1 options;
} vxml_quickjs_program_data;

typedef struct vxml_quickjs_session_data {
    quickjs_sandbox_runtime runtime;
    quickjs_cmeta_state_scratch committed_root;
    char *dynamic_uri;
    size_t dynamic_uri_capacity;
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

static size_t session_options_v1_prefix_size(void) {
    return offsetof(vxml_quickjs_session_options_v1, initial_state);
}

static bool compile_options_has_state_tail(
    const vxml_quickjs_compile_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= sizeof(*options);
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
    if (!compile_options_has_state_tail(options))
        return true;
    if (options->max_resolved_script_uri_bytes == 0u ||
        options->max_resolved_script_uri_bytes == SIZE_MAX ||
        options->max_script_source_bytes == 0u)
        return false;
    if (options->root == NULL)
        return true;
    return options->max_conversion_depth != 0u &&
        options->max_properties != 0u &&
        options->max_array_items != 0u &&
        options->max_snapshot_bytes != 0u &&
        options->max_state_string_bytes != 0u &&
        quickjs_cmeta_limits_valid(&(quickjs_cmeta_limits){
            options->max_conversion_depth,
            options->max_properties,
            options->max_array_items,
            options->max_snapshot_bytes,
            options->max_state_string_bytes});
}

static bool session_options_valid(
    const vxml_quickjs_session_options_v1 *options) {
    return options != NULL &&
        options->abi_version == VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= session_options_v1_prefix_size();
}

static const void *session_initial_state(
    const vxml_quickjs_session_options_v1 *options) {
    return options != NULL &&
        options->struct_size >= sizeof(*options)
        ? options->initial_state : NULL;
}

static quickjs_sandbox_options sandbox_options(
    const vxml_quickjs_compile_options_v1 *options) {
    return (quickjs_sandbox_options){
        .max_source_bytes = options->max_expression_bytes,
        .max_string_bytes = options->max_dynamic_script_uri_bytes,
        .max_heap_bytes = options->max_heap_bytes,
        .max_stack_bytes = options->max_stack_bytes,
        .max_eval_milliseconds = options->max_eval_milliseconds};
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
    if (session == NULL || session->program == NULL ||
        session->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program = session->program;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (program->forms == NULL || program->form_count == 0u)
        return session_fail(
            session, VXML_INVALID_STRUCTURE, NULL, 0u);

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
        if (next_block == SIZE_MAX)
            next_block = form->first_block;
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

            if (action->kind == VXML_ACTION_SCRIPT_EXTERNAL) {
                vxml_status status;
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

static vxml_status quickjs_profile_session_init(
    vxml_session_impl *session, const void *options_value) {
    const vxml_quickjs_session_options_v1 *options =
        (const vxml_quickjs_session_options_v1 *)options_value;
    const vxml_quickjs_program_data *program_data;
    vxml_quickjs_session_data *data;
    quickjs_sandbox_options sandbox;
    quickjs_sandbox_status sandbox_status;

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

    data = (vxml_quickjs_session_data *)vxml_calloc(1u, sizeof(*data));
    if (data == NULL)
        return VXML_ALLOCATION_FAILED;
    data->dynamic_uri =
        (char *)vxml_malloc(
            program_data->options.max_dynamic_script_uri_bytes + 1u);
    if (data->dynamic_uri == NULL) {
        vxml_free(data);
        return VXML_ALLOCATION_FAILED;
    }
    data->dynamic_uri_capacity =
        program_data->options.max_dynamic_script_uri_bytes + 1u;
    data->resume_form = SIZE_MAX;
    data->resume_block = SIZE_MAX;
    data->script_pending = false;

    if (compile_options_state_enabled(&program_data->options) &&
        !quickjs_cmeta_state_snapshot(
            &data->committed_root,
            program_data->options.root,
            session_initial_state(options),
            program_data->options.max_snapshot_bytes)) {
        vxml_free(data->dynamic_uri);
        vxml_free(data);
        return VXML_ALLOCATION_FAILED;
    }

    sandbox = sandbox_options(&program_data->options);
    sandbox_status = quickjs_sandbox_runtime_init(
        &data->runtime, &sandbox, NULL, 0u);
    if (sandbox_status != QUICKJS_SANDBOX_OK) {
        vxml_status status = sandbox_status_to_vxml(sandbox_status);
        if (compile_options_state_enabled(&program_data->options))
            quickjs_cmeta_state_scratch_destroy(
                &data->committed_root,
                program_data->options.root);
        vxml_free(data->dynamic_uri);
        vxml_free(data);
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
    vxml_quickjs_session_data *data;
    if (session == NULL || session->profile_data == NULL)
        return;
    data = (vxml_quickjs_session_data *)session->profile_data;
    if (session->program != NULL &&
        session->program->profile_data != NULL) {
        const vxml_quickjs_program_data *program_data =
            (const vxml_quickjs_program_data *)
                session->program->profile_data;
        if (compile_options_state_enabled(&program_data->options))
            quickjs_cmeta_state_scratch_destroy(
                &data->committed_root,
                program_data->options.root);
    }
    quickjs_sandbox_runtime_destroy(&data->runtime);
    vxml_free(data->dynamic_uri);
    vxml_free(data);
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
            VXML_QUICKJS_DEFAULT_SCRIPT_SOURCE_BYTES};
}

vxml_quickjs_session_options_v1
vxml_quickjs_default_session_options(void) {
    return (vxml_quickjs_session_options_v1){
        .abi_version = VXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_quickjs_session_options_v1),
        .initial_state = NULL};
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

    status = vxml_compile_with_features(
        bytes, size, limits,
        VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT |
            VXML_COMPILE_FEATURE_SCRIPT_SRCEXPR,
        out, diagnostic);
    if (status != VXML_OK)
        return status;
    status = validate_dynamic_scripts(
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
