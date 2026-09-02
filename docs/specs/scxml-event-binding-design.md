# SCXML current Event binding design

## Context

TurboSCXML owns SCXML parsing, expression evaluation, and session execution.
Rocida remains an installed dependency that supplies CMeta, CFlow, XML,
QueryVM, and platform services. The SCXML repository is the only owner of the
W3C-derived corpus and its conformance claims.

The owning CMeta session keeps one `scxml_expr_system_values` object. Its
`event_name.data` pointer is the binding witness for `_event`:

- before the first Event is selected, the zero-initialized pointer is `NULL`;
- `scxml_runtime_observe_event()` assigns the selected Event name before guards
  or executable content observe it;
- the same value remains visible through exit actions, transition actions,
  entry actions, and eventless stabilization;
- selecting the next Event replaces the name and optional metadata together.

Optional fields such as `invokeid` are values inside a bound Event envelope.
They use a non-`NULL`, zero-length string view when the field has no value.
This deliberately distinguishes an unbound `_event` from a bound Event with an
empty optional field.

## Decision

The CMeta expression language adds the Boolean primary expression
`isBound(_event)`. This whole-Event form accepts exactly the unquoted `_event`
system variable. The later additive field-presence forms are specified in
[`scxml-event-envelope-design.md`](scxml-event-envelope-design.md); arbitrary
or nested paths remain invalid.

At evaluation time:

- a valid system context with `event_name.data == NULL` returns `false`;
- a valid system context with `event_name.data != NULL` returns `true`;
- a missing system context remains an evaluation error;
- malformed calls fail compilation with a stable syntax diagnostic.

The operation is read-only. It does not retain borrowed views, mutate session
state, dequeue Events, or advance the interpreter. No new public C structure,
exported function, dependency, allocation, or ABI field is introduced.

## Alternatives

### Compare `_event.name` with an empty string

Rejected. Reading `_event.name` while `_event` is unbound correctly fails, and
an empty string is also a valid value inside a bound envelope. Treating these
states as equivalent would hide the lifecycle distinction required by SCXML.

### Add generic null values and optional chaining

Rejected for this increment. That changes the expression value model, QueryVM
lowering, comparison rules, and assignment semantics. The current requirement
only needs a bounded system-variable binding query.

### Use a quoted variable name

Rejected. `isBound("_event")` would turn a finite, parser-checked system
variable into a free-form string key and require runtime name dispatch.

## W3C-derived corpus mapping

The local transformations preserve these mandatory assertions from the W3C
SCXML Implementation Report suite:

- test 318 observes that `_event` remains bound to `foo` while entering the
  next state, even after `bar` has been raised but not selected;
- test 319 uses `isBound(_event)` during initial entry and requires the unbound
  branch;
- test 339 requires an internally raised Event to expose an empty `invokeid`;
- test 396 requires `_event.name` and the Event name used for transition
  selection to both equal `foo`.

Each transformation terminates through a `pass` or `fail` final state and sends
exactly one `result.pass` or `result.fail` observation through the existing
strict Event I/O probe. The manifest remains the corpus fact source.

## State, ownership, and failure semantics

`scxml_session_impl.system_values` is the single fact source. Event names point
to immutable program storage; copied external metadata and structured data stay
owned by the session according to the existing bounded envelope protocol.
`isBound(_event)` only reads the call-scoped witness and never extends its
lifetime.

Compilation is fail-fast for an unknown function, wrong argument, extra
argument, or missing parenthesis. Evaluation is fail-fast when the caller omits
the system context. Existing expression output-preservation rules remain
unchanged on failure.

## Compatibility, verification, and rollback

The syntax is additive, so existing valid expressions and ABI remain stable.
The main compatibility risk is accidentally treating a missing evaluator
context as an unbound Event; a unit test requires that case to remain an error.
The runtime risk is clearing `_event` during a run-to-completion step; W3C test
318 and the existing owned-envelope stabilization tests cover that lifetime.

Verification consists of the focused expression test, the four W3C-derived
fixtures, manifest validation, all TurboSCXML CTest targets, and `git diff
--check`. Rollback removes the expression operand and returns the four manifest
rows to `UNSUPPORTED`; no stored data or external format migration is needed.
