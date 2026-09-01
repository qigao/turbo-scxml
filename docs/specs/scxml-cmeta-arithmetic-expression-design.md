# CMeta Arithmetic Expression Design

## Background

TurboSCXML's CMeta datamodel already compiles finite scalar expressions to
QueryVM. The current grammar supports scalar literals, reflected locations,
SCXML system values, `In()`, comparisons, and short-circuit Boolean operators.
It does not admit arithmetic even though QueryVM exposes arithmetic opcodes.

Arithmetic belongs to the SCXML datamodel adapter rather than CMeta. CMeta
remains the source of reflected scalar types and callable contracts; it does
not become a runtime expression language.

## Decision

Extend `scxml_expr` with these operators and precedence levels:

1. primary expressions
2. unary `+`, unary `-`, and logical `!`
3. multiplicative `*`, `/`, `%`
4. additive `+`, `-`
5. comparisons
6. `&&`
7. `||`

The parser emits existing QueryVM arithmetic opcodes. The SCXML expression
backend implements the checked numeric operations.

## Numeric Contract

- `sint op sint` produces `sint`.
- `uint op uint` produces `uint`.
- If either operand is `float`, the other numeric operand is converted to
  `double` and the result is `float`.
- Mixed `sint`/`uint` arithmetic is rejected at compile time because a stable
  result kind cannot represent every exact result without implicit precision
  loss. Callers may use a floating operand when conversion is intentional.
- `%` accepts only same-kind integral operands.
- Unary `+` accepts any numeric operand and preserves its kind.
- Unary `-` accepts `sint` and `float`. It rejects `uint`.
- Signed overflow, unsigned overflow or underflow, integer division overflow,
  and division or remainder by zero return `SCXML_EXPR_EVALUATION_ERROR`.
- A floating result must be finite. NaN or infinity produced by arithmetic is
  an evaluation error. Existing comparisons of externally supplied NaN values
  retain their current behavior.

These rules are deliberately stricter than C's usual arithmetic conversions.
They avoid platform-dependent wraparound and silent loss of integer precision.

## State, Ownership, and Errors

Compiled instructions, operands, and retained literals remain owned by the
existing `scxml_expr_program`. Evaluation uses only call-scoped QueryVM
registers and borrowed CMeta data views. Arithmetic introduces no persistent
state, allocation, fallback path, or external side effect.

Syntax and incompatible operand kinds fail during compilation. Data-dependent
numeric faults fail during evaluation, leave the caller's output unchanged,
and propagate through the existing SCXML executable boundary as
`error.execution` where that boundary already performs the conversion.

## Compatibility and Impact

The change is source and ABI compatible: no public structure or function
signature changes. Previously valid expressions retain their precedence and
result types. A leading negative literal is reinterpreted as unary `-` applied
to a positive literal, while the exact `INT64_MIN` spelling remains admitted.

All existing value-expression consumers gain arithmetic: assignments, data
initializers, conditional executable content, send/cancel expressions,
payload parameters, and done data. Transition conditions still require a
Boolean final result.

## Alternatives

- Adding an expression engine to CMeta was rejected because it would expand a
  finite metadata/runtime-protocol layer into a language implementation.
- QuickJS remains the future full ECMAScript datamodel. It is not required for
  deterministic typed arithmetic in the CMeta datamodel.
- Converting every number to `double` was rejected because `uint64_t` and
  `int64_t` values cannot always be represented exactly.

## Verification

Tests cover precedence, parentheses, unary operators, all three numeric result
kinds, type rejection, exact `INT64_MIN`, division/remainder by zero, signed
and unsigned overflow/underflow, floating non-finite results, condition use,
and unchanged output on evaluation failure. The complete TurboSCXML CTest
suite verifies adjacent expression consumers.
