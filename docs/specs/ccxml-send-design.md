# CCXML Send Slice Design

## Status and scope

This slice adds the first bounded CCXML `<send>` profile. It accepts an empty
action with required `target` and `name` string-expression attributes,
optional literal-expression `targettype` and `delay` attributes, and an
optional dotted-location `sendid` and bounded dotted-location `namelist`:

```xml
<send target="'session:callee'" name="'call.notice'"/>
<send target="('session:' + count)" name="'call.notice'"/>
<send target="('session:' + count)" name="('call.' + event$.name)"/>
<send target="'https://example.test/events'"
      targettype="'basichttp'" name="'call.notice'" delay="'250ms'"/>
<send target="'session:callee'" name="'call.notice'"
      delay="'250ms'" sendid="request.pending"/>
<send target="'session:callee'" name="'call.notice'"
      delay="payload.count"/>
<send target="'session:callee'" name="'call.notice'"
      namelist="conference.id count"/>
<cancel sendid="request.pending"/>
```

When `targettype` is omitted, the compiled value is `ccxml`; omitted `delay`
is zero. `sendid` is a dotted writable string location populated with a
generated identifier. `<cancel>` accepts a dotted readable string location or
a quoted literal identifier. `namelist` accepts at most
`SCXML_PAYLOAD_MAX_ENTRIES` whitespace-separated dotted locations.
`target` and `name` accept dynamic strings through the configured CMeta or
QuickJS-NG expression adapter. Inline-content forms, dynamic `targettype`,
and escaped string literals remain unsupported.

### Dynamic string ownership and failure protocol

