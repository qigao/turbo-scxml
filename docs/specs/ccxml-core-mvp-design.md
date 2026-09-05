# CCXML Core MVP Design

## Status and scope

This document defines the first executable CCXML slice hosted by the
TurboSCXML package. It is an incubation boundary, not a claim of complete
W3C CCXML 1.0 conformance.

The slice accepts a bounded CCXML document containing one `<eventprocessor>`
whose `<transition>` children use case-insensitive CCXML event patterns and
optional CMeta-backed string state filtering and boolean conditions. A session
dispatches one external event synchronously, selects the first matching
transition in document order, stages every action effect, and either commits
all staged effects or discards them all.

SIP/RTP backends, document replacement, general ECMAScript, full `<send>`
expressions/inline payloads, and a built-in VoiceXML interpreter are outside this
slice. The compiler rejects unsupported constructs instead of silently
approximating them.

Normative references:

- <https://www.w3.org/TR/ccxml/>
- <https://www.w3.org/standards/history/ccxml/>

## Package boundary

The implementation is an always-built static library exported as
`TurboSCXML::CCXML`. Its public header is `<ccxml/ccxml.h>`. The dependency is
one-way:

```text
application -> TurboSCXML::CCXML -> TurboSCXML::SCXML -> Salts
```

The first slice deliberately reuses the SCXML adapter status and CFlow
move-only effect ticket contracts. Its restricted `<send>` action also reuses
the `scxml_event_io_adapter` table and `scxml_send_request` envelope while the
session-bound host retains CCXML target-type semantics. CCXML owns its XML
syntax, program, session, event selection, and telephony vocabulary; no CCXML
element is admitted by the SCXML compiler.

## Public contract

`ccxml_program` and `ccxml_session` are opaque, single-owner handles. Every
successful initializer has one matching destroy operation. Inputs and XML
strings are copied by `ccxml_compile`; a session borrows its program and
telephony adapter user until successful destruction.

`ccxml_compile` accepts only:

```xml
<ccxml xmlns="http://www.w3.org/2002/09/ccxml" version="1.0">
  <eventprocessor>
    <transition event="connection.alerting">
      <accept/>
    </transition>
    <transition event="connection.disconnected">
      <exit/>
    </transition>
  </eventprocessor>
</ccxml>
```

The bounded state-machine extension additionally accepts one root string
`<var name="..." expr="'literal'"/>`, a matching
`eventprocessor@statevariable`, whitespace-separated `transition@state`
values, and `<assign name="..." expr="'literal'"/>` targeting that declared
variable. A transition may also carry one nonempty `cond` whose XML entities
decode to a bounded datamodel expression. It does not provide a general
ECMAScript evaluator.

The root namespace and version are exact. There must be exactly one
`eventprocessor`. A transition may have one nonempty `event` pattern; omitting
it compiles to the catch-all pattern `*`. Pattern matching is ASCII
case-insensitive and each `*` matches zero or more event-name characters.
Patterns are copied and bounded by `max_name_bytes`. Foreign elements, extra
attributes, non-whitespace text, and unsupported standard elements return
`CCXML_UNSUPPORTED_FEATURE` or `CCXML_INVALID_STRUCTURE` with a source
diagnostic.

The root variable and every assignment location are validated through the
write side of `ccxml_datamodel_adapter_v1`; the statevariable is validated
through its readable tail. Session initialization commits the declared
literal only after the native CFlow instance is ready. A state-filtered CFlow
guard reads the current string before selecting a transition and compares it
case-sensitively with the whitespace-separated state tokens. A read failure
stops selection for that event. `<assign>` produces an ordinary move-only
effect ticket, so state changes commit or roll back with all other effects in
the selected transition.

Condition source bytes are XML-decoded and copied into the immutable program.
Because the datamodel schema is bound at session initialization, condition
syntax and types are compiled during session admission through the optional
tail of `ccxml_datamodel_adapter_v1`. Each successful compile returns one
opaque `ccxml_condition` owned by the session and destroyed exactly once on
admission rollback or session destruction. Adapters without the tail remain
compatible for programs that do not contain `cond`.

The built-in CMeta adapter compiles each condition once through the existing
TurboSCXML expression VM, using its configured path and string bounds. It
supports CMeta boolean expressions over the root schema plus `_event.name`;
SCXML `In()` and general ECMAScript are outside this profile. A CFlow guard
evaluates the condition only after its event and optional state match. False
continues document-order selection; compile or evaluation failure returns an
adapter error and prevents later guards from handling that event. Root variable
initializers commit only after every condition and the native CFlow instance
have initialized successfully.

Executable content also accepts a nested `<if cond="...">` block. Its direct
children are ordinary supported actions, zero or more empty
`<elseif cond="..."/>` branch markers, and at most one empty `<else/>` marker.
The compiler lowers each block to immutable `IF`, `ELSEIF`, `ELSE`, and `ENDIF`
rows; every control row counts against `max_actions`, while only selected leaf
actions consume effect-ticket capacity. Session admission compiles every `IF`
and `ELSEIF` source through the same condition adapter tail. Dispatch uses
preallocated frame scratch to evaluate branches in order, skip non-selected
content, and roll back already prepared effects on an evaluation failure.
XPath and a general ECMAScript runtime remain outside this bounded profile.

Executable content also accepts a bounded `<foreach array="..." item="...">`
profile when the session uses the built-in CMeta datamodel adapter. `array` is
a dotted location resolving to an ordered, sized CMeta sequence. Admission
registers `item` as a typed supplemental-scope slot and compiles the sequence
with the shared SCXML foreach engine; it never aliases the item into the live
application root. Each opening snapshots the finite sequence before executing
its body, so later source-container mutation cannot change that iteration.

