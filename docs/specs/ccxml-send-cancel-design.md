# CCXML Send Identifier and Cancel Design

## Status and scope

This slice extends the bounded CCXML executable-content profile with
`send/@sendid` and `<cancel>`:

```xml
<send target="'session:callee'" name="'call.notice'"
      delay="'250ms'" sendid="request.pending"/>
<cancel sendid="request.pending"/>
<cancel sendid="(request.pending)"/>
<cancel sendid="('send.' + count)"/>
```

The [W3C CCXML send and cancel contract](https://www.w3.org/TR/ccxml/#Send)
defines `sendid` as an ECMAScript left-hand-side expression populated with a
unique identifier and `cancel/@sendid` as an expression resolving to that
identifier. This bounded profile admits a dotted NCName location for
`send/@sendid`. Cancel admits either a dotted readable string location or a
quoted string literal, or a dynamic string expression through the selected
CMeta/QuickJS-NG adapter. Plain NCName paths retain the original read callback;
parenthesized paths and other nonliteral sources use expression callbacks.

## Compiler and session contract

The compiler retains the `sendid` location and charges it to
`max_name_bytes`. A send with `sendid` contributes two transactional effects:
one Event I/O send and one datamodel assignment. A cancel contributes one
Event I/O effect. Cancel literal values are XML-decoded before retention;
empty values, embedded NUL, duplicate attributes,
and nonempty child content are rejected deterministically.

Dynamic sources are XML-decoded and copied into Program storage within the same
`max_name_bytes` budget. The existing destination expression slot is reused for
cancel; no public layout or adapter ABI changes. Session admission compiles the
source and validates its syntax, requiring the complete string-expression tail.
Invalid expressions fail admission instead of falling back to variable reads.
Literal-only and location-only programs keep their original adapter requirements.

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
after reading any namelist values from the committed datamodel,
then prepares assignment of the identical bytes to the datamodel location.
Both tickets are retained atomically. Their retained order is swapped so the
datamodel assignment commits before the externally visible send, matching the
existing generated conference/dialog identifier contract. Any later prepare
failure discards all retained tickets in reverse order. The token is consumed
once materialized even if preparation later fails, preventing reuse.

Cancel reads and validates a nonempty, NUL-free string when its operand is a
location, or evaluates its compiled string expression, then forwards those exact bytes through `scxml_cancel_request` and
the shared Event I/O `prepare_cancel` path. Timer ownership, delayed-queue
races, and `cancel.successful` or `error.notallowed` event reinjection remain
the host Event I/O processor's responsibility.

An evaluated ID must be an actual nonempty NUL-free string no larger than
`SCXML_EVENT_METADATA_CAPACITY`; invalid results return `CCXML_INVALID_CONTRACT`.
VM errors and provider rejection retain their existing status mappings. The
result is borrowed through the immediate `prepare_cancel` callback; any provider
ticket must copy bytes it retains. No expression result is kept by the Session
across callbacks or dispatches. Compilation handles follow the existing bounded
admission/destruction protocol. Dispatch remains serialized by its Session owner.
Failure before or after cancel preparation discards all prior transition tickets.
The host still checks cancellation ownership and races with already delivered events.

Because external datamodel writes become visible only at transition commit, a
cancel in the same transition cannot consume a send identifier assigned by an
earlier action in that transition. The supported portable sequence cancels in
a later dispatched transition. Namelist reads have the same committed-state
rule and do not observe assignments staged earlier in their transition.
Dynamic cancellation observes the same committed state; it does not make staged
IDs visible early. Applications may migrate an operand to an expression or revert
it to a literal/path without changing provider contracts.

## Verification

Compiler tests cover valid locations and literals, invalid or unsupported
expressions, structure, retained-byte accounting, and effect capacity.
Runtime tests prove identifier equality, uniqueness, admission checks,
commit/discard ordering, cancellation forwarding, malformed values, adapter
failures, expression scratch ownership and partial admission cleanup, bounded
QuickJS results, and built-in CMeta integration. All configured build matrices must
remain green.
