# W3C-derived SCXML regression fixtures

These fixtures are local transformations of documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
The inventory follows the upstream 10 March 2015 report: 200 assertions expand
to 202 test documents because assertion 403 has three starts. Of those
documents, 168 are mandatory and 34 optional. TurboSCXML currently executes 73
local PASS transformations, records 95 mandatory documents as UNSUPPORTED,
and records all 34 optional-profile documents as N/A. Passing this corpus is
not W3C certification and is not, by itself, a claim of complete SCXML
processor conformance.

`manifest.tsv` is the single machine-readable corpus fact source. Every
upstream start document has one row. Its nine tab-separated columns record the
local ID and fixture path, applicability, status, feature, exact upstream
source, expected result, transformation, and rationale. The harness rejects
duplicate IDs, fixture paths, and sources; malformed or empty columns;
applicability/status mismatches; missing PASS fixtures; inconsistent expected
results; undocumented source origins; and an inventory other than 202 rows,
168 mandatory rows, and 34 optional rows. The prose below explains PASS
transformations and profile boundaries but does not override manifest facts.

Status meanings are strict:

- `PASS` means the local fixture is executed and must deterministically reach
  its `pass` terminal, or the equivalent single-pass terminal checked by a
  specialized strict adapter harness.
- `UNSUPPORTED` means a mandatory upstream document remains outside the
  implemented or testable TurboSCXML profile. The row states the missing
  assertion rather than silently omitting it.