The session owns committed, staged, and checkpoint scope views. Opening a
block copies committed state to staged state and checkpoints the block;
iteration writes only the staged typed item. The last item becomes committed
only with the selected transition's aggregate effect ticket. Any snapshot,
iteration, action-prepare, or CFlow staging failure destroys live values,
reverse-discards provider tickets, and clears the staged scope. The source
container and CMeta state remain borrowed and must outlive the session.
Bounded `if`/`elseif`/`else` content is admitted inside the body. Its CMeta
condition program retains the supplemental schema and evaluates against the
current staged scope view, so each branch observes the current typed item and
shares transition-wide rollback.
An optional `index` is a distinct NCName bound as a supplemental `size_t`; it
must not resolve to an application-root location. It is updated atomically with
the item for each iteration and follows the same staged, checkpoint, and
committed lifecycle.
The admitted element type must provide trivial-copy and trivial-destroy traits,
and its semantic `cmeta_data_desc` must be built in, reachable from the root
schema, or explicitly supplied through the optional size-versioned
`ccxml_cmeta_datamodel_config_v1.semantic_data` registry. Registered descriptors
are resolved by `cmeta_type_equal`, so separately emitted multi-translation-unit
descriptors with the same semantic identity still match. The datamodel owner
copies the pointer table; descriptor objects remain borrowed and must outlive
every attached session. Registry entries must be valid, have storage types, and
have no duplicate semantic storage type. Registry lookup precedes the
root-reachable fallback, allowing a structured sequence element to expose
`item.member` without an artificial root field. The original v1 config prefix
ending at `max_string_bytes` remains accepted through `struct_size`.
Opaque types and managed-copy types are rejected before attachment: the latter
can allocate through user lifecycle hooks and therefore cannot honor this
profile's hard byte budget.
`max_foreach_iterations` bounds sequence expansion and ticket admission;
`max_foreach_storage_bytes` bounds the aggregate allocation for all three
views plus the largest admitted snapshot and managed-item scratch value.
This profile rejects nested foreach, opaque elements, and managed-copy elements.
The public datamodel-adapter ABI remains unchanged.

The event supplied to `ccxml_session_dispatch` has a bounded name and an
optional connection identifier. The first matching transition is selected in
document order. An unhandled `error.*`, `ccxml.kill`, or `ccxml.kill.*` event
terminates the session; other unmatched events are dropped. `<accept/>` uses the current event's
connection identifier, matching CCXML's current-event default. It fails with
`CCXML_INVALID_EVENT` if no identifier is present.

The telephony bridge is an injected function table:

```c
typedef struct ccxml_telephony_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_adapter_status (*prepare_accept)(
        void *user,
        const ccxml_accept_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} ccxml_telephony_adapter_v1;
```

An accepted prepare must return both `commit` and `discard`. The session calls
neither callback while another provider callback is active. After all actions
prepare successfully it commits tickets in document order. On any failure it
discards prior tickets in reverse order. Adapter rejection maps to
`CCXML_ADAPTER_ERROR`; a malformed accepted ticket maps to
`CCXML_INVALID_CONTRACT`.

`<exit/>` marks the session terminated only after all preceding effects commit.
Termination closes the adapter once. Further dispatch returns `CCXML_CLOSED`.
Explicit `ccxml_session_close` is idempotent and also closes the adapter once.
`ccxml_session_destroy` returns `CCXML_BUSY` until `is_quiescent` is true.

## Ownership and concurrency

- Programs own transition rows, action rows, and copied event/state/condition/
  literal bytes.
- Sessions own transaction scratch storage, copied adapter tables, compiled
  condition handles, foreach sequence programs, sequence snapshots, and the
  three typed foreach scope views.
- Sessions borrow programs, adapter users, and the CMeta state supplied to the
  datamodel adapter.
- Compile and destruction are single-owner operations.
- Dispatch, close, and destroy are serialized by the caller in this MVP.
- Provider callbacks must be nonblocking; asynchronous work remains provider
  owned and is protected by its ticket and quiescence contract.
- There is no process-global mutable state or service locator.

## Limits and diagnostics

`ccxml_default_limits()` starts from `salts_xml_default_limits()` and supplies
positive bounds for transitions, actions, retained event-name bytes, foreach
iterations, and foreach scope storage. Zero limits are invalid. Count, size,
addition, and foreach-expansion multiplication overflow is detected before
allocation. Compile failure leaves the output program empty.

Diagnostics carry a CCXML status, XML byte/line/column location, and one
bounded message. XML parser status maps without exposing parser-owned storage.

## Verification

The MVP is complete when tests prove:

1. valid source compiles and copies its input;
2. wrong namespace/version and unsupported syntax fail deterministically;
3. the first case-insensitive wildcard-matching transition stages and commits
   `<accept/>`;
4. unmatched events produce no provider effect;
5. adapter rejection discards earlier tickets and preserves a live session;
6. invalid accepted tickets are rejected;
7. `<exit/>` terminates and closes exactly once;
8. destroy waits for provider quiescence;
9. the installed C and C++ consumer can include and link `TurboSCXML::CCXML`.
10. statevariable guards select by the current CMeta string, assignment
    effects commit atomically, and later action failure rolls them back.
11. CMeta conditions compile once per session, reevaluate against committed
    state, stop later guards on failure, and release every opaque handle.
12. typed CMeta foreach sequences prepare every expanded action before commit,
    preserve the last typed item only on success, and clear staged values while
    reverse-discarding earlier tickets on failure.
13. conditions inside foreach read the current staged typed item, select only
    the matching branch, and remain inside the aggregate transition journal.
14. opaque or managed-copy foreach elements fail admission before allocating
    scope views or executing provider effects.
15. a foreach index is visible as the current staged `size_t`, commits the final
    zero-based value, and clears with the rest of the staged scope.
