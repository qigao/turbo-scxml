# CCXML Send Delay Design

## Status and scope

This slice extends the bounded CCXML `<send>` profile with an optional `delay`
that may be:

- a compile-time string literal,
- an unquoted dotted-location numeric read at dispatch-time, or
- a string expression compiled by the selected datamodel adapter.

```xml
<send target="'session:callee'" name="'call.notice'" delay="'250ms'"/>
<send target="'session:callee'" name="'call.notice'" delay="'+1.5s'"/>
<send target="'session:callee'" name="'call.notice'" delay="payload.count"/>
<send target="'session:callee'" name="'call.notice'" delay="(count * 10 + 'ms')"/>
<send target="'session:callee'" name="'call.notice'" delay="(settings.delay)"/>
```

The [W3C CCXML `<send>` contract](https://www.w3.org/TR/ccxml/#Send)
defines `delay` as an ECMAScript expression whose result is a CSS2 time
designation. The string-expression path reuses the existing CMeta/QuickJS-NG
adapter callbacks and `scxml_time_parse_ms`, without adding an evaluator, timer,
or public ABI. Inline content remains deferred. Subsequent send-identifier/cancel and bounded namelist
slices are documented in `docs/specs/ccxml-send-cancel-design.md` and
`docs/specs/ccxml-send-namelist-design.md`.

## Compiler contract

The compiler decodes XML entities before checking the expression quotes and
CSS time grammar. It accepts a non-negative decimal followed immediately by
`ms` or `s`, including the standard forms `850ms`, `0.7s`, `.5s`, and
`+1.5s`. Milliseconds must be integral; seconds may contain at most three
fractional digits so the value can be represented exactly as `uint64_t`
milliseconds. A nonliteral dotted location with at least one dot retains its
existing numeric payload-read semantics. Other nonempty sources use the string
expression adapter; parentheses explicitly select this path for a dotted string
location such as `(settings.delay)`. This is deterministic syntax classification,
not a fallback on evaluation failure. Negative, empty,
malformed, unsupported-unit, over-precise, and overflowing literal values are
rejected as `CCXML_INVALID_STRUCTURE`. Unsupported literal escapes remain rejected;
expression syntax is checked during Session admission by the selected adapter.

The decoded literal is retained in the program's existing bounded string storage
and charged to `max_name_bytes`. The action row also stores the parsed
`uint64_t delay_ms` for literal delays. Dynamic delay locations are compiled to
program-owned storage and read from the datamodel at dispatch time; they must be
scalar and convertible to non-negative integer milliseconds.

String-expression sources are also Program-owned and charged to `max_name_bytes`.
The Session owns a separate compiled delay handle, sharing the adapter's handle
budget with target/name/type and conditions. Partial admission failure releases
all handles. Dispatch evaluates delay before payload/name/type/target, parses the
borrowed result immediately into `uint64_t`, and retains no string view across
another callback. The Session owner continues to serialize dispatch; no new
cross-thread state, result copy, queue, or shutdown obligation is introduced.
Results must be nonempty NUL-free strings within the adapter's existing result
bound and the same CSS time grammar as literals. Invalid types, negative,
over-precise, or overflowing results return `CCXML_INVALID_CONTRACT`; VM exceptions
and quota failures preserve `CCXML_ADAPTER_ERROR`. Any failure rolls back prior
transition tickets before publishing a send or ID. The provider still owns all
timer scheduling, capacity rejection, and delayed delivery.

The legacy numeric FLOAT path accepts only finite non-negative integral values
strictly below `2^64`. Comparing against `(double)UINT64_MAX` with `>` is incorrect
because that conversion rounds up to `2^64`, permitting an out-of-range cast.
The UINT path continues to accept `UINT64_MAX` exactly.

Existing numeric locations keep their payload-read admission requirement; new
string expressions require the existing string-expression tail instead. No numeric
coercion is added to the string path. Applications can revert new expressions to
literal durations; old programs require no migration.

An omitted delay is zero. A syntactically valid literal zero remains an immediate
send and does not add a delayed-send capability requirement.

CCXML and SCXML share `scxml_time_parse_ms`. This slice extends that
shared parser to admit the CSS forms above, keeping both front ends aligned.

## Event I/O capability boundary

All send programs continue to require `SCXML_EVENT_IO_CAP_SEND`. A program
containing at least one send whose parsed delay is nonzero, or with any dynamic
delay location or string expression, additionally requires `SCXML_EVENT_IO_CAP_DELAYED_SEND` during
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
capability admission. Expression coverage includes current-state reevaluation,
CSS fractions and overflow, non-string results, VM exceptions, handle cleanup,
provider rollback, and preservation of the legacy numeric path. The focused CCXML test and all configured build matrices
must remain green.
