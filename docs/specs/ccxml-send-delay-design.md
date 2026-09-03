# CCXML Literal Send Delay Design

## Status and scope

This slice extends the bounded CCXML `<send>` profile with an optional
compile-time string-literal `delay` expression:

```xml
<send target="'session:callee'" name="'call.notice'" delay="'250ms'"/>
<send target="'session:callee'" name="'call.notice'" delay="'+1.5s'"/>
```

The [W3C CCXML `<send>` contract](https://www.w3.org/TR/ccxml/#Send)
defines `delay` as an ECMAScript expression whose result is a CSS2 time
designation. The current bounded profile admits only a quoted string literal
result. Arbitrary ECMAScript expressions, `sendid`, `namelist`, inline content,
and `<cancel>` remain separate slices.

## Compiler contract

The compiler decodes XML entities before checking the expression quotes and
CSS time grammar. It accepts a non-negative decimal followed immediately by
`ms` or `s`, including the standard forms `850ms`, `0.7s`, `.5s`, and
`+1.5s`. Milliseconds must be integral; seconds may contain at most three
fractional digits so the value can be represented exactly as `uint64_t`
milliseconds. Negative, empty, malformed, unsupported-unit, over-precise, and
overflowing values are rejected as `CCXML_INVALID_STRUCTURE`; nonliteral
expressions remain `CCXML_UNSUPPORTED_FEATURE`.

The decoded literal is retained in the program's existing bounded string
storage and charged to `max_name_bytes`. The action row also stores the parsed
`uint64_t delay_ms`, so dispatch performs no parsing or allocation. An omitted
delay is zero. A syntactically valid zero delay remains an immediate send and
does not add a delayed-send capability requirement.

CCXML and SCXML share `scxml_time_parse_ms`. This slice extends that
shared parser to admit the CSS forms above, keeping both front ends aligned.

## Event I/O capability boundary

All send programs continue to require `SCXML_EVENT_IO_CAP_SEND`. A program
containing at least one send whose parsed delay is nonzero additionally
requires `SCXML_EVENT_IO_CAP_DELAYED_SEND` during session initialization.
Immediate-only programs remain compatible with SEND-only adapters.

Dispatch forwards the precomputed value through `scxml_send_request.delay_ms`.
The existing Event I/O implementation owns timer scheduling, delayed-queue
purging, and completion reporting; CCXML retains the existing transactional
prepare/commit/discard and close/quiescence rules.

## Verification

Tests prove literal millisecond, second, fractional, leading-decimal,
leading-plus, entity-encoded, omitted, and zero values; deterministic rejection
of invalid values; exact runtime forwarding; and program-sensitive capability
admission. The focused CCXML test and all configured build matrices must remain
green.
