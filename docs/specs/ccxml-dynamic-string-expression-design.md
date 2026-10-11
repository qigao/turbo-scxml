# CCXML Dynamic String Expression Design

## Scope

This facility provides typed dynamic string evaluation to
`<createcall dest="...">`, `<redirect dest="...">`, optional
`<createconference confname="...">`, `src` on `<dialogprepare>` and direct
`<dialogstart>`, and `target`/`name` on `<send>`. Quoted, nonempty literals keep their
existing behavior. An unquoted value is compiled by the configured CCXML
datamodel adapter when a session is admitted and evaluated immediately before
the consuming provider prepare callback.

XPath is not part of this design. The built-in CMeta implementation uses the
existing typed expression compiler. The opt-in QuickJS-NG adapter described
below uses the same CCXML boundary without changing the Program or Session ABI.

## QuickJS-NG adapter

`TurboSCXML::CCXMLQuickJS` / `<ccxml/quickjs.h>` is installed only with
`TURBOSCXML_ENABLE_QUICKJS=ON`. It reuses the existing vcpkg `quickjs-ng`
dependency, private QuickJS sandbox, and CMeta projection bridge. A separate
optional target preserves the CCXML feature-OFF graph. Reusing the SCXML
session evaluator directly was rejected because it requires an SCXML Session
and supplemental scope owner; duplicating the VM or CMeta conversion logic was
also rejected.

The adapter compiles condition and dynamic-string syntax at Session admission.
Each evaluation recreates a hardened JS context, copies the current CMeta root
into JS globals, and adds `event$` with `name`, `connectionid`, and `conferenceid`
(absent IDs are `undefined`). Conditions use ECMAScript truth conversion;
dynamic strings require an actual nonempty string without embedded NUL. The
root field name `event$` is reserved. Script-local mutations are not imported
into later evaluations, including after exceptions; this is an expression
profile, not a persistent ECMAScript datamodel. No pending Promise jobs are
executed; any objects retained by pending jobs remain subject to the runtime
heap bound until runtime destruction.

The application owns the CMeta schema and mutable state. The adapter owns one
runtime, result scratch, and the inner CMeta adapter. Sessions own their
validated expression handles (`tstr` source copies); destroy all Sessions and
handles before destroying the adapter, then release the schema/state. Calls
are synchronous, serialized, non-reentrant and must not overlap state mutation.
String results borrow adapter scratch through the next consuming provider
prepare; providers copy retained bytes. Existing CMeta location, payload, and
transactional assignment operations handle ID writeback, commit and discard.
Expression evaluation never exports JS mutations into application state.

Positive source/result/heap/stack/deadline and CMeta conversion bounds are
mandatory. `max_snapshot_bytes` bounds the CMeta root storage size; this adapter
does not create a native state snapshot. VM heap and conversion budgets bound
the projected values. One monotonic deadline covers context creation, projection and
evaluation. Source and Event bytes are checked before entering the VM. Errors
return existing CCXML/adapter status codes with fixed, payload-free messages;
no JavaScript exception text is logged or exposed. This is not a deterministic
instruction quota. Expression-handle capacity or explicit allocation failure
returns FULL; type/contract failures return INVALID_CONTRACT; VM exceptions
(including out-of-memory exceptions), result bounds and deadlines return
ERROR_EXECUTION. Limits must also accommodate hardened context initialization;
an insufficient setup budget fails initialization.

The public API supplies a versioned config and explicit init/adapter/destroy
operations. Existing literal/location programs keep their contracts. `foreach`
continues to require the built-in CMeta profile and is rejected by Session
admission for this adapter. Inline scripts, dynamic assignment expressions,
modules, filesystem/network APIs, arbitrary JS object persistence, and a second
Session/Actor owner are outside this profile. Applications opt in by selecting
the adapter; selecting the original CMeta adapter rolls back the integration
for programs whose expressions remain in the typed subset.

For example, after initializing a zeroed `ccxml_quickjs_datamodel owner`:

```c
ccxml_quickjs_datamodel_config_v1 js = ccxml_quickjs_default_datamodel_config();
js.state_config.root = application_schema;
js.state_config.state = application_state;
ccxml_status status = ccxml_quickjs_datamodel_init(&owner, &js);
if (status != CCXML_OK) return status;
/* owner must outlive all sessions configured this way. */
session_config.datamodel = ccxml_quickjs_datamodel_adapter();
session_config.datamodel_user = &owner;
```

