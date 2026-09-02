# CCXML Core MVP Design

## Status and scope

This document defines the first executable CCXML slice hosted by the
TurboSCXML package. It is an incubation boundary, not a claim of complete
W3C CCXML 1.0 conformance.

The slice accepts a bounded CCXML document containing one `<eventprocessor>`
whose `<transition>` children use exact event names and whose executable
content contains empty `<accept/>` and `<exit/>` actions. A session dispatches
one external event synchronously, selects the first matching transition in
document order, stages every telephony effect, and either commits all staged
effects or discards them all.

SIP, RTP, conferences, dialogs, document replacement, event-name patterns,
ECMAScript expressions, `<send>`, and VoiceXML are outside this slice. The
compiler rejects these constructs instead of silently approximating them.

Normative references:

- <https://www.w3.org/TR/ccxml/>
- <https://www.w3.org/standards/history/ccxml/>

## Package boundary

The implementation is an always-built static library exported as
`TurboSCXML::CCXML`. Its public header is `<ccxml/ccxml.h>`. The dependency is
one-way:

```text
application -> TurboSCXML::CCXML -> TurboSCXML::SCXML -> Rocida
```

The first slice deliberately reuses the SCXML adapter status and CFlow
move-only effect ticket contracts. CCXML owns its XML syntax, program, session,
event selection, and telephony vocabulary; no CCXML element is admitted by the
SCXML compiler.

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

The root namespace and version are exact. There must be exactly one
`eventprocessor`. A transition must have exactly one nonempty `event`
attribute. Names are copied and bounded by `max_name_bytes`. Foreign elements,
extra attributes, non-whitespace text, and unsupported standard elements return
`CCXML_UNSUPPORTED_FEATURE` or `CCXML_INVALID_STRUCTURE` with a source
diagnostic.

The event supplied to `ccxml_session_dispatch` has an exact name and an
optional connection identifier. `<accept/>` uses the current event's
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

- Programs own transition rows, action rows, and copied event-name bytes.
- Sessions own only transaction scratch storage and a copied adapter table.
- Sessions borrow programs and adapter users.
- Compile and destruction are single-owner operations.
- Dispatch, close, and destroy are serialized by the caller in this MVP.
- Provider callbacks must be nonblocking; asynchronous work remains provider
  owned and is protected by its ticket and quiescence contract.
- There is no process-global mutable state or service locator.

## Limits and diagnostics

`ccxml_default_limits()` starts from `turbo_xml_default_limits()` and supplies
positive bounds for transitions, actions, and retained event-name bytes. Zero
limits are invalid. Count, size, and addition overflow is detected before
allocation. Compile failure leaves the output program empty.

Diagnostics carry a CCXML status, XML byte/line/column location, and one
bounded message. XML parser status maps without exposing parser-owned storage.

## Verification

The MVP is complete when tests prove:

1. valid source compiles and copies its input;
2. wrong namespace/version and unsupported syntax fail deterministically;
3. the first exact matching transition stages and commits `<accept/>`;
4. unmatched events produce no provider effect;
5. adapter rejection discards earlier tickets and preserves a live session;
6. invalid accepted tickets are rejected;
7. `<exit/>` terminates and closes exactly once;
8. destroy waits for provider quiescence;
9. the installed C and C++ consumer can include and link `TurboSCXML::CCXML`.

