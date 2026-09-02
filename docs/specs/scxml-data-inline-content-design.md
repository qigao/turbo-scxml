# SCXML CMeta inline data content

## Context

SCXML permits a `<data>` declaration to obtain its initial value from exactly
one of `expr`, `src`, or inline child content. The value is assigned at the
document's early- or late-binding point. TurboSCXML already implements CMeta
`expr` initializers and their transactional error behavior, but previously
rejected every declaration without `expr`.

This change implements inline child content for the CMeta profile and promotes
W3C test 551. External `src` loading remains a separate host-integration
decision and stays unsupported.

Normative sources:

- [SCXML 1.0 `<data>`](https://www.w3.org/TR/2015/REC-scxml-20150901/#Data)
- [W3C test 551](https://www.w3.org/Voice/2013/scxml-irp/551/test551.txml)

## Decision

Inline child content is one immutable UTF-8 string initializer:

1. TurboXML parses the owning document.
2. The typed SCXML AST serializes and owns the complete children of both
   `<content>` and `<data>` elements. Text remains text; mixed or element
   content remains serialized XML with namespace declarations preserved.
3. Semantic analysis requires a nonempty `id` and exactly one supported
   initializer form. `expr` remains mutually exclusive with meaningful child
   content, while formatting whitespace and comments around an `expr` preserve
   the existing admission behavior.
4. Emission compiles the serialized bytes into an internal literal assignment
   program. The destination must be a CMeta string with a safe owned or borrowed
   buffer adapter.
5. The existing early- and late-initializer steps apply that assignment. No new
   runtime state machine, queue, or binding path is introduced.

Generating a quoted CMeta expression from XML bytes was rejected because the
expression grammar intentionally has no escape processing; quotes and
backslashes in XML markup would otherwise change or invalidate the value. A
general variant-valued initializer hierarchy was also rejected because CMeta
has no format-neutral rule for converting arbitrary XML to every scalar or
aggregate kind. The string adapter is the narrow data-model interpretation
needed by the current contract.

## State and ownership

The compiled `scxml_assign_program` owns one copy of the serialized bytes until
program destruction. A borrowed CMeta string may therefore reference those
bytes only while its session and program remain alive, which matches the
existing compiled-string-literal lifetime. An owned CMeta string receives an
adapter-managed copy.

The session's staged CMeta object remains the sole mutable fact source. Early
binding applies all document initializers before initial state entry. Late
binding applies a state's initializer block once, immediately before its first
`onentry`. Existing environment overrides still skip only matching top-level
initializer rows.

## Bounds and errors

Serialized content is bounded by the existing SCXML retained-content limit and
then by both CMeta `max_literal_bytes` and `max_string_bytes`. Size overflow,
allocation failure, an unknown location, a non-string mutable destination, or
a string descriptor without a safe adapter fails compilation with the existing
public status mapping. A protected system location follows the same boundary as
an expression initializer: compilation succeeds, then assignment fails at
runtime because system locations are read-only.

At runtime, adapter rejection follows the existing initializer transaction:
the destination is restored from its per-assignment snapshot, one internal
`error.execution` Event is queued, and later sibling initializers continue.
Late-binding once-only bookkeeping commits or rolls back with the containing
StateChart transaction. No error is logged or swallowed inside the assignment
layer.

## Compatibility and rollback

There is no public API, ABI, package, CMake, dependency, or data-format change.
Previously valid `expr` initializers retain their compiled representation and
runtime path. Typed AST storage grows only by the bounded serialized children
owned for `<data>` nodes.

Rollback consists of removing the literal assignment source and returning
`<data>` AST serialization to `<content>`-only storage; persisted session data
does not require migration because TurboSCXML sessions are in-memory and the
public CMeta layout is host-owned.

## Verification

- Early content declared under an inactive state is visible before initial
  entry.
- Late content is visible to the declaring state's first `onentry` and is not
  reapplied on re-entry.
- Mixed XML markup reaches a CMeta owned string byte-for-byte after canonical
  XML serialization.
- Existing initializer rollback, environment override, expression, and W3C
  corpus tests remain green.
- W3C test 551 reaches the terminal pass state using an exact CMeta string
  equality witness.