A dynamic dialog source can be written as `src="('app' + count + '.vxml')"`.
Parenthesize expressions starting and ending with quotes: the existing XML
profile otherwise classifies them as quoted literals with restricted escapes.
Destroy Sessions before `ccxml_quickjs_datamodel_destroy(&owner)`.

QuickJS-NG's runtime/context ownership and execution limits follow its
[embedding API](https://quickjs-ng.github.io/quickjs/developer-guide/intro/).

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
dynamic string action requires the complete tail at session admission.

The returned string is borrowed through the immediately following provider
prepare callback. Neither the CCXML core nor the provider may retain the
borrowed view; providers copy bytes needed after prepare returns.

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
entry points. When a program uses foreach, all dynamic string expressions compile
against the global foreach scope. During a foreach transaction evaluation
uses staged scope; otherwise it uses committed scope. Thus
`<createcall dest="item.destination"/>` and
`<redirect dest="item.destination"/>`, as well as
`<createconference conferenceid="conference_id"
confname="item.destination"/>`, observe the current typed iteration. Dialog
preparation and direct start likewise accept `src="item.destination"`.
Send accepts `target="item.destination"` and `name="item.event_name"` against
the same staged scope; evaluated names must satisfy the event-name grammar.

## Session lifecycle and transaction behavior

Session initialization allocates a zeroed array with destination/name handles only when
the program uses dynamic string expressions. Every dynamic createcall,
redirect, createconference, dialogprepare, direct dialogstart, or send action
is compiled before the session is published. Any failure
destroys all handles that compiled successfully, then follows normal session
rollback.

Dispatch evaluates the value immediately before calling the matching
`prepare_create_call`, `prepare_redirect`, `prepare_create_conference`,
`prepare_dialog_prepare`, `prepare_dialog_start`, or `prepare_send` operation. Redirect and
direct dialogstart first validate their mandatory current Event connection.
Send evaluates and validates name first, copies it into a temporary `tstr`
bounded by the Program's `max_name_bytes`, and then evaluates target. The name
copy survives adapter scratch reuse and is released after `prepare_send` or
on any intervening failure. Its provider must copy any retained bytes.
Evaluation failure, an invalid returned view, or provider rejection discards
all tickets already prepared by the transition and rolls back staged foreach
scope. Createconference, dialogprepare, and direct dialogstart retain a separate
returned-ID writeback ticket and commit that write before publishing the provider operation. A successful
provider prepare is retained in the existing effect journal, so commit ordering
and exact-once discard remain unchanged.

Session destruction destroys every live expression handle exactly once before
releasing the datamodel owner relationship.

## Compatibility and non-goals

- `CCXML_DATAMODEL_ADAPTER_ABI_V1` remains unchanged; `struct_size` gates the
  appended callbacks.
- Literal-only programs accept legacy adapter prefixes and do not compile or
  evaluate expressions.
- Conference IDs, assignment values, send types, and delay
  expressions are not migrated in this facility.
- General string concatenation and coercion are not added to the CMeta
  expression language.
- QuickJS-NG is an explicit optional adapter; it does not silently replace
  the CMeta expression language or enable unsupported CCXML syntax.

## Verification

Tests must cover XML classification and retention, legacy-prefix compatibility,
missing-tail rejection, compile failure cleanup, successful CMeta root lookup,
typed foreach staged lookup, empty/NUL runtime rejection, provider failure
rollback, and exact-once handle destruction. Release configuration must build
without QuickJS and the full CTest suite must pass.

`ccxml_quickjs_test` exercises actual JS truth conversion and concatenation,
fresh globals, bounded results and handles, deadline recovery, private error
redaction, foreach rejection, transaction discard, and the real
CCXML/DialogManager/VoiceXML source-selection path. Adjacent SCXML/VoiceXML and
shared sandbox suites cover the shared evaluator implementation.

The existing installed C/C++ CCXML consumers also exercise the optional ABI
when `TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS` is enabled. On Windows, after
installing a QuickJS-enabled SDK with its install user preset, set
`TURBOSCXML_ROOT` to that SDK and run from `tests/install_consumer` in the VS
developer environment:

```sh
cmake --preset win-release-quickjs-installed-user
cmake --build --preset win-release-quickjs-installed-user
ctest --preset win-release-quickjs-installed-user
```
