# SCXML CMeta late data binding

## Context

With `binding="late"`, the compiled `<data>` descriptor exists from program
creation, but its value is not bound into the data model until the first entry
of the state that owns the declaration. Reading or assigning that value before
entry must produce the same execution-time failure as using an unavailable
location. Immediately before the state's first `onentry`, the value becomes
bound and its initializer is evaluated. TurboSCXML already delays initializers,
but the host CMeta object exposes every schema field from session creation, so
expressions can currently observe a late value too early.

This change implements the missing binding boundary and promotes W3C test 280.
The subsequent bounded missing-substructure work promotes test 307 without
weakening static schema validation: a syntactically valid loaded path is
resolved against the runtime binding and reports `error.execution` when its
substructure is absent.

Normative sources:

- [SCXML 1.0 data binding](https://www.w3.org/TR/2015/REC-scxml-20150901/#DataBinding)
- [W3C test 280](https://www.w3.org/Voice/2013/scxml-irp/280/test280.txml)
- [W3C test 307](https://www.w3.org/Voice/2013/scxml-irp/307/test307.txml)

## Decision

The compiled program records one immutable binding descriptor for every CMeta
`<data>` declaration. A descriptor identifies the declaration's destination
byte range and, for late binding, the existing late-initializer group that owns
its lifetime transition. Expression and assignment location resolution ask the
owning session whether an accessed range is bound before dereferencing it.

The session does not maintain a second bound/unbound bitmap. Its existing
late-initializer phase remains the sole mutable fact source:

- an early declaration is always bound;
- an environment-overridden declaration is bound at session creation;
- a late declaration is unbound while its group is `NEVER`;
- it is bound while the group is `PENDING` or `DONE`;
- aborting a state-entry transaction restores `PENDING` to `NEVER`, so the
  location becomes unbound without compensating state.

Fields in the host CMeta schema that are not declared by `<data>` remain bound.
This preserves existing host-owned state and limits the new semantics to SCXML
data declarations.

## Compilation and lookup

The emitter derives destination offsets and storage sizes from the already
validated assignment program. Descriptor count is bounded by the analyzed
`<data>` row count. Each access checks range overlap using checked arithmetic.
A declared range is unbound until at least one overlapping declaration is
early, overridden, `PENDING`, or `DONE`; this preserves once-created locations
when declarations share a CMeta field. The range rule also handles aggregate
declarations and nested field expressions without assuming that every
declaration is a top-level scalar.

Expression evaluation receives a private binding callback through its existing
runtime system-values context. Direct expression APIs without a session keep
their current all-bound behavior. The same private check guards ordinary
assignment destinations, foreach sequence/item/index locations, send and invoke
`idlocation`, and borrowed structured CMeta content before any read or mutation.
Data-initializer application uses the ordinary session destination check after
the owning late group has entered `PENDING`. Donedata projection is different:
its destination is an independent completion object, so only its source
expression is checked against session binding state.

Every mutable `<data>` descriptor must yield a non-empty, in-bounds destination
range during emission. Failure rejects compilation instead of producing an
ignored zero-sized descriptor. A protected read-only system location is marked
explicitly and retains its established runtime `error.execution` behavior.

Lookup is linear in the number of compiled data declarations. The collection
is immutable and bounded, and the expected declaration count is small; adding
an index would introduce duplicate derived state without measured need.

## State transitions and errors

Before executing a late initializer group, the runtime changes its phase from
`NEVER` to `PENDING`. Initializer expressions and the declaring state's
`onentry` therefore observe the location as bound. Successful transaction
settlement changes the phase to `DONE`; rollback returns it to `NEVER`.

An unbound read or assignment returns the existing expression/assignment
evaluation error. Guards follow the current guard-error path. Executable
content raises the existing internal `error.execution` Event and aborts the
containing executable block according to current runtime rules. No fallback,
implicit default value, or extra logging is added.

## Ownership, compatibility, and rollback

Binding descriptors are owned by the immutable compiled program and borrow no
XML or host-state storage. The callback borrows the session only for the
duration of synchronous evaluation. There is no new public API, ABI, package,
CMake option, dependency, or serialized format.

Rollback consists of removing the descriptor table and binding callback.
Existing session data needs no migration because sessions are in-memory and
their CMeta layout remains host-owned.

## Verification

- Reading a late location before its declaring state is entered raises
  `error.execution` rather than exposing the host field's default bytes.
- Foreach, send/invoke `idlocation`, and structured-content borrowing enforce
  the same boundary before accessing host storage.
- Donedata writes remain independent from coincident session field offsets.
- The location is bound before its initializer and first `onentry` execute.
- An environment override makes its matching declaration bound immediately.
- A failed entry transaction restores the location to unbound.
- Undeclared host fields and early declarations retain their current behavior.
- W3C test 280 reaches terminal pass under the strict corpus runner.
- W3C test 307 reaches terminal pass through the same runtime-boundary checks:
  a pre-entry late value and a missing loaded substructure each raise one
  internal `error.execution` without weakening static schema validation.
