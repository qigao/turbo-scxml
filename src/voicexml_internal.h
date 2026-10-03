#ifndef TURBO_VOICEXML_INTERNAL_H
#define TURBO_VOICEXML_INTERNAL_H

#include <voicexml/voicexml.h>
#include <voicexml/data_resource.h>

#define VXML_DEFAULT_MAX_FORMS 64u
#define VXML_DEFAULT_MAX_BLOCKS 1024u
#define VXML_DEFAULT_MAX_ACTIONS 4096u
#define VXML_DEFAULT_MAX_NAME_BYTES (256u * 1024u)

#define VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT UINT64_C(1)
#define VXML_COMPILE_FEATURE_SCRIPT_SRCEXPR UINT64_C(2)
#define VXML_COMPILE_FEATURE_DATA_REQUEST UINT64_C(4)

typedef enum vxml_action_kind {
    VXML_ACTION_EXIT = 1,
    VXML_ACTION_GOTO,
    VXML_ACTION_GOTO_EXTERNAL,
    VXML_ACTION_SUBMIT,
    VXML_ACTION_SCRIPT_EXTERNAL,
    VXML_ACTION_DATA,
    VXML_ACTION_RETURN,
    VXML_ACTION_DISCONNECT
} vxml_action_kind;

typedef struct vxml_action_row {
    vxml_action_kind kind;
    size_t target_form;
    const char *target_uri;
    size_t target_uri_size;
    const char *fetchaudio_uri;
    size_t fetchaudio_uri_size;
    vxml_submit_method submit_method;
    vxml_submit_enctype submit_enctype;
    const char *script_src;
    size_t script_src_size;
    const char *script_srcexpr;
    size_t script_srcexpr_size;
    const char *script_charset;
    size_t script_charset_size;
    salts_xml_location script_location;
    size_t data_index;
} vxml_action_row;

typedef enum vxml_data_placement {
    VXML_DATA_DOCUMENT = 0,
    VXML_DATA_FORM,
    VXML_DATA_EXECUTABLE
} vxml_data_placement;

typedef struct vxml_data_fetch_policy {
    const char *fetchaudio_uri;
    size_t fetchaudio_uri_size;
    bool has_fetchaudio_delay;
    uint64_t fetchaudio_delay_us;
    bool has_fetchaudio_minimum;
    uint64_t fetchaudio_minimum_us;
    bool has_timeout;
    uint64_t timeout_us;
    vxml_cmeta_data_fetch_hint fetch_hint;
    bool has_max_age;
    uint64_t max_age_seconds;
    bool has_max_stale;
    uint64_t max_stale_seconds;
} vxml_data_fetch_policy;

typedef struct vxml_data_row {
    vxml_data_placement placement;
    size_t owner_form;
    const char *name;
    size_t name_size;
    const char *uri;
    size_t uri_size;
    const char *uri_expression;
    size_t uri_expression_size;
    vxml_submit_method method;
    vxml_submit_enctype enctype;
    const char *namelist;
    size_t namelist_size;
    size_t namelist_count;
    vxml_data_fetch_policy fetch_policy;
    salts_xml_location location;
} vxml_data_row;

typedef struct vxml_block_row {
    size_t first_action;
    size_t action_count;
} vxml_block_row;

typedef struct vxml_form_row {
    const char *id;
    size_t id_size;
    size_t first_block;
    size_t block_count;
} vxml_form_row;

typedef enum vxml_profile_kind {
    VXML_PROFILE_LITERAL = 0,
    VXML_PROFILE_CMETA,
    VXML_PROFILE_QUICKJS
} vxml_profile_kind;

typedef struct vxml_session_impl vxml_session_impl;
typedef struct vxml_program_impl vxml_program_impl;

typedef vxml_status (*vxml_profile_session_init_fn)(
    vxml_session_impl *session, const void *options);
typedef vxml_status (*vxml_profile_session_start_fn)(
    vxml_session_impl *session);
typedef vxml_status (*vxml_profile_session_start_at_fn)(
    vxml_session_impl *session, size_t form_index);
typedef vxml_status (*vxml_profile_session_raise_event_fn)(
    vxml_session_impl *session,
    const char *event_name,
    size_t event_name_size);
typedef void (*vxml_profile_session_destroy_fn)(vxml_session_impl *session);
typedef void (*vxml_profile_program_destroy_fn)(vxml_program_impl *program);

struct vxml_session_impl {
    const vxml_program_impl *program;
    vxml_session_state state;
    vxml_status error;
    const char *navigation_uri;
    size_t navigation_uri_size;
    const char *navigation_fetchaudio_uri;
    size_t navigation_fetchaudio_uri_size;
    bool navigation_has_fetchaudio_delay;
    uint64_t navigation_fetchaudio_delay_us;
    bool navigation_has_fetchaudio_minimum;
    uint64_t navigation_fetchaudio_minimum_us;
    const char *submit_uri;
    size_t submit_uri_size;
    vxml_submit_method submit_method;
    vxml_submit_enctype submit_enctype;
    const char *script_src;
    size_t script_src_size;
    const char *script_charset;
    size_t script_charset_size;
    void *profile_data;
};

struct vxml_program_impl {
    vxml_form_row *forms;
    vxml_block_row *blocks;
    vxml_action_row *actions;
    vxml_data_row *data_rows;
    char *storage;
    size_t form_count;
    size_t block_count;
    size_t action_count;
    size_t data_row_count;
    size_t storage_size;
    size_t allocation_size;
    vxml_profile_kind profile_kind;
    void *profile_data;
    vxml_profile_session_init_fn profile_session_init;
    vxml_profile_session_start_fn profile_session_start;
    vxml_profile_session_start_at_fn profile_session_start_at;
    vxml_profile_session_raise_event_fn profile_session_raise_event;
    vxml_profile_session_destroy_fn profile_session_destroy;
    vxml_profile_program_destroy_fn profile_program_destroy;
};

vxml_status vxml_compile_with_features(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    uint64_t features,
    vxml_program *out,
    vxml_diagnostic *diagnostic);

vxml_status vxml_session_init_profile(
    vxml_session *session, const vxml_program *program, const void *options);
vxml_status vxml_session_start_literal(vxml_session_impl *impl);
vxml_status vxml_session_start_literal_at(
    vxml_session_impl *impl, size_t form_index);

#endif /* TURBO_VOICEXML_INTERNAL_H */