- `N/A` is reserved for optional profiles that TurboSCXML does not claim,
  currently ECMAScript, XPath, and BasicHTTP-specific behavior.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test144.scxml` | [test144.txml](https://www.w3.org/Voice/2013/scxml-irp/144/test144.txml) | Events produced by `raise` are appended to the internal queue in execution order. |
| `test147.scxml` | [test147.txml](https://www.w3.org/Voice/2013/scxml-irp/147/test147.txml) | An `if` executes only the first partition whose condition is true. |
| `test148.scxml` | [test148.txml](https://www.w3.org/Voice/2013/scxml-irp/148/test148.txml) | An `if` executes its `else` partition when every condition is false. |
| `test149.scxml` | [test149.txml](https://www.w3.org/Voice/2013/scxml-irp/149/test149.txml) | An `if` executes no partition when every condition is false and no `else` exists. |
| `test158.scxml` | [test158.txml](https://www.w3.org/Voice/2013/scxml-irp/158/test158.txml) | Elements in one executable-content block execute in document order. |
| `test159.scxml` | [test159.txml](https://www.w3.org/Voice/2013/scxml-irp/159/test159.txml) | An execution error prevents the remaining elements of the same block from executing. |
| `test179.scxml` | [test179.txml](https://www.w3.org/Voice/2013/scxml-irp/179/test179.txml) | Evaluated `send/content` bytes reach the host Event I/O boundary unmodified. |
| `test223.scxml` | [test223.txml](https://www.w3.org/Voice/2013/scxml-irp/223/test223.txml) | `invoke/@idlocation` receives the generated invocation ID before completion is processed. |
| `test224.scxml` | [test224.txml](https://www.w3.org/Voice/2013/scxml-irp/224/test224.txml) | The generated invocation ID has `stateid.platformid` form at both the CMeta location and adapter boundary. |
| `test287.scxml` | [test287.txml](https://www.w3.org/Voice/2013/scxml-irp/287/test287.txml) | A legal integer value is committed to a valid CMeta location before the following eventless guard is evaluated. |
| `test487.scxml` | [test487.txml](https://www.w3.org/Voice/2013/scxml-irp/487/test487.txml) | A finite numeric value that cannot be represented by its valid integer location raises `error.execution` and aborts the remaining executable-content block. |
| `test310.scxml` | [test310.txml](https://www.w3.org/Voice/2013/scxml-irp/310/test310.txml) | The CMeta data model reports an active parallel sibling through `In(stateID)`. |
| `test312.scxml` | [test312.txml](https://www.w3.org/Voice/2013/scxml-irp/312/test312.txml) | A runtime value-expression failure raises `error.execution` and aborts the remaining executable-content block. |
| `test314.scxml` | [test314.txml](https://www.w3.org/Voice/2013/scxml-irp/314/test314.txml) | A legal expression that fails at runtime raises its processor error only when the owning state is entered and evaluates it. |
| `test318.scxml` | [test318.txml](https://www.w3.org/Voice/2013/scxml-irp/318/test318.txml) | `_event` remains bound to the selected Event throughout exit and entry processing until another Event is selected. |
| `test319.scxml` | [test319.txml](https://www.w3.org/Voice/2013/scxml-irp/319/test319.txml) | `_event` is unbound during initialization before the first Event is selected. |
| `test321.scxml` | [test321.txml](https://www.w3.org/Voice/2013/scxml-irp/321/test321.txml) | `_sessionid` is bound to a generated session identifier during initialization. |
| `test322.scxml` | [test322.txml](https://www.w3.org/Voice/2013/scxml-irp/322/test322.txml) | `_sessionid` remains bound to the same generated identifier after a failed write. |
| `test323.scxml` | [test323.txml](https://www.w3.org/Voice/2013/scxml-irp/323/test323.txml) | `_name` is bound to the root `scxml/@name` value during initialization. |
| `test324.scxml` | [test324.txml](https://www.w3.org/Voice/2013/scxml-irp/324/test324.txml) | `_name` remains bound to the root `scxml/@name` value after a failed write. |
| `test325.scxml` | [test325.txml](https://www.w3.org/Voice/2013/scxml-irp/325/test325.txml) | `_ioprocessors` is bound to the supported Event I/O processor set during initialization. |
| `test326.scxml` | [test326.txml](https://www.w3.org/Voice/2013/scxml-irp/326/test326.txml) | `_ioprocessors` retains the supported SCXML processor location after a failed write. |
| `test329.scxml` | [test329.txml](https://www.w3.org/Voice/2013/scxml-irp/329/test329.txml) | Attempts to modify all four protected system variables fail without changing their values. |
| `test330.scxml` | [test330.txml](https://www.w3.org/Voice/2013/scxml-irp/330/test330.txml) | Every internal and external Event exposes all seven required Event fields, including empty optional values. |
| `test331.scxml` | [test331.txml](https://www.w3.org/Voice/2013/scxml-irp/331/test331.txml) | Raised, processor-generated, and externally admitted Events are classified as `internal`, `platform`, and `external`. |
| `test332.scxml` | [test332.txml](https://www.w3.org/Voice/2013/scxml-irp/332/test332.txml) | A failed send's platform error exposes the same generated ID through its CMeta `idlocation` and `_event.sendid`. |
| `test333.scxml` | [test333.txml](https://www.w3.org/Voice/2013/scxml-irp/333/test333.txml) | An ordinary external Event admitted without a send ID exposes an empty `sendid`. |
| `test335.scxml` | [test335.txml](https://www.w3.org/Voice/2013/scxml-irp/335/test335.txml) | An internally raised Event exposes an empty `origin`. |
| `test336.scxml` | [test336.txml](https://www.w3.org/Voice/2013/scxml-irp/336/test336.txml) | A committed external Event's `origin` and `origintype` are evaluated as the exact target and type of a committed reply. |
| `test337.scxml` | [test337.txml](https://www.w3.org/Voice/2013/scxml-irp/337/test337.txml) | Internal and platform Events expose an empty `origintype`. |
| `test338.scxml` | [test338.txml](https://www.w3.org/Voice/2013/scxml-irp/338/test338.txml) | A normal Event reported by an invoked child exposes the same live invocation ID through CMeta `idlocation` and `_event.invokeid`. |
| `test339.scxml` | [test339.txml](https://www.w3.org/Voice/2013/scxml-irp/339/test339.txml) | An internally raised Event that did not originate from an invoked child exposes an empty `invokeid`. |
| `test342.scxml` | [test342.txml](https://www.w3.org/Voice/2013/scxml-irp/342/test342.txml) | The selected Event's `_event.name` equals the name produced by its evaluated send expression. |
| `test346.scxml` | [test346.txml](https://www.w3.org/Voice/2013/scxml-irp/346/test346.txml) | Every attempted protected system-variable write raises a distinct internal `error.execution` Event. |
| `test355.scxml` | [test355.txml](https://www.w3.org/Voice/2013/scxml-irp/355/test355.txml) | With no root `initial`, the first child state in document order is selected. |
| `test364.scxml` | [test364.txml](https://www.w3.org/Voice/2013/scxml-irp/364/test364.txml) | Root, compound-IDREFS, explicit-transition, and document-order default initial selections are all entered. |
| `test372.scxml` | [test372.txml](https://www.w3.org/Voice/2013/scxml-irp/372/test372.txml) | A parent's `done.state` event is selected after its final child's `onentry` and before that child's `onexit`. |
| `test570.scxml` | [test570.txml](https://www.w3.org/Voice/2013/scxml-irp/570/test570.txml) | Child completion is processed before completion of the containing parallel after every region reaches final. |
| `test375.scxml` | [test375.txml](https://www.w3.org/Voice/2013/scxml-irp/375/test375.txml) | Multiple `onentry` handlers execute in document order. |
| `test376.scxml` | [test376.txml](https://www.w3.org/Voice/2013/scxml-irp/376/test376.txml) | An execution error aborts only its `onentry` block; a later independent handler still executes. |
| `test377.scxml` | [test377.txml](https://www.w3.org/Voice/2013/scxml-irp/377/test377.txml) | Multiple `onexit` handlers execute in document order. |
| `test378.scxml` | [test378.txml](https://www.w3.org/Voice/2013/scxml-irp/378/test378.txml) | An execution error aborts only its `onexit` block; a later independent handler still executes. |
| `test387.scxml` | [test387.txml](https://www.w3.org/Voice/2013/scxml-irp/387/test387.txml) | An unset shallow or deep history slot enters its declared default stored configuration. |
| `test388.scxml` | [test388.txml](https://www.w3.org/Voice/2013/scxml-irp/388/test388.txml) | A visited compound restores its stored deep leaf and its stored shallow child with default descent. |
| `test396.scxml` | [test396.txml](https://www.w3.org/Voice/2013/scxml-irp/396/test396.txml) | `_event.name` equals the Event name used to select the matching transition. |
| `test399.scxml` | [test399.txml](https://www.w3.org/Voice/2013/scxml-irp/399/test399.txml) | Event descriptor unions, token prefixes, token boundaries, `.*`, and `*` select exactly the intended transitions. |
| `test401.scxml` | [test401.txml](https://www.w3.org/Voice/2013/scxml-irp/401/test401.txml) | A processor-generated `error.execution` Event is selected before an already-queued external Event. |
| `test402.scxml` | [test402.txml](https://www.w3.org/Voice/2013/scxml-irp/402/test402.txml) | A processor-generated error retains FIFO position in the internal queue and participates in ordinary event-descriptor matching. |
| `test579.scxml` | [test579.txml](https://www.w3.org/Voice/2013/scxml-irp/579/test579.txml) | Unset history transition content executes after the parent's `onentry` and initial-transition content. |
| `test580.scxml` | [test580.txml](https://www.w3.org/Voice/2013/scxml-irp/580/test580.txml) | A history pseudo-state never appears in the active configuration. |
| `test576.scxml` | [test576.txml](https://www.w3.org/Voice/2013/scxml-irp/576/test576.txml) | Root `initial` IDREFS enter both deeply nested non-default siblings of one parallel state. |
| `test413.scxml` | [test413.txml](https://www.w3.org/Voice/2013/scxml-irp/413/test413.txml) | Startup enters both non-default leaves selected by the root `initial` IDREFS. |
| `test403a.scxml` | [test403a.txml](https://www.w3.org/Voice/2013/scxml-irp/403/test403a.txml) | Transition selection prefers descendant sources, then document order, and falls through disabled conditions. |
| `test404.scxml` | [test404.txml](https://www.w3.org/Voice/2013/scxml-irp/404/test404.txml) | States execute `onexit` content in exit order before transition content. |
| `test405.scxml` | [test405.txml](https://www.w3.org/Voice/2013/scxml-irp/405/test405.txml) | Selected transition content executes in document order after all required exits. |
| `test406.scxml` | [test406.txml](https://www.w3.org/Voice/2013/scxml-irp/406/test406.txml) | Transition content executes before states enter in parent-before-child, document order. |
| `test407.scxml` | [test407.txml](https://www.w3.org/Voice/2013/scxml-irp/407/test407.txml) | A state's `onexit` content executes when the state leaves the active configuration. |
| `test409.scxml` | [test409.txml](https://www.w3.org/Voice/2013/scxml-irp/409/test409.txml) | A state leaves the active configuration after its own `onexit` and before an ancestor's `onexit`. |
| `test411.scxml` | [test411.txml](https://www.w3.org/Voice/2013/scxml-irp/411/test411.txml) | A state enters the active configuration immediately before its own `onentry`. |
| `test412.scxml` | [test412.txml](https://www.w3.org/Voice/2013/scxml-irp/412/test412.txml) | Initial-transition content executes after the parent's `onentry` and before the child's `onentry`. |
| `test415.scxml` | [test415.txml](https://www.w3.org/Voice/2013/scxml-irp/415/test415.txml) | Entering a root final halts processing before an internal event raised by its `onentry` is selected. |
| `test416.scxml` | [test416.txml](https://www.w3.org/Voice/2013/scxml-irp/416/test416.txml) | Entering a compound state's final child generates `done.state.<id>`. |
| `test417.scxml` | [test417.txml](https://www.w3.org/Voice/2013/scxml-irp/417/test417.txml) | Completing every region generates the parallel state's `done.state.<id>` event. |
| `test419.scxml` | [test419.txml](https://www.w3.org/Voice/2013/scxml-irp/419/test419.txml) | An enabled eventless transition is selected before a queued internal event. |
| `test421.scxml` | [test421.txml](https://www.w3.org/Voice/2013/scxml-irp/421/test421.txml) | Unmatched internal events are removed until one enables a transition or the internal queue is empty. |
| `test503.scxml` | [test503.txml](https://www.w3.org/Voice/2013/scxml-irp/503/test503.txml) | A targetless transition has an empty exit set. |
| `test504.scxml` | [test504.txml](https://www.w3.org/Voice/2013/scxml-irp/504/test504.txml) | An external transition exits every active proper descendant of the source/target LCCA. |
| `test505.scxml` | [test505.txml](https://www.w3.org/Voice/2013/scxml-irp/505/test505.txml) | An internal transition from a compound state to a proper descendant retains the source state. |
| `test506.scxml` | [test506.txml](https://www.w3.org/Voice/2013/scxml-irp/506/test506.txml) | An internal transition whose target is not a proper descendant uses external transition-domain semantics. |
| `test527.scxml` | [test527.txml](https://www.w3.org/Voice/2013/scxml-irp/527/test527.txml) | A `content/@expr` string value becomes the selected completion Event's `_event.data`. |
| `test529.scxml` | [test529.txml](https://www.w3.org/Voice/2013/scxml-irp/529/test529.txml) | Inline text children become the selected completion Event's `_event.data` without alteration. |
| `test533.scxml` | [test533.txml](https://www.w3.org/Voice/2013/scxml-irp/533/test533.txml) | An internal transition from a non-compound source uses external transition-domain semantics. |
| `test436.scxml` | [test436.txml](https://www.w3.org/Voice/2013/scxml-irp/436/test436.txml) | The null data model reports an inactive state as false and an active parallel sibling as true through `In(stateID)`. |

The upstream `.txml` files use a `conf:` vocabulary consumed by the W3C test
generation pipeline. Every local transformation replaces `conf:pass` and
`conf:fail` with ordinary SCXML `final` states named `pass` and `fail`, removes
test-generation metadata, selects the null or CMeta datamodel required by the
local witness, and keeps the executable structure that observes the assertion.

Tests 144, 147, 148, 149, 158, 375, 377, 404, 405, 406, and 412 replace wildcard
failure transitions with finite exact events so the local `pass`/`fail` finals
observe each expected ordering path directly. Test
147 and 148 replace generator counters with a post-conditional event that
observes both the selected partition and the absence of any later partition.
Test 149 uses the same post-conditional event to prove that no partition ran,
and test 158 retains the upstream two-event document-order trace. Test 412 also
removes redundant generator-level parent sentinels while retaining the complete
three-event observation chain. Tests 405, 406, 412, 416, and 417
omit the upstream one-second timeout `send`; it is only a liveness safety net,
while the local harness directly fails any run that does not reach `pass`.
Tests 399, 413, and 576 retain the upstream event-descriptor and root
multi-target structures respectively; they only remove generator metadata and
timeout failure sends that the local synchronous harness does not need. Test
364 retains all three upstream default-entry paths: compound `initial` IDREFS,
an explicit initial transition, and recursive first-child document-order
selection. Its finite entry events replace generator pass/fail targets without
changing which active configuration advances each stage.
Test 419 keeps the queued internal event as the failure witness, replaces the
wildcard with that exact event, and omits the additional external `send`; the
retained event is sufficient to distinguish eventless-transition precedence.
Test 401 replaces the upstream self-send with two host admissions while the
serial executor is held: `start` enters the assertion state and `foo` is already
in the external queue before its protected `_name` write generates
`error.execution`. Reaching `pass` therefore observes internal-over-external
priority rather than host delivery timing. Test 402 replaces the generator's
invalid data-model operation with the same runtime-failing protected `_name`
write. It preserves the upstream `event1`, processor error, and `event2` trace;
the `error` descriptor deliberately matches the generated `error.execution`
Event through ordinary prefix semantics.
Test 403a replaces generator counters with two queued events: the first checks
descendant and document-order priority, and the second checks condition
fallthrough to an ancestor. Tests 409 and 411 retain their `In(state)` timing
checks but replace generator pass/fail operations and timeout sends with exact
internal success and failure events. Tests 503, 505, 506, and 533 replace exit
counters and wildcard observers with finite ordered `onexit` event chains. A
missing, extra, or misordered exit either reaches `fail` or prevents the harness
from observing completion. Their upstream timeout sends are omitted because the
local harness already requires each run to terminate in `pass` without a runtime
error.

Tests 372 and 570 replace the generator's anonymous integer slot with the
test-only CMeta `sequence` field. Test 372 accepts the parent completion only
while that field contains the final child's `onentry` value; the child's
`onexit` writes a distinct later value. Test 570 records the first region's
child completion and accepts the parallel completion only after that write.
Their terminal states send exactly one `result.pass` or `result.fail` effect to
a bounded test adapter so the owning CMeta session remains a black box. Test
415 retains the upstream root final and its `event1` raise. Its direct CFlow
event hook observes transition-selection boundaries and requires the raise
action to execute while `event1` is never selected before clean termination.

Tests 318, 319, 321-326, 329, 330, 331, 333, 335, 337, 339, 342, 346, and 396 use
the owning CMeta session so protected variables are observed through the
public execution path. Test 318
raises another Event before checking that `_event.name` still names the Event
whose transition is being processed. Test 319 maps the generator's
`conf:systemVarIsBound` query to the finite CMeta expression
`isBound(_event)`. Tests 321, 323, and 325 map the same generator predicate to
`isBound(_sessionid)`, `isBound(_name)`, and `isBound(_ioprocessors)` during
initial eventless processing. Tests 322 and 326 snapshot the session ID and
SCXML processor location respectively, require the protected write to raise
`error.execution`, and compare the retained value. Test 324 observes `_name`
before and after its failed write while a following staged assignment proves
the block was aborted. Test 329 retains one selected `foo` Event while
eventless transitions check all four protected values and four block-abort
sentinels before queued processor errors can replace `_event`. Test 346
advances only on four separate `error.execution` Events and retains four raised
Events as unreachable suffix sentinels. Test 330 checks the complete
seven-field envelope
on a raised internal Event and a bounded host-admitted external Event. Test 331
uses an internal raise, a processor-generated execution error, and an external
admission to observe all three Event classifications. Tests 333, 335, and 337
require empty optional metadata to remain present in the applicable external,
internal, and platform envelopes. Test 339 compares the empty non-invoke
`invokeid`. Test 342 retains `eventexpr`; its test host exposes the copied send
only after ticket commit, then loops it through the public external-admission
API before the fixture compares `_event.name`. Test 396 compares the selected
Event name directly. Their terminal states use the same bounded
`result.pass`/`result.fail` adapter probe as the completion fixtures.

Tests 376 and 378 replace the generator counter with a `second.block` event.
Their test-only owning sessions inject `SCXML_ADAPTER_ERROR_EXECUTION`
for the first handler's `send`, then require `error.execution` followed by the
event from the later independent handler. Test 159 uses the same deterministic
adapter failure, rejects an event from the remainder of the failing block, and
accepts only the witness raised by the next independent `onentry` block. Test
387 preserves both unset-history targets and replaces wildcard failures with
the finite wrong leaf-entry events; its timeout send is omitted because the
harness requires terminal completion. Test 388 replaces the generator counter
with transition-time restoration events and strict `In(id)` guards. The deep
history leg must restore `s012`; after that configuration is exited, the shallow
history leg must restore `s01` and descend to its default `s011`. Declared
history defaults point elsewhere so an unset-slot path cannot pass. Test 579
replaces the generator counter and timeout with a finite two-pass event trace. The first pass requires
parent-entry `event1`, initial-transition `event2`, and unset-history `event3`
in order. Stored-history reentry requires `event1`, `event2`, and a leaf-entry
witness while rejecting `event3`, proving that the history default content is
suppressed after the slot is set. Test 580 keeps exact `In(sh1)` guards at
child, parent, exited, and restored observation points, replacing only generator
pass/fail operations with local terminal states and internal events. Test 407
replaces the exit counter with one exact internal exit event. Test 421
retains the four internal events and matches only the third and fourth, which
directly observes the named internal-queue draining assertion; the upstream
external-send failure witness is outside that assertion and is omitted. Test
504 replaces five counters with the two complete reverse-document exit traces
produced by its external transitions. Exact observers require both parallel
regions and their parallel parent to exit twice, and the containing state to
exit once.

Tests 527 and 529 retain the upstream `donedata/content` completion path and
replace generator predicates with exact CMeta `_event.data` guards. Test 527
maps the generated quoted expression to `expr="&quot;foo&quot;"`; test 529
keeps the inline text child `21`. Test 528 remains `UNSUPPORTED`: its separate
error-before-completion and empty-data requirements are not claimed by these
successful content witnesses.

Test 287 maps the generated data ID to the CMeta integer `sequence`, assigns
the literal value `1`, and observes that committed value from the following
eventless guard. Test 487 keeps a valid integer location but maps the generated
illegal value to `1e100`: this is a finite floating expression accepted by the
numeric assignment compiler, while exact integer conversion fails only when
the assignment executes. Its following `foo` raise and a FIFO confirmation
Event distinguish block abortion from merely queuing `error.execution`. Test
286 remains `UNSUPPORTED` because unknown CMeta locations are currently
rejected during document compilation rather than admitted as runtime-failing
assignments.

Tests 312 and 314 map the generated illegal value expression to the legal
CMeta expression `_event.data.sequence` while `_event` is unbound. Compilation
therefore succeeds, but evaluation fails at the same executable-content point
as the upstream assertion. Each fixture keeps the following `raise event="foo"`
as a block-abort sentinel, then queues a confirmation Event after selecting
`error.execution`; FIFO ordering exposes any incorrectly retained `foo` before
the fixture can pass. Test 314 advances through `s01` and `s02` with internal
Events, while the compound parent's `error.execution` transition rejects any
premature evaluation before `s03`. This does not claim support for syntactically
invalid documents or recoverable transition-guard failures.

Test 179 replaces upstream self-delivery with the versioned v3 host Event I/O
adapter. The fixture still evaluates literal `content` when `send` executes;
the adapter is the external-service boundary and requires the exact UTF-8 bytes
`123` before the only terminal path can complete. Tests 223 and 224 replace the
invoked child processor with the versioned host invoke adapter. The adapter
observes the committed generated ID and reports completion through that same
invocation token. The fixtures independently require the writable CMeta
`idlocation` to be nonempty and exactly `s0.1`, so removing the generated child
does not weaken either binding or `stateid.platformid` witness.

The late-binding implementation does not justify weakening upstream test 280:
TurboSCXML uses caller-supplied typed CMeta storage, so a declared field exists
before its state-local initializer runs. Reads before first entry therefore
observe the caller value instead of the upstream generated datamodel's
unbound-location error. Test 280 remains explicitly `UNSUPPORTED`; the local
late-binding transaction, first-entry, re-entry, history, and rollback tests
remain implementation tests rather than being relabeled as W3C PASS.

## Current state-membership profile

Null-model and CMeta `In(id)` expressions admit any ID-bearing SCXML state,
including history pseudo-states. The W3C `<initial>` element has no attributes
and cannot be named. The native Statechart active configuration remains the
single fact source: pseudo-states are never active, so a declared history query
evaluates to false. Unknown IDs still fail program admission.

Tests 310 and 436 cover the complete TurboSCXML data-model set. Test 310 maps
the generator predicate to CMeta `In("s1")` while `s1` is active in a parallel
region. Test 436 retains the upstream null-model order: it first requires
inactive root sibling `s1` to evaluate false, then requires active parallel
sibling `ps1` to evaluate true. Both paths read the same native active
configuration rather than maintaining a data-model-specific state mirror.

## Current CMeta system-event profile

CMeta sessions expose the complete read-only `_event` profile from
[SCXML 1.0 section 5.10.1](https://www.w3.org/TR/2015/REC-scxml-20150901/#InternalStructureofEvents):
`name`, `type`, `sendid`, `origin`, `origintype`, `invokeid`, and `data`.
`type` is `internal` for raised, internal-send, and state-completion Events,
`platform` for processor-generated error Events, and `external` for admitted
external and invocation-completion Events. Missing optional metadata is the
empty CMeta string; selecting the next Event clears metadata not supplied by
that Event instead of retaining stale values.

The finite predicate `isBound(_event)` and the seven exact field forms for
`name`, `type`, `sendid`, `origin`, `origintype`, `invokeid`, and `data`
distinguish the absent startup Event from fields in a selected envelope.
Unknown and nested field queries remain compile errors. Structured `data` is
considered present through its CMeta schema/object witness even though it has
no scalar string view.

The selected Event remains current through all eventless microsteps in the
same run-to-completion cycle. Initial eventless work has no current Event and
therefore fails evaluation when it reads `_event`. Scalar/text/XML data is
exposed as a bounded string. `scxml_session_try_send_v3()` additionally
copies structured CMeta data whose descriptor is exactly the compiled session
root, allowing typed paths such as `_event.data.order.count`. The copy is owned
by the session until the next Event is selected or the session is destroyed.
Because a structured value is not a string, reading it as bare `_event.data`
fails evaluation instead of silently substituting an empty value.
The fixed storage cost is bounded by
`external_event_capacity * SCXML_EVENT_DATA_CAPACITY`, plus one current
Event slot and row metadata.

Format parsing remains outside the SCXML runtime. An embedding application may
use CBind/CSerde to convert JSON, XML, YAML, or another format into the compiled
root CMeta object, then admit that object through the v3 API. This keeps codecs
and their errors out of transition selection. Invalid envelopes, unsupported
content, schema mismatches, bare/unknown `_event` paths, and every write to an
`_event` location fail fast without a compatibility fallback.

## Event I/O conformance boundary

The module deliberately retains a conforming-host adapter boundary instead of
bundling a cross-session registry or transport. The public location-copy API
lets a host register the exact address exposed through
`_ioprocessors.scxml.location`, and
`scxml_event_io_contract_test` demonstrates bounded routing and delivery
through only public APIs. This evidence validates the adapter contract; it does
not make the library alone a standalone SCXML Event I/O Processor. Accordingly,
tests 189-192, 347-354, 495-496, and 500-501 remain `UNSUPPORTED` until the W3C
fixtures are executed with a selected conforming host implementation.

The upstream suite page offers the tests under the
[W3C Test Suite License](https://www.w3.org/copyright/test-suite/) or the
[W3C 3-clause BSD License](https://www.w3.org/Consortium/Legal/2015/copyright-software-and-document).
Keep this provenance and transformation record with any copied or extended
fixture set.

Copyright © 2013 World Wide Web Consortium. W3C liability, trademark, and
document-use rules are governed by the selected license above.
