# CCXML Send Slice Design

## Status and scope

This slice adds the first bounded CCXML `<send>` profile. It accepts an empty
action with required literal-expression `target` and `name` attributes and
optional literal-expression `targettype` and `delay` attributes:

```xml
<send target="'session:callee'" name="'call.notice'"/>
<send target="'https://example.test/events'"
      targettype="'basichttp'" name="'call.notice'" delay="'250ms'"/>
```

When `targettype` is omitted, the compiled value is `ccxml`; omitted `delay`
is zero. The standard `sendid`, `namelist`, inline-content forms, dynamic
expressions, and escaped string literals remain unsupported in this incubation
profile.

## Compiler contract

`target` and `name` are required. `targettype` and `delay` are optional. These
are the only admitted unqualified attributes, and every supplied value must be
exactly one nonempty single-quoted or double-quoted string literal. The compiler
strips the expression quotes and retains the decoded bytes in program-owned
storage. The default `ccxml` target type is an immutable static value.

The delay literal uses the shared bounded CSS-time parser and is compiled to an
exact `uint64_t` millisecond value. The event name uses the CCXML lexical subset:
the first character is an ASCII
letter or underscore, and following characters are ASCII letters, digits,
underscore, or dot. The retained target, name, explicit target type, and
explicit delay share the existing `max_name_bytes` budget. Non-ignorable child
content is rejected.

Unsupported standard attributes are rejected as `CCXML_UNSUPPORTED_FEATURE`;
missing or empty required values are rejected as `CCXML_INVALID_STRUCTURE`.

## Shared Event I/O boundary

The CCXML session configuration gains borrowed `event_io` and `event_io_user`
fields using the existing `scxml_event_io_adapter` table. A program containing
`<send>` requires a valid adapter that advertises `SCXML_EVENT_IO_CAP_SEND` and
provides `prepare_send`, `close`, and `is_quiescent`. A program containing a
nonzero delay additionally requires `SCXML_EVENT_IO_CAP_DELAYED_SEND`.

The shared `scxml_send_request` carries CCXML values as follows:

- `event` is the compiled CCXML `name`;
- `target` is the compiled CCXML `target`;
- `type` is the compiled `targettype` or `ccxml`;
- `id` is empty, `delay_ms` is the compiled literal or zero, and the payload is
  empty.

This shares the allocation-free prepare/commit/discard protocol, not SCXML
target semantics. The session-bound CCXML adapter interprets `targettype` and
owns CCXML, dialog, or BasicHTTP routing policy. An SCXML transport adapter is
only reusable when its host implementation intentionally supports these CCXML
semantics.

After commit, the host is responsible for injecting the corresponding
`send.successful` or `error.send.*` outcome through the serialized CCXML event
dispatch boundary. This incubation slice does not synthesize those platform
events inside the core.

`prepare_send` receives callback-scoped borrowed fields. ACCEPTED transfers
one move-only effect ticket. Mixed telephony, datamodel, and send effects
prepare in document order, commit in document order, and discard in reverse
order after a failure.

## Ownership and lifecycle

- The program owns explicit target, name, target-type, and delay bytes until
  program destruction.
- The session copies the Event I/O operation table and borrows its user.
- Session close calls both attached adapter close callbacks exactly once.
- Session destruction remains busy until both attached adapters report
  quiescence.
- Programs without `<send>` do not require an Event I/O adapter and preserve
  their existing configuration behavior.

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
9. focused, full-preset, and installed-consumer checks remain green.
