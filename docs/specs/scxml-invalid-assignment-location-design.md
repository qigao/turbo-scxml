# SCXML invalid assignment location design

## Context

SCXML 1.0 requires an executable `<assign>` whose location expression does
not denote a valid data-model location to place `error.execution` in the
internal Event queue. The same runtime error rule applies when evaluation of a
location expression does not yield a valid location. W3C IRP tests
[286](https://www.w3.org/Voice/2013/scxml-irp/286/test286.txml) and
[311](https://www.w3.org/Voice/2013/scxml-irp/311/test311.txml) exercise that
boundary.

TurboSCXML currently resolves every CMeta assignment destination while the
document is compiled. A syntactically valid unknown field therefore rejects
the whole document with `SCXML_INVALID_STRUCTURE`, before the owning
executable block can run. Valid destinations, protected system locations, and
runtime value-conversion failures already use the compiled assignment program
and transactional executable-content path.

## Decision

Add an internal assignment-location policy with two values:

- `SCXML_ASSIGN_LOCATION_STRICT` retains the current load-time fail-fast
  contract.
- `SCXML_ASSIGN_LOCATION_RUNTIME` admits a lexically valid location that the
  static CMeta schema cannot resolve and records an invalid-destination marker
  in the compiled assignment program.

Only XML executable `<assign>` emission selects the runtime policy. Data
initializers and `<donedata><param>` materialization select the strict policy,
because their statically declared destinations are internal schema
invariants, not executable location expressions.

The compiler still rejects missing attributes, malformed dotted-NCName syntax,
source/path limits, invalid CMeta roots or child descriptors, inconsistent
field offsets/storage bounds, unsupported destination adapters,
malformed value expressions, allocation failure, and known source/destination
type mismatch. For a deferred invalid destination it compiles the value
expression to preserve those admission checks, but skips destination type
compatibility because no destination type exists.

`scxml_location_compile()` distinguishes document-level absence from schema
corruption: a missing field, scalar traversal, or valid descriptor without
addressable storage remains `SCXML_EXPR_UNKNOWN_LOCATION`; an invalid child
descriptor or impossible offset/storage range is
`SCXML_EXPR_INVALID_ARGUMENT`. Runtime admission applies only to the former.

At execution, the assignment detects the invalid-destination marker before
evaluating the right-hand expression or accessing staged state and returns
`SCXML_EXPR_UNKNOWN_LOCATION`. The existing `SCXML_STEP_ASSIGN` boundary
converts any non-OK assignment result to one internal `error.execution`, aborts
the rest of that executable-content block, and discards its staged mutations.

## Alternatives

### Keep load-time rejection

This preserves the present implementation but cannot execute the mandatory
286/311 assertions. It also conflates an invalid executable location with an
invalid SCXML document even though the specification assigns the former a
runtime processor error.

### Defer all assignment destinations

Resolving every destination at execution would retain source strings and add
repeated schema traversal to the hot path. More importantly, it would weaken
fail-fast validation for data initializers and internal completion-data
materialization. That broader semantic and ownership change is unnecessary.

### Explicit policy at the assignment boundary

The selected policy makes the semantic choice visible at each internal call
site. It reuses one compiler and one runtime program without making XML element
kind implicit inside the expression module.

## State, ownership, and ordering

The caller-owned typed CMeta object remains the only data-model fact source.
The assignment program owns its compiled right-hand expression and a finite
destination classification; it retains neither the source location bytes nor
a second state map. A deferred invalid destination cannot mutate state because
execution rejects it before value evaluation and destination access.

The session executor remains the single owner of state transitions. The
existing internal Event queue owns `error.execution`, so processor-error
priority and FIFO behavior do not change. No new lock, queue, allocation,
fallback, or cross-thread lifetime is introduced at execution.

## Errors and compatibility

No installed header, public C signature, ABI version, persisted data,
configuration, dependency, or deployment contract changes. The internal
`scxml_assign_compile()` signature gains an explicit policy argument.

One user-visible behavior changes intentionally: a CMeta document containing a
syntactically valid unknown executable `<assign>` destination changes from
load-time `SCXML_INVALID_STRUCTURE` to successful compilation, followed by one
runtime `error.execution` if that statement executes. Invalid syntax and
strict internal destinations remain load-time failures. Successful assignment,
protected-system-variable, and value-conversion behavior remain unchanged.

## Performance and resource bounds

Successful assignments add one predictable destination-classification branch.
Invalid assignments add no execution-time allocation or location traversal;
they fail before expression evaluation. Compiled-program storage grows only by
an enum-sized classification already represented by existing destination
state.

## Verification and rollback

Direct expression tests prove strict rejection, runtime-policy admission,
runtime `SCXML_EXPR_UNKNOWN_LOCATION`, byte-exact state preservation, and
fail-fast rejection of a corrupt nested schema.
W3C-derived fixtures prove internal error delivery and executable-block
abortion for both an unknown root and a path traversing a scalar. Existing
CMeta compilation tests continue to reject missing attributes, malformed
paths, unknown internal destinations, and null-datamodel assignments.

Verification runs focused expression/CMeta/W3C tests, manifest validation,
fresh Debug and Release builds, the complete CTest suite, CodeGraph affected
analysis, and `git diff --check`.

Rollback removes the policy and invalid-destination marker, restores load-time
rejection, removes the two fixtures, and returns their manifest rows to
`UNSUPPORTED`. No external data rollback is required.
