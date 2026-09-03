# CCXML Merge Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with network-level
connection merge:

```xml
<merge connectionid1="'call-a'" connectionid2="'call-b'"/>
```

Both connection identifiers are required nonempty string-literal expressions.
The optional `hints` attribute, nested content, escaped literals, and arbitrary
ECMAScript expressions remain unsupported.

The core compiles and transactionally dispatches the command. The provider
owns authoritative connection and bridge state, session-ownership checks,
network capability, signaling, media teardown, and asynchronous result events.

## Compiler contract

`connectionid1` and `connectionid2` are the only admitted unqualified
attributes and each must occur exactly once. Missing attributes or quoted
empty literals return `CCXML_INVALID_STRUCTURE`; malformed, escaped,
namespaced, or additional attributes return `CCXML_UNSUPPORTED_FEATURE`.
Duplicate unqualified XML attributes are rejected earlier as `CCXML_XML_ERROR`
by the XML parser.

The compiler strips the expression quotes and copies both identifiers into
program-owned storage. Each retained range includes one trailing NUL in the
existing `max_name_bytes` accounting, while runtime requests retain explicit
sizes. Nested executable content is rejected. Programs containing merge record
a `uses_merge` capability bit.

## Adapter contract

The v1 telephony function table gains one append-only operation after
`prepare_unjoin`:

```c
scxml_adapter_status (*prepare_merge)(
    void *user,
    const ccxml_merge_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_merge_request` contains borrowed `connection_id1`/size and
`connection_id2`/size views. They are valid only during `prepare_merge`; a
provider retaining them must copy them before returning. A program containing
`<merge>` requires the complete callback field and a non-NULL callback.
Unjoin-era adapter prefixes remain valid for programs that do not use merge.

The callback returns the existing move-only effect ticket. Actions prepare and
commit in document order, while failure discards prepared tickets in reverse
order. Provider refusal maps to `CCXML_ADAPTER_ERROR`; an accepted malformed
ticket maps to `CCXML_INVALID_CONTRACT`.

## Connection and event contract

Committing a merge ticket asks the provider to merge the two ordered
connections at network level. The provider verifies that both resources are
connections handled by the requesting session and that the underlying network
supports the operation. It tears down affected bridges and media paths.

On success the provider emits `connection.merged` for each affected connection
and any required `conference.unjoined` events. On failure it emits one
`connection.merge.failed` event identifying both connections. The core does
not pre-validate connection state, mutate provider connection objects,
terminate the CCXML session, or synthesize results from prepare/commit success.

The ordering of the two identifiers has no platform semantic effect, but their
source order is preserved in the request and failure event so diagnostics and
provider behavior remain deterministic.

## Ownership and lifecycle

- The compiled program owns both decoded identifier ranges until destruction.
- The session borrows the program and owns copied adapter operations and
  in-flight effect tickets under the existing close/quiescence contract.
- The provider owns connection objects, network signaling, bridge/media
  teardown, result-event storage, and event delivery.
- No callback may retain request views after returning without copying them.

## Verification

The slice is complete when tests prove strict syntax and retained-budget
failures, exact source-independent request bytes, ordered commit and reverse
rollback, session non-termination, unjoin-era prefix compatibility, required
callback-tail validation, and a green supported build/install matrix.
