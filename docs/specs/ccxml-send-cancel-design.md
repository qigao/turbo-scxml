# CCXML Send Identifier and Cancel Design

## Status and scope

This slice extends the bounded CCXML executable-content profile with
`send/@sendid` and `<cancel>`:

```xml
<send target="'session:callee'" name="'call.notice'"
      delay="'250ms'" sendid="request.pending"/>
<cancel sendid="request.pending"/>
```

The [W3C CCXML send and cancel contract](https://www.w3.org/TR/ccxml/#Send)
defines `sendid` as an ECMAScript left-hand-side expression populated with a
unique identifier and `cancel/@sendid` as an expression resolving to that
identifier. This bounded profile admits a dotted NCName location for
`send/@sendid`. Cancel admits either a dotted readable string location or a
quoted string literal. Arbitrary ECMAScript expressions remain deferred.

## Compiler and session contract

The compiler retains the `sendid` location and charges it to
`max_name_bytes`. A send with `sendid` contributes two transactional effects:
one Event I/O send and one datamodel assignment. A cancel contributes one
Event I/O effect. Cancel literal values are XML-decoded before retention;
empty values, embedded NUL, malformed dotted locations, duplicate attributes,
and nonempty child content are rejected deterministically.

Session admission validates each send identifier location through
`validate_string_location`, each location-based cancel through
`validate_readable_string_location`, and requires
`SCXML_EVENT_IO_CAP_CANCEL` whenever cancel appears. The existing Event I/O
contract already requires cancel support to be paired with delayed-send
support and a non-null `prepare_cancel` callback.

## Identifier and transaction semantics

Each session using `sendid` receives a Salts UUID namespace and a monotonic
nonzero token. Generated identifiers use the bounded form
`send.<uuid>.<token>`, fit within `SCXML_EVENT_METADATA_CAPACITY`, and are
unique across sends and sessions subject to the UUID provider contract.

Dispatch first prepares the Event I/O send using the generated identifier,
then prepares assignment of the identical bytes to the datamodel location.
Both tickets are retained atomically. Their retained order is swapped so the
datamodel assignment commits before the externally visible send, matching the
existing generated conference/dialog identifier contract. Any later prepare
failure discards all retained tickets in reverse order. The token is consumed
once materialized even if preparation later fails, preventing reuse.

Cancel reads and validates a nonempty, NUL-free string when its operand is a
location, then forwards those exact bytes through `scxml_cancel_request` and
the shared Event I/O `prepare_cancel` path. Timer ownership, delayed-queue
races, and `cancel.successful` or `error.notallowed` event reinjection remain
the host Event I/O processor's responsibility.

Because external datamodel writes become visible only at transition commit, a
cancel in the same transition cannot consume a send identifier assigned by an
earlier action in that transition. The supported portable sequence cancels in
a later dispatched transition.

## Verification

Compiler tests cover valid locations and literals, invalid or unsupported
expressions, structure, retained-byte accounting, and effect capacity.
Runtime tests prove identifier equality, uniqueness, admission checks,
commit/discard ordering, cancellation forwarding, malformed values, adapter
failures, and built-in CMeta integration. All configured build matrices must
remain green.