The [CCXML send contract](https://www.w3.org/TR/ccxml/#Send) defines target and name
as expressions. Reuse the existing destination-expression path rather than
introducing another evaluator or changing the Event I/O ABI. A Program owns
the decoded source; a Session owns its compiled expression handle. Admission
rejects dynamic targets or names when the datamodel lacks the complete string-expression
tail. Literal-only send programs remain compatible with legacy adapters.

Dispatch resolves delay and payload first, then evaluates name and target before
generating a send ID or calling `prepare_send`. The result must be a nonempty,
NUL-free string bounded by the selected datamodel. It is borrowed through that
prepare callback; the provider must copy any bytes needed by its ticket or
delayed queue. No new queue, thread, scheduler or mutable routing state is added.
The host retains target/type interpretation and transport authorization.

Dynamic name results use the same ASCII event-name validation as literals.
The Session copies a valid dynamic name into a temporary `tstr` before evaluating
target because an adapter may reuse its result scratch on each evaluation.
The copy is capped by `max_name_bytes` (excluding its trailing NUL), freed after
the consuming prepare callback, and freed on every intervening failure. Name
overflow returns `CCXML_LIMIT_EXCEEDED`; invalid name grammar returns
`CCXML_INVALID_CONTRACT`. Providers still copy fields retained by effect tickets.
No Session retains expression result views between actions or dispatches.

Each action has separate destination and name handles in one bounded allocation;
partial admission failure destroys all admitted handles. Both share the
adapter's live-handle capacity. Literal names retain their compile-time grammar
checks and require no result copy. The new runtime name bound only applies to
newly supported dynamic names; existing programs preserve their behavior.

Evaluation failure discards all prior transition tickets without publishing a
send or ID. Provider rejection and later-action failure preserve the existing
rollback order. Expression FULL maps to allocation failure; invalid result/contract
maps to invalid contract; execution failure maps to adapter error. Provider
refusal, including FULL, keeps the existing adapter-error mapping. Retry reevaluates
current application state. CMeta foreach uses its staged typed scope; QuickJS
foreach remains unsupported. Source storage shares `max_name_bytes` and handle
count remains bounded by the existing Program/Session and adapter limits.

## Compiler contract

`target` and `name` are required. `targettype`, `delay`, `sendid`, and
`namelist` are
optional. Type and literal delay values must be exactly one nonempty
single-quoted or double-quoted string literal. Target and name use the shared decoded
destination classifier: quoted literals retain their interior; other nonempty
values retain complete expression source. `sendid` must be a dotted NCName
location. Dynamic delay may instead be an unquoted dotted location with at least
one dot. The compiler strips expression quotes for literals and retains decoded
literal and location bytes in program-owned storage. The default `ccxml` target
type is an immutable static value. Cancel requires only `sendid`, accepts a dotted
location or quoted literal, and must be empty.

The delay literal uses the shared bounded CSS-time parser and is compiled to an
exact `uint64_t` millisecond value. The event name uses the CCXML lexical subset:
the first character is an ASCII
letter or underscore, and following characters are ASCII letters, digits,
underscore, or dot. The retained target, name, explicit target type, and
explicit delay and each decoded namelist token share the existing
`max_name_bytes` budget. Dynamic delay locations are validated as payload
locations and resolved at dispatch time as unsigned scalar values. Namelist
order, duplicates, and qualification are preserved. Non-ignorable child content
is rejected.

Unsupported standard attributes are rejected as `CCXML_UNSUPPORTED_FEATURE`;
missing or empty required values are rejected as `CCXML_INVALID_STRUCTURE`.

## Shared Event I/O boundary

The CCXML session configuration gains borrowed `event_io` and `event_io_user`
fields using the existing `scxml_event_io_adapter` table. A program containing
`<send>` or `<cancel>` requires a valid adapter. Send requires
`SCXML_EVENT_IO_CAP_SEND`; a nonzero delay additionally requires
`SCXML_EVENT_IO_CAP_DELAYED_SEND`. Cancel requires
`SCXML_EVENT_IO_CAP_CANCEL` and `prepare_cancel`; the shared adapter contract
couples cancellation to delayed-send support. `close` and `is_quiescent` remain
mandatory for every attached adapter.

A non-empty namelist additionally requires `SCXML_EVENT_IO_CAP_PAYLOAD` and
the datamodel adapter's struct-size-protected `validate_payload_location` and
`read_payload` tail. A dynamic delay also requires `read_payload` and
its validation. Empty namelists behave like an omitted payload.

The shared `scxml_send_request` carries CCXML values as follows:

- `event` is the compiled literal or evaluated CCXML `name`;
- `target` is the compiled literal or evaluated CCXML `target`;
- `type` is the compiled `targettype` or `ccxml`;
- `id` is empty unless `sendid` is present; then it is a bounded
  `send.<session-uuid>.<token>` value also staged into the datamodel location;
- `delay_ms` is the compiled literal or resolved dynamic payload value;
- `payload` is empty or an ordered shared named payload containing scalar and
  callback-scoped CMeta object views.

This shares the allocation-free prepare/commit/discard protocol, not SCXML
target semantics. The session-bound CCXML adapter interprets `targettype` and
owns CCXML, dialog, or BasicHTTP routing policy. An SCXML transport adapter is
only reusable when its host implementation intentionally supports these CCXML
semantics.

After commit, the host is responsible for injecting the corresponding
`send.successful`, `error.send.*`, `cancel.successful`, or `error.notallowed`
outcome through the serialized CCXML event dispatch boundary. This incubation
slice does not synthesize those platform events inside the core.

`prepare_send` receives callback-scoped borrowed fields. ACCEPTED transfers
one move-only effect ticket. A send with `sendid` retains both the Event I/O
ticket and a datamodel assignment ticket, ordered so the ID is visible before
the send commit. Mixed telephony, datamodel, and Event I/O effects
prepare in document order, commit in document order, and discard in reverse
order after a failure.

## Ownership and lifecycle

- The program owns explicit target, name, target-type, delay, sendid-location,
  namelist-token, and cancel-operand bytes until program destruction.
- The session copies the Event I/O operation table and borrows its user.
- Session close calls both attached adapter close callbacks exactly once.
- Session destruction remains busy until both attached adapters report
  quiescence.
- Programs without `<send>` or `<cancel>` do not require an Event I/O adapter
  and preserve their existing configuration behavior.

## Verification

The slice is complete when tests prove:

1. literal target/name, explicit/default target types, and CSS delays compile;
2. source overwrite does not affect prepared request bytes;
3. missing, empty, unsupported nonliteral fields, invalid-name, unsupported-attribute, and nested
   content forms fail deterministically;
4. dispatch prepares and commits the exact shared send request;
5. adapter rejection and malformed tickets map to the existing status model;
6. mixed send/telephony effects preserve transaction ordering and rollback;
7. send programs reject absent or incapable adapters, including a missing
   delayed-send capability when a nonzero or dynamic delay is used;
8. close is exactly once per adapter and destruction waits for Event I/O
   quiescence;
9. generated IDs are unique, committed before send publication, readable by a
   later transition, and forwarded exactly to cancel;
10. focused, full-preset, and installed-consumer checks remain green.
11. dynamic targets/names preserve decoded storage bounds, adapter admission,
    CMeta foreach scope, QuickJS scratch ownership, ID ordering and rollback;
12. invalid and oversized dynamic names fail before publication, exact-limit
    names succeed, and partial admission releases both kinds of handles.

Detailed identifier and cancellation semantics are specified in
`docs/specs/ccxml-send-cancel-design.md`. Namelist projection and lifetime
semantics are specified in `docs/specs/ccxml-send-namelist-design.md`.
