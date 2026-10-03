#ifndef TURBOSCXML_QUICKJS_SANDBOX_H
#define TURBOSCXML_QUICKJS_SANDBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum quickjs_sandbox_status {
    QUICKJS_SANDBOX_OK = 0,
    QUICKJS_SANDBOX_INVALID_ARGUMENT,
    QUICKJS_SANDBOX_ALLOCATION_FAILED,
    QUICKJS_SANDBOX_LIMIT_EXCEEDED,
    QUICKJS_SANDBOX_EXCEPTION
} quickjs_sandbox_status;

typedef struct quickjs_sandbox_options {
    size_t max_source_bytes;
    size_t max_string_bytes;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_eval_milliseconds;
} quickjs_sandbox_options;

typedef struct quickjs_sandbox_runtime {
    void *runtime;
    void *context;
    quickjs_sandbox_options options;
    char *result_string;
    size_t result_string_capacity;
    uint64_t deadline_ms;
    size_t max_diagnostic_bytes;
    bool interrupted;
} quickjs_sandbox_runtime;

bool quickjs_sandbox_limits_valid(
    const quickjs_sandbox_options *options);

void quickjs_sandbox_diagnostic(
    char *diagnostic, size_t capacity, const char *message);

bool quickjs_sandbox_deadline_expired(
    quickjs_sandbox_runtime *runtime);
bool quickjs_sandbox_deadline_begin(
    quickjs_sandbox_runtime *runtime, uint64_t milliseconds);
void quickjs_sandbox_deadline_end(
    quickjs_sandbox_runtime *runtime, bool owned);

quickjs_sandbox_status quickjs_sandbox_exception(
    quickjs_sandbox_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity);
quickjs_sandbox_status quickjs_sandbox_context_recreate(
    quickjs_sandbox_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity);

quickjs_sandbox_status quickjs_sandbox_runtime_init(
    quickjs_sandbox_runtime *runtime,
    const quickjs_sandbox_options *options,
    char *diagnostic, size_t diagnostic_capacity);
quickjs_sandbox_status quickjs_sandbox_runtime_eval(
    quickjs_sandbox_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    char *diagnostic, size_t diagnostic_capacity);
void quickjs_sandbox_runtime_destroy(
    quickjs_sandbox_runtime *runtime);

quickjs_sandbox_status quickjs_sandbox_validate_source(
    const quickjs_sandbox_options *options,
    const char *source, size_t source_size,
    const char *filename,
    char *diagnostic, size_t diagnostic_capacity);
quickjs_sandbox_status quickjs_sandbox_validate_expression(
    const quickjs_sandbox_options *options,
    const char *source, size_t source_size,
    const char *filename,
    char *diagnostic, size_t diagnostic_capacity);

#endif
