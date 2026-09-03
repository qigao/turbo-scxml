# CCXML Unjoin Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with bridge teardown:

```xml
<unjoin id1="'call-a'" id2="'conference-b'"/>
```

Both identifiers are required nonempty string-literal expressions. The
optional `hints` attribute, nested content, escaped literals, and arbitrary
ECMAScript expressions remain unsupported.

The core does not distinguish connection, conference, and dialog identifiers.
The provider owns the authoritative resource registry, session-ownership
checks, bridge graph, media resources, and asynchronous result delivery.

## Compiler contract

`id1` and `id2` are the only admitted unqualified attributes and each must
occur exactly once. Missing attributes or quoted empty literals return
`CCXML_INVALID_STRUCTURE`; malformed, escaped, namespaced, or additional
attributes return `CCXML_UNSUPPORTED_FEATURE`. Duplicate unqualified XML
attributes are rejected earlier as `CCXML_XML_ERROR` by the XML parser.

The compiler strips the expression quotes and copies both identifiers into
program-owned storage. Each retained range includes one trailing NUL in the
existing `max_name_bytes` accounting. Nested executable content is rejected.
Programs containing unjoin record a `uses_unjoin` capability bit.

## Adapter contract

The v1 telephony function table gains one append-only operation after
`prepare_join`:

```c
scxml_adapter_status (*prepare_unjoin)(
    void *user,
    const ccxml_unjoin_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

The request contains borrowed `id1`/`id1_size` and `id2`/`id2_size` views.
They are valid only during `prepare_unjoin`; providers must copy retained
values. Programs containing `<unjoin>` require the complete callback field and
a non-NULL callback. Join-era adapter prefixes remain valid for programs that
do not use unjoin.

The callback returns the existing move-only effect ticket. Actions prepare and
commit in document order; failure discards prepared tickets in reverse order.
Provider refusal maps to `CCXML_ADAPTER_ERROR`, and malformed accepted tickets
map to `CCXML_INVALID_CONTRACT`.

## Bridge and event contract

Committing an unjoin ticket asks the provider to remove the bridge between the
ordered resources. It does not prove that either resource or bridge exists and
does not terminate the CCXML session. The provider validates identifiers,
resource kinds, session ownership, and the existing bridge relationship.

On success the provider injects `conference.unjoined`; on failure it injects
`error.conference.unjoin` to the requesting session. The provider performs any
conference fan-out and media teardown. The core does not synthesize result
events from successful prepare or commit.

## Ownership and lifecycle

- The compiled program owns both decoded identifiers until destruction.
- The session borrows the program and owns copied adapter operations and
  in-flight effect tickets under the existing close/quiescence contract.
- The provider owns resources, bridges, media teardown, result-event storage,
  and cross-session notification fan-out.
- No callback may retain request views after returning without copying them.

## Verification

The slice is complete when tests prove strict syntax and retained-budget
failures, exact source-independent request bytes, ordered commit and reverse
rollback, session non-termination, join-era prefix compatibility, required
callback-tail validation, and a green supported build/install matrix.
