# TurboSCXML Typed AST and Statechart Host Integration Design

## Context

TurboSCXML currently parses an owning TurboXML DOM and then repeatedly maps
element names, attributes, children, and source locations while analysis and
emission traverse that generic tree. The runtime IR is sound, but the compiler
boundary mixes XML mechanics with SCXML semantics and makes structural changes
expensive. Runtime invocation preprocessing previously composed older
Statechart hooks; the current runtime uses the stable V4 host transaction only.

The local uSCXML implementation at `C:/projects/cpp/uscxml/scxml` is used as an
algorithm and conformance reference, especially its document setup,
microstep/event ordering, and finalize-before-autoforward behavior. Its retained
DOM pointers and runtime tag dispatch are intentionally not copied.

## Compiler Pipeline

The compiler becomes:

```text
XML bytes
  -> TurboXML owning DOM (syntax only)
  -> immutable typed scxml_ast (SCXML vocabulary and source locations)
  -> semantic analysis and immutable program IR
  -> cflow_statechart plus scxml_program sidecars
```

`scxml_ast` is a logical tree stored as bounded contiguous node and attribute
arrays. Nodes use stable integer IDs, parent/first-child/next-sibling links, a
typed `scxml_element_kind`, and a source location. Attribute values and text
are copied into AST-owned storage with checked arithmetic. Known attributes are
identified once during AST construction; namespace and vocabulary errors are
reported there. The generic DOM is destroyed immediately after AST construction
and is never retained by semantic analysis, emission, program IR, or runtime.

The first migration preserves the emitted Statechart/program IR byte-for-byte
in meaning. Analysis and emission move by behavior family rather than by file
size: root/state topology, transitions, executable content, and invocation.

## Runtime Integration

TurboSCXML uses CFlow host ABI V4 only. `PREPARE_TRIGGER` performs, in order:

1. bind the selected Event system values;
2. resolve a nonzero invocation origin token against the session registry;
3. execute only that invocation's `<finalize>` block against the lazy staged
   CMeta state;
4. mark invocation completion state when applicable;
5. prepare autoforward tickets;
6. return `CONTINUE` so transition guards observe committed finalize changes.

`PREPARE_QUIESCENCE` reconciles live invocations and stages start/cancel effects.
The session invocation registry remains the sole token-to-descriptor fact
source. The Statechart owns managed state/configuration/queues; `scxml_program`
owns immutable AST-derived semantic assets; adapters own external resources.

Finalize reuses the normal executable interpreter over the host context. The
bounded CMeta profile admits the same supported executable subset in finalize
as in ordinary executable blocks: `<if>`/`<elseif>`/`<else>`, `<foreach>`,
`<raise>`, `<send>`, `<cancel>`, `<assign>`, and `<log>`. A finalize error
returns `FATAL`; partial state, Events, and tickets roll back as one host
transaction.

## Compatibility and Migration

Public TurboSCXML APIs and accepted XML remain unchanged. Diagnostics retain
their status class and source location; exact wording changes only where a
typed-AST boundary can provide a more precise error. Program/session ownership,
queue capacities, adapters, and serialized formats do not change.

The migration is incremental and reversible: introduce and test AST ownership,
switch compiler stages one family at a time, then remove DOM parameters only
after no semantic/emission caller remains. Runtime migration occurs after the
CFlow V4 package is available. Reverting the compiler migration restores the
previous compiler without data migration; the removed hook compatibility layer
is intentionally not restored.

## Alternatives and Tradeoffs

Retaining the DOM with thin wrappers was rejected because semantic stages would
still depend on XML handles and repeated string dispatch. Building a pointer
object graph was rejected because it adds allocation and pointer-chasing cost.
Direct XML-to-final-IR lowering was rejected because it conflates syntax,
validation, and lowering and makes multi-pass checks harder.

The typed AST adds one bounded copy of names/text during compilation. This is a
deliberate compile-time ownership cost that removes DOM lifetime coupling and
repeated vocabulary dispatch. Runtime memory and hot-path behavior do not grow.

## Verification

Private AST tests cover namespace recognition, typed kinds/attributes, sibling
order, source locations, limits, checked overflow, and independent lifetime
after DOM destruction. Existing compile/IR and W3C tests prove semantic
equivalence. W3C 233/234 prove finalize assignment commits before selection and
matching-invocation isolation. Full Release CTest plus ASan-focused compiler
tests cover ownership and cleanup.
