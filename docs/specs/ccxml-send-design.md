# CCXML Send Slice Design

## Status and scope

This slice adds the first bounded CCXML `<send>` profile. It accepts an empty
action with required literal-expression `target` and `name` attributes,
optional literal-expression `targettype` and `delay` attributes, and an
optional dotted-location `sendid` and bounded dotted-location `namelist`:

```xml
<send target="'session:callee'" name="'call.notice'"/>
<send target="'https://example.test/events'"
      targettype="'basichttp'" name="'call.notice'" delay="'250ms'"/>
<send target="'session:callee'" name="'call.notice'"
      delay="'250ms'" sendid="request.pending"/>
<send target="'session:callee'" name="'call.notice'"
      namelist="conference.id count"/>
<cancel sendid="request.pending"/>
```

When `targettype` is omitted, the compiled value is `ccxml`; omitted `delay`
is zero. `sendid` is a dotted writable string location populated with a
generated identifier. `<cancel>` accepts a dotted readable string location or
a quoted literal identifier. `namelist` accepts at most
`SCXML_PAYLOAD_MAX_ENTRIES` whitespace-separated dotted locations.
Inline-content forms, arbitrary dynamic expressions, and escaped string
literals remain unsupported.

## Compiler contract

`target` and `name` are required. `targettype`, `delay`, `sendid`, and
`namelist` are
optional. Target, name, type, and delay values must be exactly one nonempty
single-quoted or double-quoted string literal. `sendid` must be a dotted NCName
location. The compiler strips expression quotes and retains decoded literal
bytes in program-owned storage. The default `ccxml` target type is an immutable
static value. Cancel requires only `sendid`, accepts a dotted location or
quoted literal, and must be empty.

The delay literal uses the shared bounded CSS-time parser and is compiled to an
exact `uint64_t` millisecond value. The event name uses the CCXML lexical subset:
the first character is an ASCII
letter or underscore, and following characters are ASCII letters, digits,
underscore, or dot. The retained target, name, explicit target type, and
explicit delay and each decoded namelist token share the existing
`max_name_bytes` budget. Namelist order, duplicates, and qualification are
preserved. Non-ignorable child content is rejected.

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
`read_payload` tail. Empty namelists behave like an omitted payload.

The shared `scxml_send_request` carries CCXML values as follows:

- `event` is the compiled CCXML `name`;
- `target` is the compiled CCXML `target`;
- `type` is the compiled `targettype` or `ccxml`;
- `id` is empty unless `sendid` is present; then it is a bounded
  `send.<session-uuid>.<token>` value also staged into the datamodel location;
- `delay_ms` is the compiled literal or zero;
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
3. missing, empty, nonliteral, invalid-name, unsupported-attribute, and nested
   content forms fail deterministically;
4. dispatch prepares and commits the exact shared send request;
5. adapter rejection and malformed tickets map to the existing status model;
6. mixed send/telephony effects preserve transaction ordering and rollback;
7. send programs reject absent or incapable adapters, including a missing
   delayed-send capability when a nonzero delay is compiled;
8. close is exactly once per adapter and destruction waits for Event I/O
   quiescence;
9. generated IDs are unique, committed before send publication, readable by a
   later transition, and forwarded exactly to cancel;
10. focused, full-preset, and installed-consumer checks remain green.

Detailed identifier and cancellation semantics are specified in
`docs/specs/ccxml-send-cancel-design.md`. Namelist projection and lifetime
semantics are specified in `docs/specs/ccxml-send-namelist-design.md`.
