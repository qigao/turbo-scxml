# CMeta Custom Executable Action Design

## Scope

The CMeta compiler accepts empty foreign-namespace executable elements when a
matching action is explicitly registered. A registration is keyed by exact
namespace URI and local name and binds one immutable `cmeta_callable` plus an
ordered list of unqualified XML attributes containing its argument expressions.

Custom elements remain illegal outside executable-content positions. Unknown
foreign elements, nested content, namespaced argument attributes, missing or
extra arguments, duplicate registrations, and unsupported callable signatures
fail compilation.

## Public contract

`scxml_compile_cmeta_v2()` receives a versioned provider containing the V1
expression settings and a bounded action registration array. Each row contains:

- exact namespace URI and local name views;
- a `cmeta_callable` copied and bound into the compiled program;
- `parameter_names`, in callable-signature order.

The initial profile supports CMeta value callables whose parameters and return
type are built-in scalar descriptors representable by the expression VM.
Callable return values are intentionally discarded. Failure of
`cmeta_callable_invoke()` raises `error.execution`; a successful call continues
the executable block.

## Ownership, lifetime, and execution

- Provider rows and strings are borrowed only during compilation.
- The bound callable, including inline capture bytes, is copied into owned
  program IR and remains immutable until program destruction.
- Argument programs are owned by the program and evaluated against the staged
  state on the session SerialExecutor.
- Argument and return scratch is stack-local, aligned, bounded, and never
  retained by TurboSCXML. A callable that retains an argument pointer violates
  the registration contract.
- No global registry or mutable shared lookup table is introduced.

The callable's CMeta effects/properties remain descriptive metadata. SCXML
marks the containing native executable as stateful and fallible because an
adapter callable may perform host work or reject invocation.

## Validation

Compilation rejects:

- invalid V2 ABI/shape or count/pointer combinations;
- empty or duplicate action keys;
- invalid/unbound callables or generator protocols;
- unsupported parameter/return descriptors;
- duplicate/empty parameter names or parameter count mismatch;
- unregistered foreign executable elements;
- child nodes and attributes not declared by the registration.

## Verification

Tests cover a successful scalar call, staged expression evaluation, use inside
`if`/`foreach`/`finalize`, unregistered elements, malformed arguments and child
content, and duplicate registrations. The complete preset regression preserves
the existing executable-content and lifecycle behavior.
