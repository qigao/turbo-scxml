#ifndef TURBOSCXML_QUICKJS_CMETA_BRIDGE_H
#define TURBOSCXML_QUICKJS_CMETA_BRIDGE_H

#include "cmeta_scope.h"
#include "quickjs_sandbox.h"

#include <cmeta/data.h>
#include <cmeta/declared_type.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if TURBOSCXML_HAS_QUICKJS
#include <quickjs.h>
#endif

typedef struct quickjs_cmeta_limits {
    size_t max_conversion_depth;
    size_t max_properties;
    size_t max_array_items;
    size_t max_snapshot_bytes;
    size_t max_string_bytes;
} quickjs_cmeta_limits;

typedef struct quickjs_cmeta_bridge quickjs_cmeta_bridge;

typedef struct quickjs_cmeta_collection_adapter {
    bool (*schema_supported)(
        const cmeta_data_desc *root,
        const cmeta_data_desc *descriptor,
        const cmeta_declared_type *declared_type,
        size_t depth,
        const quickjs_cmeta_limits *limits,
        size_t *properties,
        void *user);
#if TURBOSCXML_HAS_QUICKJS
    JSValue (*import_collection)(
        quickjs_cmeta_bridge *bridge,
        const cmeta_data_desc *descriptor,
        const cmeta_type_desc *storage_type,
        const cmeta_declared_type *declared_type,
        const void *object,
        size_t depth,
        void *user);
    bool (*export_collection)(
        quickjs_cmeta_bridge *bridge,
        const cmeta_data_desc *descriptor,
        const cmeta_type_desc *storage_type,
        const cmeta_declared_type *declared_type,
        JSValueConst value,
        void *object,
        size_t depth,
        void *user);
#endif
} quickjs_cmeta_collection_adapter;

struct quickjs_cmeta_bridge {
#if TURBOSCXML_HAS_QUICKJS
    JSContext *context;
#endif
    quickjs_cmeta_limits limits;
    const cmeta_data_desc *root;
    size_t properties;
    const quickjs_cmeta_collection_adapter *collections;
    void *collection_user;
};

typedef struct quickjs_cmeta_state_scratch {
    void *allocation;
    void *value;
    bool live;
} quickjs_cmeta_state_scratch;

bool quickjs_cmeta_limits_valid(
    const quickjs_cmeta_limits *limits);

const quickjs_cmeta_collection_adapter *
quickjs_cmeta_default_collection_adapter(void);

bool quickjs_cmeta_schema_supported(
    const cmeta_data_desc *root,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user,
    size_t supplemental_properties);

bool quickjs_cmeta_value_schema_supported(
    const cmeta_data_desc *root,
    const cmeta_data_desc *descriptor,
    const cmeta_declared_type *declared_type,
    size_t depth,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user,
    bool inside_collection,
    size_t *properties);

bool quickjs_cmeta_bridge_init(
    quickjs_cmeta_bridge *bridge,
    quickjs_sandbox_runtime *runtime,
    const cmeta_data_desc *root,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user);

#if TURBOSCXML_HAS_QUICKJS
JSValue quickjs_cmeta_import_value(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    const void *object,
    size_t depth);

bool quickjs_cmeta_export_value(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value,
    void *object,
    size_t depth);

bool quickjs_cmeta_import_root(
    quickjs_cmeta_bridge *bridge,
    const void *state);

bool quickjs_cmeta_export_root(
    quickjs_cmeta_bridge *bridge,
    void *state);
#endif

bool quickjs_cmeta_state_snapshot(
    quickjs_cmeta_state_scratch *scratch,
    const cmeta_data_desc *root,
    const void *committed,
    size_t max_snapshot_bytes);

void quickjs_cmeta_state_scratch_destroy(
    quickjs_cmeta_state_scratch *scratch,
    const cmeta_data_desc *root);

bool quickjs_cmeta_state_publish(
    const cmeta_data_desc *root,
    void *committed,
    quickjs_cmeta_state_scratch *scratch);

#endif
