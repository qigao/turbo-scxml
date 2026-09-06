#include "cmeta_location.h"
#include "cmeta_scope.h"
#include "tinytest.h"

#include <cmeta/cmeta.h>
#include <cmeta/data.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_probe {
    size_t calls;
    size_t fail_on_call;
    size_t live;
} allocation_probe;

static void *probe_allocate(void *user, size_t size) {
    allocation_probe *probe = (allocation_probe *)user;
    void *pointer;
    ++probe->calls;
    if (probe->fail_on_call == probe->calls) return NULL;
    pointer = malloc(size);
    if (pointer != NULL) ++probe->live;
    return pointer;
}

static void *probe_allocate_zero(void *user, size_t count, size_t size) {
    allocation_probe *probe = (allocation_probe *)user;
    void *pointer;
    ++probe->calls;
    if (probe->fail_on_call == probe->calls) return NULL;
    pointer = calloc(count, size);
    if (pointer != NULL) ++probe->live;
    return pointer;
}

static void probe_deallocate(void *user, void *pointer) {
    allocation_probe *probe = (allocation_probe *)user;
    if (pointer == NULL) return;
    free(pointer);
    --probe->live;
}

static cmeta_scope_allocator probe_allocator(allocation_probe *probe) {
    return (cmeta_scope_allocator){
        .user = probe,
        .allocate = probe_allocate,
        .allocate_zero = probe_allocate_zero,
        .deallocate = probe_deallocate};
}

typedef struct managed_value {
    int *value;
} managed_value;

static size_t managed_destroy_count;
static size_t managed_copy_calls;
static size_t managed_fail_copy_call;

static bool managed_copy(void *destination, const void *source) {
    managed_value *out = (managed_value *)destination;
    const managed_value *in = (const managed_value *)source;
    ++managed_copy_calls;
    out->value = NULL;
    if (managed_fail_copy_call == managed_copy_calls) return false;
    out->value = (int *)malloc(sizeof(*out->value));
    if (out->value == NULL) return false;
    *out->value = *in->value;
    return true;
}

static void managed_move(void *destination, void *source) {
    managed_value *out = (managed_value *)destination;
    managed_value *in = (managed_value *)source;
    out->value = in->value;
    in->value = NULL;
}

static void managed_destroy(void *object) {
    managed_value *value = (managed_value *)object;
    free(value->value);
    value->value = NULL;
    ++managed_destroy_count;
}

static const cmeta_type_identity managed_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.managed");
static const cmeta_type_traits managed_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = managed_copy,
    .move_construct = managed_move,
    .destroy = managed_destroy};
static const cmeta_type_desc managed_type = {
    .name = "managed_value",
    .size = sizeof(managed_value),
    .align = _Alignof(managed_value),
    .kind = CMETA_T_OBJECT,
    .traits = &managed_traits,
    .identity = &managed_identity};
static const unsigned char managed_shape = 0u;
static const cmeta_data_desc managed_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.managed.data",
    .display_name = "managed value",
    .kind = CMETA_DATA_CUSTOM,
    .storage_type = &managed_type,
    .shape = &managed_shape};

Struct(nested_value,
    (int, number)
);

Struct(root_value,
    (bool, enabled),
    (nested_value, nested)
);

static const cmeta_type_identity nested_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.nested");
static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.root");
static const cmeta_type_traits aggregate_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc nested_type = {
    .name = "nested_value",
    .size = sizeof(nested_value),
    .align = _Alignof(nested_value),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits,
    .identity = &nested_identity};
static const cmeta_type_desc root_type = {
    .name = "root_value",
    .size = sizeof(root_value),
    .align = _Alignof(root_value),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits,
    .identity = &root_identity};
static const cmeta_data_field_desc nested_fields[] = {
    {"test.voicexml.cmeta.nested.number", "number",
     offsetof(nested_value, number), &cmeta_data_int}};
static const cmeta_data_struct_shape nested_shape = {
    .layout = StructMeta(nested_value),
    .fields = nested_fields,
    .field_count = sizeof(nested_fields) / sizeof(nested_fields[0])};
static const cmeta_data_desc nested_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.nested.data",
    .display_name = "nested value",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &nested_type,
    .shape = &nested_shape};
static const cmeta_data_field_desc root_fields[] = {
    {"test.voicexml.cmeta.root.enabled", "enabled",
     offsetof(root_value, enabled), &cmeta_data_bool},
    {"test.voicexml.cmeta.root.nested", "nested",
     offsetof(root_value, nested), &nested_data}};
