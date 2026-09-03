# CCXML Join Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with the smallest
useful bridge operation:

```xml
<join id1="'call-a'" id2="'call-b'"/>
```

Both identifiers are required nonempty string-literal expressions. Omitting
`duplex` selects the CCXML default of full-duplex media. The optional `duplex`,
`hints`, `entertone`, `exittone`, `autoinputgain`, `autooutputgain`,
`dtmfclamp`, and `toneclamp` attributes remain unsupported, as do nested
content, escaped literals, and arbitrary ECMAScript expressions.

The core does not distinguish connection, conference, and dialog identifiers.
The provider owns the authoritative resource registry, session-ownership
checks, bridge graph, media resources, and asynchronous result delivery.

## Compiler contract

`id1` and `id2` are the only admitted unqualified attributes and each must
occur exactly once. Each value must be a single-quoted or double-quoted
nonempty string literal. Missing attributes or quoted empty literals return
`CCXML_INVALID_STRUCTURE`; malformed, escaped, namespaced, or additional
attributes return `CCXML_UNSUPPORTED_FEATURE`. A duplicate unqualified XML
attribute is rejected earlier as `CCXML_XML_ERROR` by the XML parser.

The compiler strips the expression quotes and copies both identifier byte
ranges into program-owned storage. Each retained range includes one trailing
NUL in the existing `max_name_bytes` accounting, although the runtime contract
continues to carry explicit sizes. Nested executable content is rejected.
Programs containing join record a `uses_join` capability bit.

## Adapter contract

The v1 telephony function table gains one append-only operation after
`prepare_redirect`:

```c
scxml_adapter_status (*prepare_join)(
    void *user,
    const ccxml_join_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_join_request` contains borrowed `id1`/`id1_size` and `id2`/`id2_size`
views. They are valid only during `prepare_join`; a provider retaining them
must copy them before returning. A program containing `<join>` requires the
complete callback field and a non-NULL callback. Redirect-era and older
adapter prefixes remain valid for programs that do not use join.

The callback returns the existing move-only effect ticket. Actions prepare
and commit in document order, while failure discards already prepared tickets
in reverse order. Provider refusal maps to `CCXML_ADAPTER_ERROR`; an accepted
ticket missing either callback maps to `CCXML_INVALID_CONTRACT`.

## Bridge and event contract

Committing a join ticket asks the provider to create a full-duplex bridge. It
does not prove that either resource exists, mutate a core-owned resource
registry, or terminate the CCXML session. The provider validates identifiers,
resource kinds, ownership, existing bridge constraints, and media capacity.

On success the provider injects `conference.joined` with the same ordered
`id1` and `id2` values. On failure it injects `error.conference.join` only to
the requesting session. The existing event-dispatch boundary transports those
events; the core does not synthesize them from a successful prepare or commit.

## Ownership and lifecycle

- The compiled program owns both decoded identifier byte ranges until program
  destruction.
- The session borrows the program and owns copied adapter operations and
  in-flight effect tickets under the existing close/quiescence contract.
- The provider owns resource objects, bridges, media allocation, result-event
  storage, and any cross-session conference notification fan-out.
- No callback may retain a request view after returning without copying it.

## Verification

The slice is complete when tests prove:

1. both literals compile, retain their exact bytes, and survive source
   overwrite;
2. missing, empty, nonliteral, escaped, duplicate/extra, and nested forms fail
   at the documented boundary;
3. both strings consume the retained-name budget;
4. dispatch passes the exact ordered pair, commits once, and leaves the
   session active;
5. mixed actions commit in document order and provider refusal rolls earlier
   effects back in reverse order;
6. older adapter prefixes remain valid without join, while join programs
   reject absent, NULL, or truncated callback tails;
7. all supported presets and installed consumers remain green.
