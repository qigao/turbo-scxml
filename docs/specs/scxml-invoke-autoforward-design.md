# SCXML Invoke Autoforward Design

## Context

W3C SCXML requires an `<invoke autoforward="true">` service to receive an
exact copy of every external Event selected by the parent processor. The copy
retains all seven Event fields: `name`, `type`, `sendid`, `origin`,
`origintype`, `invokeid`, and `data`. The authoritative assertions are
[test 229](https://www.w3.org/Voice/2013/scxml-irp/229/test229.txml) and
[test 230](https://www.w3.org/Voice/2013/scxml-irp/230/test230.txml).

TurboSCXML already starts and cancels autoforward invocations, calls
`prepare_forward` for selected external Events, stages accepted tickets in the
current host transaction, and executes the matching `<finalize>` before
forwarding a returned child Event. The missing boundary is field completeness:
`scxml_invoke_forward_request.event` exposes the CFlow Event ID and payload but
cannot represent the complete SCXML Event object.

The local uSCXML reference forwards its current Event object after finalize.
TurboSCXML keeps that ordering while exposing a bounded borrowed view rather
than sharing an owning object graph.

## Decision

Add a versioned `scxml_event_envelope_view` to the public API and append one
borrowed `envelope` pointer to `scxml_invoke_forward_request`:

```c
#define SCXML_EVENT_ENVELOPE_ABI 1u

typedef struct scxml_event_envelope_view {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    size_t name_size;
    const char *type;
    size_t type_size;
    const char *send_id;
    size_t send_id_size;
    const char *origin;
    size_t origin_size;
    const char *origin_type;
    size_t origin_type_size;
    const char *invoke_id;
    size_t invoke_id_size;
    scxml_content_view data;
} scxml_event_envelope_view;

typedef struct scxml_invoke_forward_request {
    uint64_t token;
    const char *id;
    size_t id_size;
    const cflow_event_view *event;
    const scxml_event_envelope_view *envelope;
} scxml_invoke_forward_request;
```

`event` remains available for existing adapters and preserves the current
CFlow identity/payload boundary. `envelope` is the canonical SCXML copy used by
new adapters. The adapter must validate `abi_version` and `struct_size` before
reading fields and must copy any field retained after `prepare_forward`
returns.

No adapter callback signature or `scxml_invoke_adapter` layout changes, so
`SCXML_ADAPTER_ABI` remains 1. Appending the request field preserves the old
prefix used by already compiled callbacks; source recompilation only exposes
the additive field.

## State, Ownership, and Ordering

`scxml_session_impl.system_values` remains the sole current-Event fact source.
`scxml_runtime_observe_event()` binds it before invocation preprocessing.
`forward_external_to_invocations()` builds one stack-local envelope view from
those values and passes the same immutable view to each active autoforward
invocation in descriptor order.

The data field is constructed as follows:

- structured data uses `SCXML_CONTENT_CMETA` with the session-owned schema and
  object;
- otherwise data uses `SCXML_CONTENT_TEXT_UTF8` with the current bounded byte
  view, including a present empty value.

All pointers are borrowed for one callback. Program names and event IDs are
immutable program storage; metadata strings are session-owned bounded copies;
structured data is session-owned and copied using its CMeta traits at external
admission. No callback may retain a raw view across return, the next selected
Event, a session reset, or session destruction.

The runtime order is unchanged:

```text
select external Event
  -> bind system_values
  -> resolve returned-child source
  -> execute matching finalize
  -> stage completion and autoforward tickets in invoke document order
  -> publish the staged finalize state to transition guards
  -> evaluate transitions and executable content
  -> commit all effect tickets on success, or discard all on failure
```

The session SerialExecutor is the only producer and consumer of the current
envelope. The registry mutex still protects only invocation-row snapshots; no
adapter callback runs while it is held. This change adds no queue, allocation,
retry, or capacity.

## Error Semantics

The runtime always supplies a non-NULL envelope with ABI 1 and the exact
structure size. A host that requires the complete Event rejects an incompatible
view as `SCXML_ADAPTER_INVALID_CONTRACT`. Existing adapter-result handling is
unchanged:

- an accepted ticket must provide both `commit` and `discard`;
- a recoverable adapter error raises the corresponding processor error Event;
- an invalid contract is fatal;
- transaction failure discards every already prepared ticket exactly once.

There is no fallback to partial metadata, reconstructed names, or independently
maintained host state.

## W3C-Derived Witnesses

`test229.scxml` keeps the upstream round-trip assertion. A bounded test host
reports `childToParent` through the live invocation token. The autoforward
ticket copies the Event during prepare; ticket commit only marks that copy
deliverable. After the executor becomes idle, the test host pump reports
`eventReceived` through the same token. The parent reaches `pass` only after
the returned Event is selected. No adapter callback recursively advances the
session.

`test230.scxml` admits one external Event with distinct literal values for
`sendid`, `origin`, `origintype`, `invokeid`, and text `data`. Its name and type
are bound by normal selection. The prepare callback compares all seven fields;
the host pump reports `fieldsEqual` only after a matching ticket commits. Any
missing, empty, changed, discarded, or late-read field prevents `pass`.

Both transformations remove only the upstream test-generator vocabulary and
replace its invoked child implementation with a deterministic bounded host
probe. They preserve the named conformance assertions.

## Compatibility and Migration

The public change is additive but ABI-sensitive for consumers that copy or
serialize request layouts outside the callback contract. Conforming adapters
only read borrowed fields during the callback and continue to work because the
old prefix and callback signature are unchanged. Consumers should rebuild and
prefer `request->envelope` for SCXML semantics.

Accepted XML, session configuration, persisted data, dependency graph, queue
ordering, capacities, and effect-ticket semantics do not change. After the two
rows move to `PASS`, the corpus becomes 134 mandatory passes, 34 mandatory
unsupported rows, and 34 optional not-applicable rows; 13 invoke assertions
remain.

## Verification and Rollback

Verification includes a compile-time RED test for the new request field, a
direct runtime test that copies and compares all seven fields during
`prepare_forward`, W3C-derived tests 229 and 230, manifest validation, a fresh
Windows Release build, the complete CTest suite, and `git diff --check`.

Rollback removes the envelope view and the two fixtures, restores manifest rows
229 and 230 to `UNSUPPORTED`, and restores the previous request initializer.
There is no stored-state or configuration migration to reverse.