static const cmeta_data_struct_shape root_shape = {
    .layout = StructMeta(root_value),
    .fields = root_fields,
    .field_count = sizeof(root_fields) / sizeof(root_fields[0])};
static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.root.data",
    .display_name = "root value",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape};

static void reset_managed_probe(void) {
    managed_destroy_count = 0u;
    managed_copy_calls = 0u;
    managed_fail_copy_call = 0u;
}

spec("neutral VoiceXML CMeta scope storage") {
    before_each() {
        reset_managed_probe();
    }

    it("aligns trivial and managed slots within the storage hard limit") {
        cmeta_scope_schema schema = {0};
        cmeta_scope_storage storage = {0};
        size_t boolean_slot = SIZE_MAX;
        size_t managed_slot = SIZE_MAX;
        bool conflict = true;

        check_true(cmeta_scope_schema_init(
            &schema, 2u, sizeof(bool) + _Alignof(managed_value) - 1u +
                              sizeof(managed_value),
            NULL));
        check_true(cmeta_scope_register(
            &schema, "enabled", 7u, &cmeta_data_bool,
            &boolean_slot, &conflict));
        check_false(conflict);
        check_true(cmeta_scope_register(
            &schema, "item", 4u, &managed_data, &managed_slot, &conflict));
        check_equal(schema.slots[boolean_slot].offset % _Alignof(bool),
                    (size_t)0u);
        check_equal(schema.slots[managed_slot].offset % _Alignof(managed_value),
                    (size_t)0u);
        check_true(schema.slots[managed_slot].managed);
        check_equal(schema.storage_size,
                    schema.slots[managed_slot].offset + sizeof(managed_value));
        check_true(cmeta_scope_storage_init(&storage, &schema, NULL));
        check_equal((uintptr_t)storage.view.storage % schema.storage_align,
                    (uintptr_t)0u);
        cmeta_scope_storage_destroy(&storage);
        cmeta_scope_schema_destroy(&schema);
    }

    it("accepts semantic-equal duplicate types and rejects conflicts") {
        cmeta_type_desc equal_int_type = cmeta_type_int;
        cmeta_data_desc equal_int_data = cmeta_data_int;
        cmeta_scope_schema schema = {0};
        size_t first = SIZE_MAX;
        size_t duplicate = SIZE_MAX;
        bool conflict = false;

        equal_int_data.storage_type = &equal_int_type;
        check_true(cmeta_scope_schema_init(&schema, 2u, 64u, NULL));
        check_true(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &first, &conflict));
        check_true(cmeta_scope_register(
            &schema, "value", 5u, &equal_int_data,
            &duplicate, &conflict));
        check_false(conflict);
        check_equal(duplicate, first);
        check_equal(schema.slot_count, (size_t)1u);
        check_false(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_double,
            &duplicate, &conflict));
        check_true(conflict);
        check_equal(schema.slot_count, (size_t)1u);
        cmeta_scope_schema_destroy(&schema);
    }

    it("rejects slot storage and arithmetic overflow beyond hard limits") {
        cmeta_scope_schema schema = {0};
        cmeta_type_desc huge_type = cmeta_type_int;
        cmeta_data_desc huge_data = cmeta_data_int;
        size_t slot = SIZE_MAX;
        bool conflict = false;

        check_false(cmeta_scope_schema_init(
            &schema, SIZE_MAX / sizeof(cmeta_scope_slot) + 1u,
            SIZE_MAX, NULL));
        check_true(cmeta_scope_schema_init(
            &schema, 1u, sizeof(int), NULL));
        check_true(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &slot, &conflict));
        check_false(cmeta_scope_register(
            &schema, "other", 5u, &cmeta_data_bool, &slot, &conflict));
        cmeta_scope_schema_destroy(&schema);

        check_true(cmeta_scope_schema_init(
            &schema, 1u, sizeof(int) - 1u, NULL));
        check_false(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &slot, &conflict));
        cmeta_scope_schema_destroy(&schema);

        huge_type.size = SIZE_MAX;
        huge_data.storage_type = &huge_type;
        check_true(cmeta_scope_schema_init(&schema, 2u, SIZE_MAX, NULL));
        check_true(cmeta_scope_register(
            &schema, "prefix", 6u, &cmeta_data_bool, &slot, &conflict));
        check_false(cmeta_scope_register(
            &schema, "huge", 4u, &huge_data, &slot, &conflict));
        cmeta_scope_schema_destroy(&schema);
    }

    it("keeps bound state distinct from a stored scalar zero") {
        cmeta_scope_schema schema = {0};
        cmeta_scope_storage storage = {0};
        const cmeta_data_desc *value = NULL;
        const void *object = NULL;
        size_t slot = SIZE_MAX;
        bool conflict = false;
        int zero = 0;

        check_true(cmeta_scope_schema_init(&schema, 1u, sizeof(int), NULL));
        check_true(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &slot, &conflict));
        check_true(cmeta_scope_storage_init(&storage, &schema, NULL));
        check_false(cmeta_scope_view_read(
            &storage.view, slot, &value, &object));
        check_true(cmeta_scope_view_assign(&storage.view, slot, &zero));
        check_true(cmeta_scope_view_read(
            &storage.view, slot, &value, &object));
        check_equal(*(const int *)object, 0);
        check_true(storage.view.bound[slot] != 0u);
        cmeta_scope_storage_destroy(&storage);
        cmeta_scope_schema_destroy(&schema);
    }

    it("destroys managed slots exactly when clearing or destroying storage") {
        cmeta_scope_schema schema = {0};
        cmeta_scope_storage storage = {0};
        size_t slot = SIZE_MAX;
        bool conflict = false;
        int first = 7;
        int second = 9;
        managed_value source = {&first};

        check_true(cmeta_scope_schema_init(&schema, 1u, 64u, NULL));
        check_true(cmeta_scope_register(
            &schema, "item", 4u, &managed_data, &slot, &conflict));
        check_true(cmeta_scope_storage_init(&storage, &schema, NULL));
        check_true(cmeta_scope_view_assign(&storage.view, slot, &source));
        cmeta_scope_view_clear_slot(&storage.view, slot);
        check_equal(managed_destroy_count, (size_t)1u);
        check_equal(storage.view.bound[slot], (unsigned char)0u);
        source.value = &second;
        check_true(cmeta_scope_view_assign(&storage.view, slot, &source));
        cmeta_scope_storage_destroy(&storage);
        check_equal(managed_destroy_count, (size_t)2u);
        cmeta_scope_schema_destroy(&schema);
    }

    it("rolls back a failed managed copy and moves without double destruction") {
        cmeta_scope_schema schema = {0};
        cmeta_scope_storage source = {0};
        cmeta_scope_storage destination = {0};
        cmeta_scope_storage moved = {0};
        size_t first_slot = SIZE_MAX;
        size_t second_slot = SIZE_MAX;
        bool conflict = false;
        int first = 11;
        int second = 22;
        managed_value first_value = {&first};
        managed_value second_value = {&second};

        check_true(cmeta_scope_schema_init(&schema, 2u, 64u, NULL));
        check_true(cmeta_scope_register(
            &schema, "first", 5u, &managed_data, &first_slot, &conflict));
        check_true(cmeta_scope_register(
            &schema, "second", 6u, &managed_data, &second_slot, &conflict));
        check_true(cmeta_scope_storage_init(&source, &schema, NULL));
        check_true(cmeta_scope_storage_init(&destination, &schema, NULL));
        check_true(cmeta_scope_storage_init(&moved, &schema, NULL));
        check_true(cmeta_scope_view_assign(
            &source.view, first_slot, &first_value));
        check_true(cmeta_scope_view_assign(
            &source.view, second_slot, &second_value));

        managed_copy_calls = 0u;
        managed_fail_copy_call = 2u;
        check_false(cmeta_scope_view_copy(&destination.view, &source.view));
        check_equal(destination.view.bound[first_slot], (unsigned char)0u);
        check_equal(destination.view.bound[second_slot], (unsigned char)0u);
        check_true(source.view.bound[first_slot] != 0u);
        check_true(source.view.bound[second_slot] != 0u);

        managed_fail_copy_call = 0u;
        check_true(cmeta_scope_view_copy(&destination.view, &source.view));
        check_true(cmeta_scope_view_move_replace(
            &moved.view, &destination.view));
        check_equal(destination.view.bound[first_slot], (unsigned char)0u);
        check_equal(destination.view.bound[second_slot], (unsigned char)0u);
        check_true(moved.view.bound[first_slot] != 0u);
        check_true(moved.view.bound[second_slot] != 0u);

        cmeta_scope_storage_destroy(&moved);
        cmeta_scope_storage_destroy(&destination);
        cmeta_scope_storage_destroy(&source);
        check_equal(managed_destroy_count, (size_t)5u);
        cmeta_scope_schema_destroy(&schema);
    }

    it("uses injected allocation operations for schema names and storage") {
        allocation_probe probe = {0};
        cmeta_scope_allocator allocator = probe_allocator(&probe);
        cmeta_scope_schema schema = {0};
        cmeta_scope_storage storage = {0};
        size_t slot = SIZE_MAX;
        bool conflict = false;

        probe.fail_on_call = 1u;
        check_false(cmeta_scope_schema_init(&schema, 1u, 64u, &allocator));
        check_equal(probe.live, (size_t)0u);

        memset(&probe, 0, sizeof(probe));
        allocator = probe_allocator(&probe);
        check_true(cmeta_scope_schema_init(&schema, 1u, 64u, &allocator));
        probe.fail_on_call = 2u;
        check_false(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &slot, &conflict));
        check_equal(schema.slot_count, (size_t)0u);
        probe.fail_on_call = 0u;
        check_true(cmeta_scope_register(
            &schema, "value", 5u, &cmeta_data_int, &slot, &conflict));
        probe.fail_on_call = 4u;
        check_false(cmeta_scope_storage_init(&storage, &schema, &allocator));
        check_null(storage.allocation);
        cmeta_scope_schema_destroy(&schema);
        check_equal(probe.live, (size_t)0u);
    }
}

