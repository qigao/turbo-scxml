# SCXML Event envelope field-presence design

## Context

The SCXML Event object has seven required fields: `name`, `type`, `sendid`,
`origin`, `origintype`, `invokeid`, and `data`. The W3C contract requires the
fields to exist for internal and external Events even when an optional value is
empty. See the [SCXML system-variable definition](https://www.w3.org/TR/scxml/#SystemVariables).

TurboSCXML already exposes these values through the call-scoped
`scxml_expr_system_values` view. `scxml_runtime_observe_event()` clears the
previous optional metadata, binds the newly selected Event name and type, then
copies external metadata or invoke identity into session-owned bounded storage.
The missing capability is a finite expression query that distinguishes a
present empty field from an absent current Event.

## Decision

The internal CMeta expression language accepts these exact Boolean primaries:

```text
isBound(_event.name)
isBound(_event.type)
isBound(_event.sendid)
isBound(_event.origin)
isBound(_event.origintype)
isBound(_event.invokeid)
isBound(_event.data)
```

The parser maps each supported field to an existing finite Event operand kind.
It does not accept arbitrary paths, string keys, an unknown field, or a nested
path such as `isBound(_event.data.sequence)`. This keeps compilation bounded
and preserves the current fail-fast syntax boundary.

`event_name.data` remains the binding witness for the whole Event. Therefore:

- before an Event is selected, every field query returns `false`;
- after selection, every required field query returns `true`;
- a non-`NULL`, zero-length view is a present empty field;
- structured `data` is present when `event_data_schema` and
  `event_data_object` are both bound, even though its scalar string view is
  intentionally `NULL`;
- omitting the complete system-values context is an evaluation error and does
  not modify the caller's output value.

This syntax is read-only and additive. It does not add a public structure,
function, ABI field, allocation, dependency, or state transition.

## State and ownership

`scxml_session_impl.system_values` is the sole Event-envelope fact source.
Program Event names are immutable program storage. External metadata is copied
into bounded session storage at admission and again into the current Event
envelope when selected. Structured data is copied and destroyed through its
CMeta type traits; the current call only borrows the session-owned object.

The expression evaluator never retains these views. They are valid only for
the current evaluation call and may be replaced when the next Event is
selected. Internal Events keep empty `origin`, `origintype`, `sendid`, and
`invokeid` views. External admission owns the supplied metadata independently
of the caller after `scxml_session_try_send_v2()` or `_v3()` succeeds.

## Host routing and transaction order

SCXML `<send>` is an external effect. A host adapter first validates and copies
the request during `prepare_send`; its ticket makes the reservation deliverable
only on `commit`. A host pump then resolves the destination and calls the
public bounded external-admission API. `discard` releases the reservation and
must never produce an Event.

The W3C test for an evaluated Event name uses this same order in a test-only
loopback adapter:

```text
evaluate eventexpr -> prepare/copy -> commit -> host pump
    -> external mailbox -> select Event -> read _event.name
```

No adapter callback recursively advances the interpreter, and no external
mailbox admission occurs while an effect is merely prepared.

## Error semantics

Compilation rejects malformed or unsupported `isBound` arguments. Evaluation
fails when the system-values context is absent. External admission reports its
existing bounded mailbox status; a full or invalid admission is not silently
retried or converted to an internal Event. A loopback reservation larger than
its fixed test capacity is rejected during preparation.

The runtime continues to classify raised Events as `internal`, processor error
Events as `platform`, and externally admitted Events as `external`. Optional
metadata is empty unless the corresponding external or invoke boundary
provides it.

## Compatibility and migration

Existing expressions, public headers, Event metadata layout, queue ordering,
and adapter ABI remain unchanged. Previously invalid field-qualified
`isBound` expressions become valid only for the seven standard Event fields.
There is no persisted data, configuration, or deployment migration.

The main compatibility risks are treating an unbound Event as an empty bound
Event, treating structured data as absent, or delivering a prepared but
discarded send. Focused expression tests and committed-loopback tests cover
those boundaries.

## Verification and rollback

Verification includes expression RED/GREEN tests, W3C-derived tests 330, 331,
333, 335, 337, and 342, manifest validation, the complete TurboSCXML CTest
suite, and `git diff --check`. Corpus transformations retain the named W3C
assertions while replacing only the upstream harness vocabulary.

Rollback removes the internal parser/evaluator extension and test-only
fixtures/adapters, then restores the six manifest rows to `UNSUPPORTED`. No
stored state or public ABI rollback is required.
