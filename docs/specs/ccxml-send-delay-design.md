# CCXML Send Delay Design

## Status and scope

This slice extends the bounded CCXML `<send>` profile with an optional `delay`
that may be:

- a compile-time string literal, or
- an unquoted dotted-location read at dispatch-time.

```xml
<send target="'session:callee'" name="'call.notice'" delay="'250ms'"/>
<send target="'session:callee'" name="'call.notice'" delay="'+1.5s'"/>
<send target="'session:callee'" name="'call.notice'" delay="payload.count"/>
```

The [W3C CCXML `<send>` contract](https://www.w3.org/TR/ccxml/#Send)
defines `delay` as an ECMAScript expression whose result is a CSS2 time
designation. The current bounded profile admits a quoted string literal result or a
simple dotted NCName location. Arbitrary ECMAScript delay expressions and inline
content remain deferred. Subsequent send-identifier/cancel and bounded namelist
slices are documented in `docs/specs/ccxml-send-cancel-design.md` and
`docs/specs/ccxml-send-namelist-design.md`.

## Compiler contract

The compiler decodes XML entities before checking the expression quotes and
CSS time grammar. It accepts a non-negative decimal followed immediately by
`ms` or `s`, including the standard forms `850ms`, `0.7s`, `.5s`, and
`+1.5s`. Milliseconds must be integral; seconds may contain at most three
fractional digits so the value can be represented exactly as `uint64_t`
milliseconds. A delay that is not a quoted literal must be a dotted-location with
at least one dot and pass `dotted_location_valid` checks. Negative, empty,
malformed, unsupported-unit, over-precise, and overflowing literal values are
rejected as `CCXML_INVALID_STRUCTURE`; nonliteral expressions remain
`CCXML_UNSUPPORTED_FEATURE`.

The decoded literal is retained in the program's existing bounded string storage
and charged to `max_name_bytes`. The action row also stores the parsed
`uint64_t delay_ms` for literal delays. Dynamic delay locations are compiled to
program-owned storage and read from the datamodel at dispatch time; they must be
scalar and convertible to non-negative integer milliseconds.

An omitted delay is zero. A syntactically valid literal zero remains an immediate
send and does not add a delayed-send capability requirement.

CCXML and SCXML share `scxml_time_parse_ms`. This slice extends that
shared parser to admit the CSS forms above, keeping both front ends aligned.

## Event I/O capability boundary

All send programs continue to require `SCXML_EVENT_IO_CAP_SEND`. A program
containing at least one send whose parsed delay is nonzero, or with any dynamic
delay location, additionally requires `SCXML_EVENT_IO_CAP_DELAYED_SEND` during
session initialization.
Immediate-only programs remain compatible with SEND-only adapters.

Dispatch forwards the precomputed value through `scxml_send_request.delay_ms`.
The existing Event I/O implementation owns timer scheduling, delayed-queue
purging, and completion reporting; CCXML retains the existing transactional
prepare/commit/discard and close/quiescence rules.

## Verification

Tests prove literal millisecond, second, fractional, leading-decimal,
leading-plus, entity-encoded, omitted, and zero values; deterministic rejection
of invalid values; exact runtime forwarding for both literal and dynamic delays;
runtime rejection for non-numeric dynamic delay payloads; and program-sensitive
capability admission. The focused CCXML test and all configured build matrices
must remain green.
