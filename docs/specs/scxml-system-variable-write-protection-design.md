# SCXML system-variable write protection design

## Context

TurboSCXML exposes `_sessionid`, `_name`, `_ioprocessors`, and `_event` from
the session-owned `scxml_expr_system_values` projection. They are immutable
system facts, but before this decision CMeta assignment admission rejected
their locations with `SCXML_INVALID_STRUCTURE`. The then-remaining mandatory
SystemVariables fixtures required a different observable boundary:

- [322](https://www.w3.org/Voice/2013/scxml-irp/322/test322.txml),
  [324](https://www.w3.org/Voice/2013/scxml-irp/324/test324.txml), and
  [326](https://www.w3.org/Voice/2013/scxml-irp/326/test326.txml) attempt a
  write and then prove the original binding remains;
- [329](https://www.w3.org/Voice/2013/scxml-irp/329/test329.txml) attempts to
  modify every system variable and proves no value changes;
- [346](https://www.w3.org/Voice/2013/scxml-irp/346/test346.txml) requires a
  distinct internal `error.execution` Event for every attempt.

## Decision

Keep ordinary invalid and unknown assignment locations as admission errors.
Recognize only the four standard system roots as read-only system locations:

```text
_sessionid
_name
_ioprocessors[.<path>]
_event[.<path>]
```

The internal location module validates the dotted NCName syntax and identifies
those roots. `scxml_assign_compile()` records a
`read_only_system_destination` flag instead of resolving a mutable CMeta field.
It still compiles the value expression so malformed syntax, source limits, and
allocation failure retain their current admission diagnostics. Because a
system object has no writable CMeta destination type, source/destination type
compatibility is intentionally not applied.

At execution, `scxml_assign_apply_with_system()` detects the flag before value
evaluation or state access and returns `SCXML_EXPR_EVALUATION_ERROR`. The
existing executable-content boundary converts that failure into the reserved
internal `error.execution` Event, aborts the rest of the current block, and
rolls staged CMeta state back. No write is attempted and no system string or
Event envelope is copied or replaced.

## State, ownership, and ordering

`scxml_session_impl.system_values` remains the only system-variable fact
source. The compiled assignment program owns only its expression program and a
Boolean classification; it does not retain session views. Runtime failure uses
the existing prioritized internal Event queue, so it remains ahead of queued
external work. Each failed assignment executes in its own content block and
therefore raises exactly one error Event; later statements in that block do
not run.

The implementation is single-owner through the existing executor callback.
It adds no queue, lock, allocation at execution, mutable mirror, fallback, or
cross-thread lifetime.

## Compatibility

Public function signatures, structure layouts, ABI versions, status enum
values, queue capacities, and successful assignment behavior do not change.
One observable compile/runtime semantic changes intentionally: a syntactically
valid CMeta document assigning to a recognized system location changes from
`SCXML_INVALID_STRUCTURE` at compilation to `SCXML_OK`, followed by
`error.execution` when that assignment executes. This is the W3C-required
runtime behavior and is not applied to unknown `_...` roots, missing
attributes, malformed paths, malformed expressions, or the null datamodel.

Consumers that used compilation failure to detect these four system targets
must instead observe the session's runtime processor error. There is no data
format, persisted state, configuration, dependency, or deployment migration.

## Verification and rollback

TDD first changes the direct assignment contract and an owning-session runtime
test. The direct test proves recognized system roots compile, apply fails, and
caller CMeta storage is unchanged. The runtime test proves four ordered
attempts each select `error.execution`, block suffixes never execute, and
system values remain bound.

Five strict W3C-derived fixtures then preserve the independent lifetime,
write-failure, and error-queue witnesses. Verification runs focused expression
and CMeta tests, all five corpus cases, manifest validation, a fresh Release
build, the complete CTest suite, and `git diff --check`.

Rollback restores admission rejection, removes the five fixtures, and returns
their manifest rows to `UNSUPPORTED`. No external data rollback is required.
