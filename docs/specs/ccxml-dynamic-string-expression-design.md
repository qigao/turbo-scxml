# CCXML Dynamic String Expression Design

## Scope

This facility provides typed dynamic string evaluation to
`<createcall dest="...">` and `<redirect dest="...">`. Quoted, nonempty
destination literals keep their existing behavior. An unquoted destination is
compiled by the configured CCXML datamodel adapter when a session is admitted
and evaluated immediately before the telephony prepare callback.

XPath is not part of this design. The built-in implementation uses the
existing CMeta expression compiler. QuickJS can implement the same adapter
tail later without changing the CCXML program or session model.

## Public boundary

Append an opaque `ccxml_string_expression` handle and three optional callbacks
to `ccxml_datamodel_adapter_v1`:

```c
typedef struct ccxml_string_expression {
    void *impl;
} ccxml_string_expression;

scxml_adapter_status (*compile_string_expression)(
    void *user, const char *source, size_t source_size,
    ccxml_string_expression *out_expression, const char **out_error);
scxml_adapter_status (*evaluate_string_expression)(
    void *user, const ccxml_string_expression *expression,
    const ccxml_event *event, ccxml_string_view *out_value,
    const char **out_error);
void (*destroy_string_expression)(
    void *user, ccxml_string_expression *expression);
```

These fields are an additive, size-versioned tail. Existing adapter prefixes
remain valid for programs that use literals only. A program containing a
dynamic destination requires the complete tail at session admission.

The returned string is borrowed through the immediately following telephony
prepare callback. Neither the CCXML core nor the provider may retain it.

## Compile and storage model

The XML compiler first decodes attribute entities and fails compilation if
decoding fails. It then classifies the decoded destination as follows:

- a matching single- or double-quoted value is a literal; existing empty and
  escape restrictions remain unchanged, and only its interior is retained;
- every other nonempty attribute value is a dynamic expression source; the
  complete source is retained and `destination_is_dynamic` is set;
- both forms consume the existing `max_name_bytes` retained-string budget.

The program records `uses_string_expression` so session admission can require
only the capabilities actually used. Dynamic sources remain immutable program
storage. Compiled handles are owned per session and per action.

## CMeta implementation

The built-in adapter compiles with
`scxml_expr_compile_value_with_scope(...)`. Admission succeeds only when
`scxml_expr_program_value_kind(...) == SCXML_EXPR_VALUE_STRING`.

Evaluation uses `scxml_expr_evaluate_value_with_system(...)`, exposes
only `_event.name` from the SCXML system-operand family, and supplies the
active typed supplemental scope. Other system operands are rejected during
session admission. The result
must be `SCXML_EXPR_VALUE_STRING` and satisfy all of these runtime rules:

- `data` is non-NULL;
- `size` is nonzero;
- no embedded NUL occurs in the borrowed byte range;
- `size` does not exceed the CMeta datamodel's configured
  `max_string_bytes`.

Internal scoped compile/evaluate entry points mirror the existing condition
entry points. When a program uses foreach, all destination expressions compile
against the global foreach scope. During a foreach transaction evaluation
uses staged scope; otherwise it uses committed scope. Thus
`<createcall dest="item.destination"/>` and
`<redirect dest="item.destination"/>` observe the current typed iteration.

## Session lifecycle and transaction behavior

Session initialization allocates a zeroed expression-handle array only when
the program uses dynamic string expressions. Every dynamic createcall or
redirect action is compiled before the session is published. Any failure
destroys all handles that compiled successfully, then follows normal session
rollback.

Dispatch evaluates the destination immediately before calling the matching
`prepare_create_call` or `prepare_redirect` operation. Redirect first validates
its mandatory current Event connection. Evaluation failure, an invalid returned
view, or provider rejection discards all tickets already prepared by the
transition and rolls back staged foreach scope. A successful provider prepare
is retained in the existing effect journal, so commit ordering and exact-once
discard remain unchanged.

Session destruction destroys every live expression handle exactly once before
releasing the datamodel owner relationship.

## Compatibility and non-goals

- `CCXML_DATAMODEL_ADAPTER_ABI_V1` remains unchanged; `struct_size` gates the
  appended callbacks.
- Literal-only programs accept legacy adapter prefixes and do not compile or
  evaluate expressions.
- Dialog source, conference name, IDs, assignment values, send targets, and
  delay expressions are not migrated in this facility.
- General string concatenation and coercion are not added to the CMeta
  expression language.
- QuickJS support is a separate adapter/runtime extraction task because the
  current QuickJS evaluator owns SCXML-session-specific runtime state.

## Verification

Tests must cover XML classification and retention, legacy-prefix compatibility,
missing-tail rejection, compile failure cleanup, successful CMeta root lookup,
typed foreach staged lookup, empty/NUL runtime rejection, provider failure
rollback, and exact-once handle destruction. Release configuration must build
without QuickJS and the full CTest suite must pass.
