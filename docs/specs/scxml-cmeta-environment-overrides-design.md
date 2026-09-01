# CMeta Environment Overrides Design

## Context

W3C SCXML data binding requires values supplied by the host environment when a
session is instantiated to replace the contained values of matching top-level
`<data>` declarations. The existing `scxml_session_init_cmeta` API copies one
complete CMeta state value and then evaluates every document initializer. It
therefore cannot distinguish a host-supplied field from an ordinary initial
state field and cannot implement W3C test 276 without changing V1 behavior.

This decision affects the public CMeta session API, the analyzer/emitter
contract, early and late data initialization, session-owned storage, and the
W3C conformance adapter. The existing StateChart lowering and CFlow execution
ownership remain unchanged.

## Decision

Add an explicit V2 session initialization API. V1 remains source- and
behavior-compatible.

```c
#define SCXML_CMETA_SESSION_OPTIONS_ABI_V2 2u

typedef struct scxml_cmeta_environment_override {
    const char *location;
    size_t location_size;
} scxml_cmeta_environment_override;

typedef struct scxml_cmeta_session_options_v2 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
    const scxml_cmeta_environment_override *environment_overrides;
    size_t environment_override_count;
} scxml_cmeta_session_options_v2;

cflow_statechart_instance_status scxml_session_init_cmeta_v2(
    scxml_session *out,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v2 *options);
```

The full `initial_state` is still the only source of values. Each override row
only identifies a top-level `<data>` location whose contained initializer must
not overwrite that value. The caller owns the options, rows, strings, and
initial state; all may be released or modified after the function returns.

Example:

```c
const scxml_cmeta_environment_override overrides[] = {
    {"sequence", sizeof("sequence") - 1u}
};
const scxml_cmeta_session_options_v2 options = {
    SCXML_CMETA_SESSION_OPTIONS_ABI_V2,
    sizeof(scxml_cmeta_session_options_v2),
    &initial_state,
    overrides,
    1u
};
cflow_statechart_instance_status status =
    scxml_session_init_cmeta_v2(&session, &config, &options);
```

## Data and ownership protocol

- Data unit: one compiled top-level data-initializer assignment index.
- Fact source: the mutable CMeta object owned by the CFlow StateChart instance.
- Input ownership: all V2 option views are borrowed until initialization
  returns.
- Session ownership: a bounded `size_t` array of validated assignment indices.
- Lifetime: the session array is immutable after successful initialization and
  is freed with all other session storage.
- Thread topology: initialization is a single-threaded control-plane operation;
  runtime reads are performed by the existing session executor.
- Capacity: the row count cannot exceed the compiled top-level declaration
  count, and allocation uses checked existing row allocation.
- Shutdown: no new drain or callback protocol is introduced.

## Compile-time invariant

The analyzer records the number of root `<scxml>/<datamodel>/<data>` rows.
Both emit paths record the exact contiguous assignment span emitted for the
root datamodel, independent of its admitted child order. Program construction
verifies that the analyzed root declaration count equals this span and that
the span is contained by the emitted assignment table.

An override location is compiled against the program CMeta schema and must
match exactly one destination in this span by semantic type, storage offset,
and storage size. This avoids retaining source strings or exposing expression
compiler details through the public API.

## Initialization transaction

1. Validate the V2 ABI, struct size, initial state, row count, and row views.
2. Compile every location and map it to one root data assignment index.
3. Reject unknown, nested/non-root, duplicate, or non-writable locations.
4. Allocate and retain the validated index array in session storage.
5. Copy the complete caller state into the mutable CMeta instance state.
6. For early binding, apply every document data initializer except retained
   override indices.
7. For late binding, the existing late initializer transaction applies every
   assignment except retained override indices.
8. Attach the native StateChart instance only after validation and early
   initialization succeed.

Skipping the assignment is required instead of applying and restoring it:
restoration could violate CMeta-managed object lifetime and would create a
second transient source of truth.

## Errors and atomicity

Malformed V2 options, unknown paths, non-top-level paths, and duplicates return
`CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT`. Allocation failure returns
`CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED`. Expression initialization keeps
its existing error result. A failure occurs before native instance attachment;
cleanup destroys all copied CMeta state and session-owned arrays, leaving the
output session empty.

## Alternatives considered

### Change V1 semantics

Rejected because V1 has no presence information. Treating every initial-state
field as an override would silently disable document defaults for existing
callers.

### Restore overridden values after initialization

Rejected because it performs an unnecessary second state mutation and can
mis-handle managed strings or containers. Skipping the initializer preserves
ownership by construction.

### Store location strings in the session

Rejected because the program already owns the compiled destination metadata.
Validated indices are smaller, bounded, and avoid runtime parsing.

## Compatibility, migration, and rollback

V1 callers require no changes and observe the same initializer behavior.
Callers that materialize SCXML invocation parameters opt into V2 and list only
the child top-level data locations actually supplied by the invoking
environment. Removing the V2 function, types, private counters, and private
session array fully rolls back the change without changing V1 serialized data
or StateChart IR.

## Verification

- V1 regression: document defaults still overwrite ordinary initial-state
  fields.
- V2 early and late binding: supplied top-level values survive initialization.
- Contract failures: NULL/empty, unknown, nested, duplicate, and excessive
  rows fail without a live session.
- W3C test 276: a host value of `1` replaces the child's contained default `0`.
- Focused CMeta and W3C tests, followed by Debug and Release preset regression.