spec("neutral VoiceXML CMeta dotted locations") {
    it("resolves nested fields at the exact path-depth limit") {
        cmeta_location location = {0};
        size_t error_offset = SIZE_MAX;

        check_equal(cmeta_location_compile(
                        &location, "nested.number", 13u,
                        &root_data, 2u, &error_offset),
                    CMETA_LOCATION_OK);
        check_equal(location.kind, CMETA_LOCATION_ROOT);
        check_equal(location.offset,
                    offsetof(root_value, nested) +
                        offsetof(nested_value, number));
        check_true(cmeta_type_equal(
            location.value->storage_type, &cmeta_type_int));
    }

    it("reports syntax unknown traversal depth and schema overflow") {
        cmeta_location location = {0};
        cmeta_data_field_desc bad_field = root_fields[1];
        cmeta_data_struct_shape bad_shape = root_shape;
        cmeta_data_desc bad_root = root_data;
        size_t error_offset = SIZE_MAX;

        check_equal(cmeta_location_compile(
                        &location, "nested.", 7u,
                        &root_data, 2u, &error_offset),
                    CMETA_LOCATION_SYNTAX_ERROR);
        check_equal(error_offset, (size_t)7u);
        check_equal(cmeta_location_compile(
                        &location, "nested.missing", 14u,
                        &root_data, 2u, &error_offset),
                    CMETA_LOCATION_UNKNOWN);
        check_equal(cmeta_location_compile(
                        &location, "nested.number", 13u,
                        &root_data, 1u, &error_offset),
                    CMETA_LOCATION_LIMIT_EXCEEDED);

        bad_field.offset = SIZE_MAX;
        bad_shape.fields = &bad_field;
        bad_shape.field_count = 1u;
        bad_root.shape = &bad_shape;
        check_equal(cmeta_location_compile(
                        &location, "nested.number", 13u,
                        &bad_root, 2u, &error_offset),
                    CMETA_LOCATION_INVALID_ARGUMENT);
    }

    it("resolves a dotted path rooted in one lexical frame schema") {
        cmeta_scope_schema schema = {0};
        cmeta_location location = {0};
        size_t slot = SIZE_MAX;
        size_t error_offset = SIZE_MAX;
        bool conflict = false;

        check_true(cmeta_scope_schema_init(&schema, 1u, 64u, NULL));
        check_true(cmeta_scope_register(
            &schema, "item", 4u, &nested_data, &slot, &conflict));
        check_equal(cmeta_location_compile_with_scope(
                        &location, "item.number", 11u,
                        &root_data, &schema, 2u, &error_offset),
                    CMETA_LOCATION_OK);
        check_equal(location.kind, CMETA_LOCATION_SCOPE);
        check_equal(location.slot, slot);
        check_equal(location.offset, offsetof(nested_value, number));
        cmeta_scope_schema_destroy(&schema);
    }
}
