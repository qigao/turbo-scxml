# SCXML Invoke Finalize Conformance Design

## Context

W3C SCXML 1.0 requires an invoked service's returned Event to execute the
matching `<invoke>` element's `<finalize>` content immediately before that
Event is removed from the external queue for transition selection. No other
live invocation may execute its finalize block for that Event.

TurboSCXML already represents returned Events with the invocation token passed
to `scxml_session_report_invoke_event()`. The session registry is the sole fact
source for mapping that token to one active invocation descriptor. This change
adds executable W3C-derived witnesses for tests 233 and 234; it does not add a
second mapping, child interpreter, transport, or public API.

## Required Behavior

For a returned non-completion Event selected from the parent's external queue:

1. Resolve its nonzero source token against the active invocation registry.
2. Reject a stale token before any finalize content or transition executes.
3. Execute only the resolved descriptor's finalize block.
4. Commit finalize assignments before evaluating Event transitions.
5. Preserve the Event for ordinary transition selection after preprocessing.

Test 233 uses one invocation whose finalize block changes `sequence` from `1`
to `2`; the same returned Event can reach `pass` only if its guard observes the
committed value `2`.

Test 234 uses two live invocations and one integer as a compact independent
witness. The first finalize maps `11` to `21`, while the second maps `11` to
`12`. An Event returned through the first token passes only when the resulting
value is exactly `21`. Running neither finalize, running only the wrong one, or
running both cannot satisfy the guard.

## Ownership and Ordering

- The `scxml_session` owns the invocation registry, external FIFO, CMeta state,
  and all runtime mutations.
- The test host borrows callback request fields and retains only copied IDs and
  scalar tokens.
- `scxml_session_report_invoke_event()` copies the Event into the bounded
  external FIFO; the caller retains no queued storage.
- Finalize and transition selection execute on the session SerialExecutor.
- Event binding and finalize execute in CFlow host ABI V4
  `PREPARE_TRIGGER`; the lazy staged CMeta state commits before guards run.
- Completion bookkeeping and autoforward adapter tickets are staged in that
  same transaction, so a fatal result discards them with the state edit.
- Leaving the invoking state cancels every invocation still active after the
  returned Event is processed. Cancellation is an observable cleanup effect,
  not a second source of state.

## Errors and Resource Bounds

- Unknown or terminal invocation tokens are rejected by the public report API.
- Invalid finalize executable content fails through the existing runtime error
  path; the conformance fixtures require a non-errored terminal session.
- The fixtures admit at most two invocations and one returned Event. Session
  capacities remain explicit and no unbounded allocation or retry path is
  introduced.
- Adapter tickets retain the existing exactly-one commit-or-discard contract.

## Compatibility

Public TurboSCXML API/ABI and data formats do not change. The runtime now
requires the CFlow V4 host transaction ABI and no longer composes V2/V3 hook
tables. The compiler admits the existing bounded CMeta assignment subset in
`finalize`; unsupported external-effect elements remain fail-fast.

## Verification

- Focused TinyTest filters for tests 233 and 234 must pass.
- A temporary local mutation that skips `execute_invocation_finalize()` must
  make both focused tests fail; the mutation is then reverted before delivery.
- The manifest must contain 202 rows: 168 mandatory, 34 optional, 132 PASS,
  36 UNSUPPORTED, and 34 N/A.
- Windows Release build and all CTest entries must pass.

Upstream requirements:

- https://www.w3.org/TR/2015/REC-scxml-20150901/#invoke
- https://www.w3.org/Voice/2013/scxml-irp/233/test233.txml
- https://www.w3.org/Voice/2013/scxml-irp/234/test234.txml
