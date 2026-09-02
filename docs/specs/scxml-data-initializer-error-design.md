# CMeta Data Initializer Error Design

## Context

W3C SCXML 1.0 requires every `<data>` element to exist at document
initialization. If its supplied value is illegal, the processor places
`error.execution` on the internal queue and leaves an empty data element that
can be assigned later. Early binding initializes every declaration before the
initial state is entered; late binding assigns each declaration before its
owning state's first `onentry`.

TurboSCXML currently compiles CMeta data initializers into assignments. Early
assignments run in `scxml_session_init_cmeta*()` before the native CFlow
StateChart exists, so an evaluation error returns
`CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION` and no SCXML Event can be
raised. Late assignments already run in the StateChart transaction, but an
evaluation error is treated as a fatal instance error. Both behaviors prevent
the recoverable W3C test 277 flow.

This decision affects analyzer capacity accounting, program emission, CMeta
session initialization, executable runtime behavior, and conformance tests.
It does not change a public API, the CFlow ABI, XML syntax, or dependency
versions.

Normative references:

- [SCXML 1.0 `<data>`](https://www.w3.org/TR/2015/REC-scxml-20150901/#Data)
- [SCXML interpretation algorithm](https://www.w3.org/TR/2015/REC-scxml-20150901/#AlgorithmforSCXMLInterpretation)
- [W3C test 277](https://www.w3.org/Voice/2013/scxml-irp/277/test277.txml)

## Decision

Evaluate early CMeta data assignments through one private executable attached
as the first entry action of the immutable native root state. The root is
entered exactly once during initial stabilization, before descendant
`onentry` actions. The executable iterates the already compiled document-wide
initializer span in document order.

Each assignment has one of three outcomes:

1. an environment override skips the document initializer;
2. a successful initializer updates the staged CFlow state;
3. a failed initializer retains the field's pre-initialization value, stages
   one `error.execution`, and allows later initializers and state entry to
   continue.

Late initializer blocks use the same per-assignment recovery rule during the
owning state's first entry. Their existing effect ticket still makes the
first-entry marker transactional; after successfully staging any required
error Events, the marker and successful sibling assignments commit together.

## State and ownership protocol

- Data unit: one compiled `scxml_assign_program` in the admitted initializer
  span.
- Mutable fact source: the CFlow StateChart instance's staged CMeta state.
- Program ownership: immutable assignment descriptors, block rows, and step
  rows live until `scxml_program_destroy()`.
- Session ownership: validated environment override indices remain immutable
  until `scxml_session_destroy()`.
- Per-assignment rollback: one root-sized scratch object is allocated when an
  initializer block executes. Trivial state uses `memcpy`; managed state uses
  the root descriptor's copy/destroy/move traits. The scratch is refreshed
  before each non-overridden assignment, so a failed adapter restores only
  that assignment's input while earlier successful siblings remain staged.
- Input ownership: the caller's initial object is borrowed only during session
  initialization; CFlow owns its independent copy afterward.
- Thread topology: initializer actions run only on the borrowed serial
  executor. No new thread, callback wait, or cross-thread state mutation is
  introduced.
- Capacity: one early executable/block/step/state-action row is allocated only
  when early CMeta data declarations exist. Error Events consume the existing
  bounded internal queue. Scratch allocation is exactly one root object per
  executing initializer block; allocation or managed-copy failure fails the
  transaction explicitly.
- Shutdown: program rows and session state use existing destruction paths; no
  new drain protocol is required.

## CMeta empty-value contract

CMeta uses a host-declared typed object rather than creating dynamic variables.
The copied field value before a document initializer runs is therefore the
CMeta profile's empty/predeclared representation. This is consistent with the
SCXML allowance for platform-predeclared and predefined variables. A failed
initializer does not synthesize an unrelated scalar, zero arbitrary managed
storage, or restore from a second state mirror. A later legal `<assign>` writes
the same typed location normally.

V2 environment overrides are not empty values: their validated assignment
indices skip evaluation entirely, so host values remain authoritative and do
not generate initializer errors.

## Ordering and failure semantics

The root initializer entry action has order zero. Root entry precedes
descendant entry, so its staged error Events precede Events raised by initial
state `onentry` content. Multiple failing data declarations produce one Event
each in document order; successful siblings are still committed.

Early initialization evaluates `In()` against the empty configuration. Late
initialization retains the entered action-time configuration, so the owning
state is visible to `In()` before its `onentry`. Program-level CFlow bindings
execute the same early block without session-only environment overrides.

If assignment evaluation fails but staging `error.execution` succeeds, the
initializer block returns success so normal initialization continues. If the
reserved Event is missing or the bounded internal queue cannot accept it, the
runtime fails fast and discards the current StateChart transaction. It never
silently drops, reorders, or converts an internal error into an external Event.

## Alternatives considered

### Keep pre-StateChart evaluation and return a configuration error

Rejected because it cannot satisfy the required recoverable internal Event or
allow the declared location to be assigned later.

### Inject an adapter-internal Event after instance initialization

Rejected because CFlow initial stabilization may already execute and consume
Events raised by descendant `onentry` actions before the instance handle is
published. Post-initialization injection cannot preserve W3C ordering.

### Add a new CFlow seeded-internal-event API

Rejected for this milestone because the root executable boundary already
provides the required single-owner transaction. A new public CFlow API and ABI
would increase migration cost without adding SCXML-visible behavior.

### Put initialization on the synthetic root initial transition

Rejected because data initialization precedes global script evaluation in the
SCXML algorithm. A private root entry action keeps the initialization boundary
separate from document-authored initial-transition executable content.

## Compatibility, migration, and rollback

Successful early and late initializers preserve their current observable
values and ordering. V1 and V2 public session signatures remain unchanged.
Managed CMeta state still follows the existing copy/destroy traits, and
environment overrides retain their current skip semantics.

The intentional behavior change is limited to runtime-illegal data values:
session creation no longer reports invalid configuration when the error Event
can be staged. Instead, the session follows the SCXML recovery path. Callers
that relied on the old nonconforming failure must observe the session's
terminal/event result instead.

Rollback removes the private early block/step, restores pre-instance
assignment application, and restores fatal late-initializer errors. No stored
data, public struct, serialized format, or dependency migration is involved.

## Verification

- Focused early test: a runtime-illegal initializer queues
  `error.execution` before an initial `onentry` sentinel Event, then a later
  legal assignment updates the same field.
- Focused late test: the same failure is recoverable before the owning state's
  first `onentry` and is not retried on re-entry.
- Regression tests: existing early values, late transactions, environment
  overrides, and managed-state lifecycle remain green.
- W3C test 277: executable local transformation preserves the upstream error,
  empty/predeclared location, later assignment, and pass observation.
- Full Debug and Release preset builds and CTest runs.
