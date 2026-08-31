# SCXML Event provenance design

## Context

Three remaining mandatory SystemVariables assertions describe where an Event
came from and how that identity remains usable:

- a processor error caused by a failed `<send>` carries the triggering send ID;
- an external Event carries a routable `origin` and matching `origintype`;
- an Event returned by an invoked child carries that invocation's ID.

The authoritative fixtures are [test 332](https://www.w3.org/Voice/2013/scxml-irp/332/test332.txml),
[test 336](https://www.w3.org/Voice/2013/scxml-irp/336/test336.txml), and
[test 338](https://www.w3.org/Voice/2013/scxml-irp/338/test338.txml).
TurboSCXML already owns bounded Event metadata rows, host-defined external
routing, generated send/invoke IDs, and live invocation tokens. The missing
production behavior is attaching a failed send's ID to its platform error
Event while preserving the generated `idlocation` value that names the same
attempt.

## Failed-send identity

`execute_send()` materializes the request and any `idlocation` before calling
the Event I/O adapter. A recoverable adapter status continues to raise the same
prioritized internal `error.execution` or `error.communication` Event and
abort the remainder of the executable-content block.

When the materialized request has a nonempty ID, the error Event is raised as a
tagged internal Event backed by one existing bounded metadata row whose only
nonempty field is `send_id`. The Event remains `platform`; it is not converted
to external admission and does not change internal-before-external ordering.
The metadata row is session-owned and transaction-coupled: reservation failure
is fatal, a failed raise/stage releases it, transaction discard releases it,
and selection consumes it through the existing current-Event copy path.

The executable block continues to roll back ordinary CMeta assignments on a
recoverable error. A generated send `idlocation` is the one narrow exception
because SCXML exposes it as the identity of the attempted send. The owning
session retains one bounded, executor-only restore record containing the
compiled location and generated ID. After the block restores its input state,
`scxml_runtime_execute_block()` reapplies exactly that owned-string location
before publishing the state. No earlier assignment, later statement, adapter
effect, or unrelated location is retained.

The restore record is not a second business fact source. It exists only during
one synchronous executable callback, is cleared at entry and on every exit,
and is never observed by another thread. The tagged error metadata row remains
the sole source for `_event.sendid`.

If a failed request has an explicit `id` but no `idlocation`, only the error
metadata is attached. If it has neither, the existing empty `sendid` remains.
Fatal adapter-contract violations remain fatal and do not synthesize a
recoverable error Event.

## Routable external origin

Routing remains a host responsibility. The W3C fixture uses a bounded
test-only single-session host with a two-message committed loopback:

```text
send foo -> prepare/copy -> commit -> host admission
         -> Event(origin=session location, origintype=SCXML processor URI)
         -> send bar targetexpr=_event.origin typeexpr=_event.origintype
         -> prepare validates reply target/type -> commit -> host admission
```

Only committed reservations are admitted. The fixed queue permits two
business sends and one terminal result; overflow, invalid target/type, missing
commit, or admission failure fails the fixture. Production code does not infer
or invent origin values.

## Invocation return identity

The invoke adapter owns a live token and generated invocation ID. A test host
reports a normal child Event with `scxml_session_report_invoke_event()`. The
session validates the token at admission and again during preprocessing; Event
selection resolves the same live invocation row and binds its ID to
`_event.invokeid`. Leaving the invoking state uses the existing cancel
transaction. Stale tokens remain rejected.

## Compatibility and error semantics

No public function, structure, ABI version, queue capacity, event priority, or
adapter status changes. Recoverable send failures keep their existing error
kind and block-abort behavior. The observable addition is the standard
`sendid` metadata and persistence of the corresponding generated
`idlocation`. All storage is fixed and bounded by existing metadata capacity.

The compatibility risk is accidentally retaining unrelated staged state or
publishing metadata for a discarded Event. Tests require ordinary assignments
to remain rolled back, exactly one generated ID to survive, and discarded
metadata never to become selectable.

## Verification and rollback

Verification includes a RED/GREEN CMeta runtime test for failed-send identity,
W3C-derived tests 332, 336, and 338, existing block-rollback and Event-envelope
regressions, manifest validation, a fresh Release build, the complete CTest
suite, and `git diff --check`.

Rollback removes the internal restore record and tagged send-error metadata,
returns the three manifest rows to `UNSUPPORTED`, and removes the test-only
routing extensions. There is no public ABI, persisted data, or configuration
migration to reverse.
