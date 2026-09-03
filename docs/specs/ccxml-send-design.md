# CCXML Send Slice Design

## Status and scope

This slice adds the first bounded CCXML `<send>` profile. It accepts an empty
action with required literal-expression `target` and `name` attributes and an
optional literal-expression `targettype` attribute:

```xml
<send target="'session:callee'" name="'call.notice'"/>
<send target="'https://example.test/events'"
      targettype="'basichttp'" name="'call.notice'"/>
```

When `targettype` is omitted, the compiled value is `ccxml`. The standard
`delay`, `sendid`, `namelist`, and inline-content forms remain unsupported in
this incubation profile, as do arbitrary ECMAScript expressions and escaped
string literals.

## Compiler contract

`target` and `name` are required. `targettype` is optional. These are the only
admitted unqualified attributes, and every supplied value must be exactly one
nonempty single-quoted or double-quoted string literal. The compiler strips
the expression quotes and retains the decoded bytes in program-owned storage.
The default `ccxml` target type is an immutable static value.

The event name uses the CCXML lexical subset: the first character is an ASCII
letter or underscore, and following characters are ASCII letters, digits,
underscore, or dot. The retained target, name, and explicit target type share
the existing `max_name_bytes` budget. Non-ignorable child content is rejected.

Unsupported standard attributes are rejected as `CCXML_UNSUPPORTED_FEATURE`;
missing or empty required values are rejected as `CCXML_INVALID_STRUCTURE`.

## Shared Event I/O boundary

The CCXML session configuration gains borrowed `event_io` and `event_io_user`
fields using the existing `scxml_event_io_adapter` table. A program containing
`<send>` requires a valid adapter that advertises `SCXML_EVENT_IO_CAP_SEND` and
provides `prepare_send`, `close`, and `is_quiescent`.

The shared `scxml_send_request` carries CCXML values as follows:

- `event` is the compiled CCXML `name`;
- `target` is the compiled CCXML `target`;
- `type` is the compiled `targettype` or `ccxml`;
- `id` is empty, `delay_ms` is zero, and the payload is empty.

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

- The program owns explicit target, name, and target-type bytes until program
  destruction.
- The session copies the Event I/O operation table and borrows its user.
- Session close calls both attached adapter close callbacks exactly once.
- Session destruction remains busy until both attached adapters report
  quiescence.
- Programs without `<send>` do not require an Event I/O adapter and preserve
  their existing configuration behavior.

## Verification

The slice is complete when tests prove:

1. literal target/name and explicit/default target types compile;
2. source overwrite does not affect prepared request bytes;
3. missing, empty, nonliteral, invalid-name, unsupported-attribute, and nested
   content forms fail deterministically;
4. dispatch prepares and commits the exact shared send request;
5. adapter rejection and malformed tickets map to the existing status model;
6. mixed send/telephony effects preserve transaction ordering and rollback;
7. send programs reject absent or incapable adapters;
8. close is exactly once per adapter and destruction waits for Event I/O
   quiescence;
9. focused, full-preset, and installed-consumer checks remain green.
