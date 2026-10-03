# QuickJS execution-quota contract

## Decision

TurboSCXML does **not** expose a deterministic QuickJS bytecode-instruction
quota.

The pinned QuickJS embedding ABI provides memory, stack, interrupt and stack-top
hooks, but no stable public instruction counter. TurboSCXML therefore does not
reinterpret elapsed time or interrupt-handler invocations as an "instruction"
count.

`scxml_quickjs_compile_options_v1.max_instructions` keeps its existing meaning:
it bounds TurboSCXML's own expression/compiler IR used while compiling SCXML.
It is not consumed by the QuickJS runtime and must not be described as a
JavaScript execution-instruction budget.

VoiceXML QuickJS options intentionally have no `max_instructions` field.

## Runtime hard bounds

The shared private QuickJS sandbox enforces:

- source-size bound;
- result/string-size bound;
- heap bound through `JS_SetMemoryLimit`;
- stack bound through `JS_SetMaxStackSize`;
- non-blocking runtime mode;
- monotonic evaluation deadline through `JS_SetInterruptHandler`;
- profile-specific conversion/property/array/snapshot bounds.

The wall-clock deadline is an independent safety bound. It is not an
instruction counter and is never presented as one.

## Why no interrupt-poll quota

QuickJS calls the interrupt handler at engine-defined polling points. Counting
those callbacks would create an engine-poll budget, not a language instruction
budget. Its granularity may change with the pinned engine implementation and
must not be smuggled behind the existing `max_instructions` name.

A future poll/tick budget would require a new, explicitly named append-only ABI
field and tests written to that exact engine-poll semantic. No such field is
needed for the current VoiceXML/SCXML safety contract.

## Qualification

Regression tests must prove both sides of the contract:

1. setting `max_instructions` to a very small positive value does not impose a
   QuickJS runtime loop quota;
2. an infinite QuickJS loop is still interrupted by the independent positive
   monotonic evaluation deadline;
3. feature-OFF packages remain qjs-free;
4. heap/stack/deadline and transactional rollback tests remain green.

This keeps the runtime contract honest without weakening existing safety
bounds.
