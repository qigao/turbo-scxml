# W3C-derived SCXML regression fixtures

These fixtures are local transformations of documents from the
[W3C SCXML 1.0 Implementation Report test suite](https://www.w3.org/Voice/2013/scxml-irp/).
The inventory follows the upstream 10 March 2015 report: 200 assertions expand
to 202 test documents because assertion 403 has three starts. Of those
documents, 168 are mandatory and 34 optional. TurboSCXML currently records 156
local PASS transformations, records 12 mandatory documents as UNSUPPORTED,
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

- `PASS` means the local fixture has a deterministic witness: it either reaches
  its `pass` terminal (or an equivalent strict-adapter terminal), or, only when
  the upstream assertion explicitly permits load rejection, compilation fails
  with the exact status checked by a specialized harness.
- `UNSUPPORTED` means a mandatory upstream document remains outside the
  implemented or testable TurboSCXML profile. The row states the missing
  assertion rather than silently omitting it.
- `N/A` is reserved for optional profiles that TurboSCXML does not claim,
  currently ECMAScript, XPath, and BasicHTTP-specific behavior.

The current CMeta `<donedata><param>` profile is deliberately narrower than
the complete W3C failure semantics. Successful params are materialized from
one immutable state snapshot. Each failed param queues `error.execution` and is
omitted, while successful siblings are published through a slot-owned subset
schema. Projection fields and stable IDs are preallocated at session creation,
so projection metadata adds no runtime allocation and remains bounded by the
compiled descriptor. Existing CMeta object-copy and field-adapter allocation
semantics are unchanged. Tests 294, 298, 343, and 488 cover full, empty, and
mixed completion-data outcomes.

| Local fixture | Upstream source | Assertion preserved |
| --- | --- | --- |
| `test144.scxml` | [test144.txml](https://www.w3.org/Voice/2013/scxml-irp/144/test144.txml) | Events produced by `raise` are appended to the internal queue in execution order. |
| `test147.scxml` | [test147.txml](https://www.w3.org/Voice/2013/scxml-irp/147/test147.txml) | An `if` executes only the first partition whose condition is true. |
| `test148.scxml` | [test148.txml](https://www.w3.org/Voice/2013/scxml-irp/148/test148.txml) | An `if` executes its `else` partition when every condition is false. |
| `test149.scxml` | [test149.txml](https://www.w3.org/Voice/2013/scxml-irp/149/test149.txml) | An `if` executes no partition when every condition is false and no `else` exists. |
| `test152.scxml` | [test152.txml](https://www.w3.org/Voice/2013/scxml-irp/152/test152.txml) | Invalid iterable admission or an invalid item location raises `error.execution` and aborts the containing executable-content block. |
| `test153.scxml` | [test153.txml](https://www.w3.org/Voice/2013/scxml-irp/153/test153.txml) | A `foreach` assigns ordered collection items from first to last together with their zero-based indexes. |
| `test155.scxml` | [test155.txml](https://www.w3.org/Voice/2013/scxml-irp/155/test155.txml) | A `foreach` executes its child content after assigning each item and before advancing. |
| `test156.scxml` | [test156.txml](https://www.w3.org/Voice/2013/scxml-irp/156/test156.txml) | A child execution error stops the `foreach` and aborts its containing executable-content block. |
| `test525.scxml` | [test525.txml](https://www.w3.org/Voice/2013/scxml-irp/525/test525.txml) | Modifying the source collection during the first `foreach` body does not change the captured iteration values, order, or count. |
| `test158.scxml` | [test158.txml](https://www.w3.org/Voice/2013/scxml-irp/158/test158.txml) | Elements in one executable-content block execute in document order. |
| `test159.scxml` | [test159.txml](https://www.w3.org/Voice/2013/scxml-irp/159/test159.txml) | An execution error prevents the remaining elements of the same block from executing. |
| `test172.scxml` | [test172.txml](https://www.w3.org/Voice/2013/scxml-irp/172/test172.txml) | `send/@eventexpr` reads the value assigned immediately before the send executes. |
| `test173.scxml` | [test173.txml](https://www.w3.org/Voice/2013/scxml-irp/173/test173.txml) | `send/@targetexpr` reads the current value and routes through `#_internal`. |
| `test174.scxml` | [test174.txml](https://www.w3.org/Voice/2013/scxml-irp/174/test174.txml) | `send/@typeexpr` reads the current canonical SCXML Event Processor URI. |
| `test175.scxml` | [test175.txml](https://www.w3.org/Voice/2013/scxml-irp/175/test175.txml) | `send/@delayexpr` reads the current interval before the delayed request is committed. |
| `test176.scxml` | [test176.txml](https://www.w3.org/Voice/2013/scxml-irp/176/test176.txml) | A send parameter reads its current CMeta value and reaches the host unchanged. |
| `test178.scxml` | [test178.txml](https://www.w3.org/Voice/2013/scxml-irp/178/test178.txml) | Duplicate send payload names retain document order and both distinct values. |
| `test179.scxml` | [test179.txml](https://www.w3.org/Voice/2013/scxml-irp/179/test179.txml) | Evaluated `send/content` bytes reach the host Event I/O boundary unmodified. |
| `test183.scxml` | [test183.txml](https://www.w3.org/Voice/2013/scxml-irp/183/test183.txml) | `send/@idlocation` contains the generated ID before the sent Event is selected. |
| `test185.scxml` | [test185.txml](https://www.w3.org/Voice/2013/scxml-irp/185/test185.txml) | A delayed send retains its parsed interval and cannot overtake an immediate send. |
| `test186.scxml` | [test186.txml](https://www.w3.org/Voice/2013/scxml-irp/186/test186.txml) | Delayed-send arguments are materialized when `send` executes rather than when the host later dispatches them. |
| `test187.scxml` | [test187.txml](https://www.w3.org/Voice/2013/scxml-irp/187/test187.txml) | Session destruction closes the host and cancels a committed delayed message before delivery. |
| `test194.scxml` | [test194.txml](https://www.w3.org/Voice/2013/scxml-irp/194/test194.txml) | An unsupported or invalid send target raises `error.execution`. |
| `test198.scxml` | [test198.txml](https://www.w3.org/Voice/2013/scxml-irp/198/test198.txml) | A `send` with neither `type` nor `typeexpr` uses the canonical SCXML Event Processor and the received Event identifies that processor. |
| `test199.scxml` | [test199.txml](https://www.w3.org/Voice/2013/scxml-irp/199/test199.txml) | An unsupported Event Processor type raises `error.execution`. |
| `test200.scxml` | [test200.txml](https://www.w3.org/Voice/2013/scxml-irp/200/test200.txml) | The required explicit SCXML Event Processor type is accepted and delivers the Event. |
| `test205.scxml` | [test205.txml](https://www.w3.org/Voice/2013/scxml-irp/205/test205.txml) | The host receives the exact sent Event name and named scalar payload. |
| `test521.scxml` | [test521.txml](https://www.w3.org/Voice/2013/scxml-irp/521/test521.txml) | A send that cannot be dispatched raises `error.communication`. |
| `test553.scxml` | [test553.txml](https://www.w3.org/Voice/2013/scxml-irp/553/test553.txml) | A send whose arguments fail evaluation is discarded before host delivery. |
| `test207.scxml` | [test207.txml](https://www.w3.org/Voice/2013/scxml-irp/207/test207.txml) | A cancel in one session cannot observe or remove a same-named delayed send owned by another session. |
| `test208.scxml` | [test208.txml](https://www.w3.org/Voice/2013/scxml-irp/208/test208.txml) | A literal `cancel/@sendid` removes the matching delayed send from the same session. |
| `test210.scxml` | [test210.txml](https://www.w3.org/Voice/2013/scxml-irp/210/test210.txml) | `cancel/@sendidexpr` is evaluated when the cancel element executes and resolves the generated delayed-send ID. |
| `test215.scxml` | [test215.txml](https://www.w3.org/Voice/2013/scxml-irp/215/test215.txml) | `invoke/@typeexpr` reads the value assigned during `onentry`, and the strict host receives the canonical SCXML type. |
| `test216.scxml` | [test216.txml](https://www.w3.org/Voice/2013/scxml-irp/216/test216.txml) | `invoke/@srcexpr` reads the source assigned during `onentry`, and the strict host receives the updated URL. |
| `test220.scxml` | [test220.txml](https://www.w3.org/Voice/2013/scxml-irp/220/test220.txml) | The platform host accepts and commits the canonical SCXML invocation type before returning completion. |
| `test223.scxml` | [test223.txml](https://www.w3.org/Voice/2013/scxml-irp/223/test223.txml) | `invoke/@idlocation` receives the generated invocation ID before completion is processed. |
| `test224.scxml` | [test224.txml](https://www.w3.org/Voice/2013/scxml-irp/224/test224.txml) | The generated invocation ID has `stateid.platformid` form at both the CMeta location and adapter boundary. |
| `test225.scxml` | [test225.txml](https://www.w3.org/Voice/2013/scxml-irp/225/test225.txml) | Two invocations in one session receive different nonzero tokens and generated IDs at both idlocations and the host boundary. |
| `test226.scxml` | [test226.txml](https://www.w3.org/Voice/2013/scxml-irp/226/test226.txml) | The strict host receives the canonical type, exact source URL, and named integer parameter before returning the child Event. |
| `test228.scxml` | [test228.txml](https://www.w3.org/Voice/2013/scxml-irp/228/test228.txml) | The completion Event returned through a live token exposes that invocation's exact ID through `_event.invokeid`. |
| `test229.scxml` | [test229.txml](https://www.w3.org/Voice/2013/scxml-irp/229/test229.txml) | A child Event is copied back to the same `autoforward` invocation; only the committed host reservation produces the child's `eventReceived` reply. |
| `test230.scxml` | [test230.txml](https://www.w3.org/Voice/2013/scxml-irp/230/test230.txml) | The invocation adapter receives exact copies of all seven SCXML Event fields through the borrowed versioned envelope. |
| `test232.scxml` | [test232.txml](https://www.w3.org/Voice/2013/scxml-irp/232/test232.txml) | The host reports two normal Events and completion in that order; the parent's three-state sequence observes the same FIFO order. |
| `test233.scxml` | [test233.txml](https://www.w3.org/Voice/2013/scxml-irp/233/test233.txml) | The matching invocation's `finalize` assignment commits before the returned Event's transition guard is evaluated. |
| `test234.scxml` | [test234.txml](https://www.w3.org/Voice/2013/scxml-irp/234/test234.txml) | A returned Event executes only the `finalize` belonging to its exact committed invocation token. |
| `test235.scxml` | [test235.txml](https://www.w3.org/Voice/2013/scxml-irp/235/test235.txml) | Completion for explicit invocation ID `foo` passes only when the selected Event's `_event.name` is exactly `done.invoke.foo`. |
| `test236.scxml` | [test236.txml](https://www.w3.org/Voice/2013/scxml-irp/236/test236.txml) | A normal return precedes completion; after completion is processed, the stale token is rejected and its late Event cannot reach selection. |
| `test237.scxml` | [test237.txml](https://www.w3.org/Voice/2013/scxml-irp/237/test237.txml) | Leaving the invoking state commits cancellation of a real host-owned child; the cancelled child rejects further processing and its stale completion token is rejected by the parent. |
| `test239.scxml` | [test239.txml](https://www.w3.org/Voice/2013/scxml-irp/239/test239.txml) | Real host-owned children execute both an allowlisted `src` document and copied inline XML, returning distinct witness Events. |
| `test240.scxml` | [test240.txml](https://www.w3.org/Voice/2013/scxml-irp/240/test240.txml) | Namelist and param values are copied into a closed child CMeta schema before two real child sessions run. |
| `test241.scxml` | [test241.txml](https://www.w3.org/Voice/2013/scxml-irp/241/test241.txml) | Namelist and param inject the same nondefault integer and produce identical child-visible success. |
| `test242.scxml` | [test242.txml](https://www.w3.org/Voice/2013/scxml-irp/242/test242.txml) | An allowlisted `src` child and inline child execute the same behavior and each returns `childRan`. |
| `test243.scxml` | [test243.txml](https://www.w3.org/Voice/2013/scxml-irp/243/test243.txml) | A matching param initializes the typed child `child_value` field before execution. |
| `test244.scxml` | [test244.txml](https://www.w3.org/Voice/2013/scxml-irp/244/test244.txml) | A matching namelist key initializes the typed child `child_value` field before execution. |
| `test245.scxml` | [test245.txml](https://www.w3.org/Voice/2013/scxml-irp/245/test245.txml) | An unmatched named value is ignored; the closed child schema retains its declared field's zero default. |
| `test247.scxml` | [test247.txml](https://www.w3.org/Voice/2013/scxml-irp/247/test247.txml) | A host-owned real child session reaches top-level final before exactly one completion is reported through the real parent's committed token. |
| `test250.scxml` | [test250.txml](https://www.w3.org/Voice/2013/scxml-irp/250/test250.txml) | Cancelling a real nested child runs the active `sub01` and `sub0` `onexit` handlers in descendant-before-ancestor order and does not report normal invocation completion. |
| `test252.scxml` | [test252.txml](https://www.w3.org/Voice/2013/scxml-irp/252/test252.txml) | After cancellation, both a normal child Event and completion report through the stale token are rejected; only an independent parent Event can reach pass. |
| `test253.scxml` | [test253.txml](https://www.w3.org/Voice/2013/scxml-irp/253/test253.txml) | One active canonical SCXML invocation exchanges `childRunning`, `parentToChild`, and `success` through `#_parent`/`#_foo`; both receivers require the SCXML Event I/O `origintype`. |
| `test530.scxml` | [test530.txml](https://www.w3.org/Voice/2013/scxml-irp/530/test530.txml) | Invoke content observes the value assigned in `onentry`, proving evaluation at invocation rather than admission. |
| `test554.scxml` | [test554.txml](https://www.w3.org/Voice/2013/scxml-irp/554/test554.txml) | A runtime argument error raises `error.execution` and produces no host start request. |
| `test279.scxml` | [test279.txml](https://www.w3.org/Voice/2013/scxml-irp/279/test279.txml) | Default early binding initializes data declared in an inactive sibling before the initial state reads it. |
| `test286.scxml` | [test286.txml](https://www.w3.org/Voice/2013/scxml-irp/286/test286.txml) | An unknown assignment location raises internal `error.execution` and aborts the remaining executable-content block. |
| `test287.scxml` | [test287.txml](https://www.w3.org/Voice/2013/scxml-irp/287/test287.txml) | A legal integer value is committed to a valid CMeta location before the following eventless guard is evaluated. |
| `test294.scxml` | [test294.txml](https://www.w3.org/Voice/2013/scxml-irp/294/test294.txml) | A named param becomes a structured completion Event field, while a later inline content child remains the full completion data value. |
| `test298.scxml` | [test298.txml](https://www.w3.org/Voice/2013/scxml-irp/298/test298.txml) | An unavailable param location queues `error.execution` before completion and contributes no field to that completion Event's data. |
| `test343.scxml` | [test343.txml](https://www.w3.org/Voice/2013/scxml-irp/343/test343.txml) | A valid `sequence` param survives in completion data when a later invalid `send_id` location queues `error.execution`; the projected schema omits only `send_id`. |
| `test487.scxml` | [test487.txml](https://www.w3.org/Voice/2013/scxml-irp/487/test487.txml) | A finite numeric value that cannot be represented by its valid integer location raises `error.execution` and aborts the remaining executable-content block. |
| `test488.scxml` | [test488.txml](https://www.w3.org/Voice/2013/scxml-irp/488/test488.txml) | A failed param expression queues `error.execution` before completion and leaves that completion Event's `_event.data` empty. |
| `test550.scxml` | [test550.txml](https://www.w3.org/Voice/2013/scxml-irp/550/test550.txml) | Explicit early binding evaluates `data/@expr` and assigns its result before the declaring state is entered. |
| `test309.scxml` | [test309.txml](https://www.w3.org/Voice/2013/scxml-irp/309/test309.txml) | A non-Boolean transition condition is treated as false, allowing the unconditional fallback to run. |
| `test310.scxml` | [test310.txml](https://www.w3.org/Voice/2013/scxml-irp/310/test310.txml) | The CMeta data model reports an active parallel sibling through `In(stateID)`. |
| `test311.scxml` | [test311.txml](https://www.w3.org/Voice/2013/scxml-irp/311/test311.txml) | A location path that traverses a scalar cannot yield a valid location and raises internal `error.execution`. |
| `test312.scxml` | [test312.txml](https://www.w3.org/Voice/2013/scxml-irp/312/test312.txml) | A runtime value-expression failure raises `error.execution` and aborts the remaining executable-content block. |
| `test313.scxml` | [test313.txml](https://www.w3.org/Voice/2013/scxml-irp/313/test313.txml) | A syntactically ill-formed CMeta value expression rejects the document at load time with `SCXML_INVALID_STRUCTURE`. |
| `test314.scxml` | [test314.txml](https://www.w3.org/Voice/2013/scxml-irp/314/test314.txml) | A legal expression that fails at runtime raises its processor error only when the owning state is entered and evaluates it. |
| `test344.scxml` | [test344.txml](https://www.w3.org/Voice/2013/scxml-irp/344/test344.txml) | A non-Boolean transition condition queues `error.execution` before Events raised by the fallback transition's entry actions. |
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
| `test189.scxml` | [test189.txml](https://www.w3.org/Voice/2013/scxml-irp/189/test189.txml) | A `#_internal` send is selected from the sending session's internal queue before an earlier external send is dispatched. |
| `test190.scxml` | [test190.txml](https://www.w3.org/Voice/2013/scxml-irp/190/test190.txml) | A send to the current `#_scxml_sessionid` is admitted to that session's external queue after internal work. |
| `test191.scxml` | [test191.txml](https://www.w3.org/Voice/2013/scxml-irp/191/test191.txml) | A child's initial `#_parent` send reaches the invoking parent's external queue through a pre-reserved host endpoint. |
| `test192.scxml` | [test192.txml](https://www.w3.org/Voice/2013/scxml-irp/192/test192.txml) | A parent routes through its child's invoke-id alias and receives the child's `#_parent` reply. |
| `test347.scxml` | [test347.txml](https://www.w3.org/Voice/2013/scxml-irp/347/test347.txml) | Two independent sessions exchange three Events through host-owned external queues. |
| `test348.scxml` | [test348.txml](https://www.w3.org/Voice/2013/scxml-irp/348/test348.txml) | The sent event name becomes the exact receiving `_event.name`. |
| `test349.scxml` | [test349.txml](https://www.w3.org/Voice/2013/scxml-irp/349/test349.txml) | The receiving Event origin is the sender address and can route a reply. |
| `test350.scxml` | [test350.txml](https://www.w3.org/Voice/2013/scxml-irp/350/test350.txml) | The copied target selects the current published session endpoint for delivery. |
| `test351.scxml` | [test351.txml](https://www.w3.org/Voice/2013/scxml-irp/351/test351.txml) | An explicit send ID is copied to `_event.sendid`, while a send without `id` or `idlocation` leaves it empty. |
| `test352.scxml` | [test352.txml](https://www.w3.org/Voice/2013/scxml-irp/352/test352.txml) | The SCXML processor source type becomes the canonical receiving `_event.origintype`. |
| `test354.scxml` | [test354.txml](https://www.w3.org/Voice/2013/scxml-irp/354/test354.txml) | Structured CMeta Event data is copied across sender, host, and receiver ownership boundaries. |
| `test496.scxml` | [test496.txml](https://www.w3.org/Voice/2013/scxml-irp/496/test496.txml) | An inaccessible session target raises internal `error.communication`. |
| `test500.scxml` | [test500.txml](https://www.w3.org/Voice/2013/scxml-irp/500/test500.txml) | `_ioprocessors.scxml.location` exists and is nonempty without implying BasicHTTP support. |
| `test501.scxml` | [test501.txml](https://www.w3.org/Voice/2013/scxml-irp/501/test501.txml) | The startup SCXML location can be reserved as a send target and matches the public post-initialization session address. |
| `test495.scxml` | [test495.txml](https://www.w3.org/Voice/2013/scxml-irp/495/test495.txml) | The processor converts and admits a default send externally after a `#_internal` send has been selected internally. |
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
| `test403b.scxml` | [test403b.txml](https://www.w3.org/Voice/2013/scxml-irp/403/test403b.txml) | A transition inherited by two parallel leaves is selected once, and its lower source preempts the root ancestor. |
| `test403c.scxml` | [test403c.txml](https://www.w3.org/Voice/2013/scxml-irp/403/test403c.txml) | A conflicting descendant preempts its ancestor while targetless and wildcard transitions remain in the optimal set in document order. |
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
| `test422.scxml` | [test422.txml](https://www.w3.org/Voice/2013/scxml-irp/422/test422.txml) | After macrostep settlement, only invocations owned by the still-active ancestor and descendant start, in document order. |
| `test423.scxml` | [test423.txml](https://www.w3.org/Voice/2013/scxml-irp/423/test423.txml) | Internal work precedes external admission; one unmatched external Event is consumed before the later enabling Event is selected. |
| `test503.scxml` | [test503.txml](https://www.w3.org/Voice/2013/scxml-irp/503/test503.txml) | A targetless transition has an empty exit set. |
| `test504.scxml` | [test504.txml](https://www.w3.org/Voice/2013/scxml-irp/504/test504.txml) | An external transition exits every active proper descendant of the source/target LCCA. |
| `test505.scxml` | [test505.txml](https://www.w3.org/Voice/2013/scxml-irp/505/test505.txml) | An internal transition from a compound state to a proper descendant retains the source state. |
| `test506.scxml` | [test506.txml](https://www.w3.org/Voice/2013/scxml-irp/506/test506.txml) | An internal transition whose target is not a proper descendant uses external transition-domain semantics. |
| `test527.scxml` | [test527.txml](https://www.w3.org/Voice/2013/scxml-irp/527/test527.txml) | A `content/@expr` string value becomes the selected completion Event's `_event.data`. |
| `test528.scxml` | [test528.txml](https://www.w3.org/Voice/2013/scxml-irp/528/test528.txml) | A failed `content/@expr` queues `error.execution` before completion and leaves that completion Event's `_event.data` empty. |
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

Tests 403b and 403c are derived from the W3C 403 sources linked above. They
replace only the generator-specific `conf:` counter/pass/fail vocabulary with
the test-owned CMeta `sequence` field, finite ordered internal marker Events,
and the existing terminal `result.pass`/`result.fail` adapter probe. Test 403b
requires the parallel ancestor transition to set `sequence` once and rejects
both the root-ancestor action and any duplicate selected marker. Test 403c
requires the exact `wildcard.one`, `targetless.two`, `descendant.two`,
`wildcard.two` trace, so a dropped compatible transition, retained conflicting
transition, or reordered selected action reaches `fail` or cannot complete.
The same `event="*"` transition branches its executable content on
`_event.name`, so it must be selected independently for both source Events.
The only omitted mechanism is 403c's one-second generator liveness timeout;
the bounded local harness rejects non-terminal execution synchronously. These
fixtures retain the assertion witness; they do not certify the full W3C suite.

The behavioral comparison used qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16
(`test/w3c/txml/test403b.txml` and `test/w3c/txml/test403c.txml`) as read-only
reference material. No uSCXML source code or runtime dependency is included.

Tests 152, 153, 155, and 156 map the generated array to a test-owned bounded
CMeta `Vec<int>` containing `1`, `2`, and `3`. Test 152 admits only two
iterations, so the three-element collection fails before a body can run; its
second state uses the lexically valid but unresolved CMeta item location
`total.missing`. Both paths must select `error.execution`, preserve the zero
item/index/body values, and suppress their containing block's suffix Event.
Test 153 requires every assigned item
to exceed the preceding value and observes the final item/index pair `3`/`2`.
Test 155 replaces the generated sum helper with a three-stage witness: each
child can advance the stage only after observing its exact assigned item and
zero-based index. Test 156 makes the second item's child perform a legal
expression whose value cannot be represented by its CMeta integer destination.
Its error transition first checks transaction rollback, then queues a
confirmation Event behind any incorrectly executed third-item or containing-
block suffix sentinel. FIFO selection sends either sentinel to `fail` before
the confirmation can reach `pass`. Test 525 maps the upstream collection-
extension function to a strict host `send` effect that appends a fourth value
to the staged CMeta Vec during the first child body. The terminal witness
requires the loop to finish at the original third value and zero-based index
two; the host probe also requires the mutation effect to prepare and commit
exactly once. Tests 150 and 151 remain `UNSUPPORTED`
because CMeta variables are declared by the caller's static schema; test 152
remains `UNSUPPORTED` because illegal array and item locations are rejected at
document compilation.

Tests 198 and 200 replace the generator's pass/fail vocabulary with strict
result Events and omit the one-second timeout, which is only a liveness safety
net because the local harness fails synchronously if loopback delivery does not
complete. The bounded host accepts the first send only when its materialized
type is the canonical SCXML Event Processor URI. Test 198 additionally exposes
that same URI as the received Event's `origintype`, preserving the upstream
observable witness rather than checking only the outgoing request.

Tests 194, 199, and 521 use a strict bounded Event I/O host that validates the
exact outgoing target and processor type before returning the failure class
specified by the assertion. Their fixtures can reach `pass` only through the
corresponding internal `error.execution` or `error.communication` Event. Test
553 replaces the generator's invalid namelist with an initialization-time
`targetexpr` over the unbound `_event.name`. Its adapter accepts only the final
result Event, so any attempted `event1` delivery fails the test; an additional
same-block sentinel proves argument failure also aborts the remaining content.

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
422 replaces its three child interpreters with the versioned bounded invoke
adapter. The adapter observes committed starts for `invokeS1` and `invokeS12`
in document order, observes no start for the transient `invokeS11`, and returns
the two child Events through their public invocation tokens. A separate real
adapter trace in `scxml_event_io_contract_test` queues later external work and
requires both live invocation tickets to commit before that Event is selected.
Test 423 replaces the immediate/delayed sends with `start`, `externalEvent1`,
and `externalEvent2` admitted FIFO while a serial-executor gate is held. The
`start` transition raises `internalEvent` with both later external Events
already queued; only internal-first processing reaches `s1`, where unmatched
`externalEvent1` is consumed and `externalEvent2` reaches `pass`. Both fixtures
are finite and bounded; neither adds a child interpreter or timer fallback.
Test 504 replaces five counters with the two complete reverse-document exit traces
produced by its external transitions. Exact observers require both parallel
regions and their parallel parent to exit twice, and the containing state to
exit once.

Tests 527-529 retain the upstream `donedata/content` completion path and replace
generator predicates with exact CMeta Event guards. Test 527 maps the generated
quoted expression to `expr="&quot;foo&quot;"`; test 529 keeps the inline text child
`21`. Test 528 maps the invalid generated expression to a legal compiled CMeta
read from unavailable structured Event data. Its only passing path first consumes
`error.execution`, then requires the later `done.state.s0` Event data to equal
the empty string; an early completion reaches `fail`.

Test 488 applies the same runtime-only failure witness to
`donedata/param/@expr`. `_event.data.sequence` is a legal CMeta expression, but
no Event is bound while the nested final state's completion data is
materialized. The only passing path consumes `error.execution` before
`done.state.s0`, then requires that completion Event's `_event.data` to be the
empty string. This preserves the upstream error and empty-data observations
without changing the atomic typed-object ownership contract.

Test 298 uses `_event.data.sequence` through `donedata/param/@location` while
no Event is bound. The legal CMeta location expression therefore cannot supply
a value during completion materialization. The passing path first consumes the
resulting internal `error.execution`, then consumes `done.state.s0` only when
its `_event.data` is empty. The derived object is discarded atomically, so the
invalid name/value pair never becomes observable.

Test 286 maps the generated invalid location to the unknown CMeta field
`missing`. Its following `foo` raise and FIFO confirmation Event prove that
the assignment places `error.execution` on the internal queue and aborts the
remaining executable-content block. Test 287 maps the generated data ID to the
CMeta integer `sequence`, assigns
the literal value `1`, and observes that committed value from the following
eventless guard. Test 487 keeps a valid integer location but maps the generated
illegal value to `1e100`: this is a finite floating expression accepted by the
numeric assignment compiler, while exact integer conversion fails only when
the assignment executes. Its following `foo` raise and a FIFO confirmation
Event distinguish block abortion from merely queuing `error.execution`.

Test 311 maps its invalid location expression to `sequence.missing`. The path
is lexically valid but traverses the scalar CMeta `sequence` field, so it can
never yield a writable location and its only passing transition consumes the
resulting internal `error.execution`. Test 307 remains `UNSUPPORTED`: the
static CMeta schema does not expose the loaded-instance missing-substructure
state needed to compare pre-late-binding and post-load access behavior.

Tests 309 and 344 map the generated non-Boolean predicate to the typed CMeta
integer `sequence`. Test 309 can reach pass only when the invalid condition is
treated as false. Test 344 enters a fallback state whose `onentry` raises
`foo`; its wildcard transition reaches fail unless the guard's
`error.execution` was queued first during transition selection.

Tests 312 and 314 map the generated illegal value expression to the legal
CMeta expression `_event.data.sequence` while `_event` is unbound. Compilation
therefore succeeds, but evaluation fails at the same executable-content point
as the upstream assertion. Each fixture keeps the following `raise event="foo"`
as a block-abort sentinel, then queues a confirmation Event after selecting
`error.execution`; FIFO ordering exposes any incorrectly retained `foo` before
the fixture can pass. Test 314 advances through `s01` and `s02` with internal
Events, while the compound parent's `error.execution` transition rejects any
premature evaluation before `s03`. Test 313 instead selects the alternative
explicitly permitted by its upstream manual assertion: the incomplete CMeta
expression `1 +` is rejected during document compilation with
`SCXML_INVALID_STRUCTURE`, before any executable content can run. This does not
claim recoverable transition-guard failures.

Tests 172-175 preserve the upstream mutation-before-send witness with the
owned CMeta `send_id` and integer `sequence` fields. Tests 172 and 174 use a
bounded committed loopback, test 173 selects the native `#_internal` path, and
test 175 inspects both materialized delay values before admitting event1 then
event2. Thus the adapter observes only transactionally committed effects; it
does not become a second expression or state fact source. Test 183 uses the
same committed loopback but permits its event1 transition only after the
generated ID has been published to the owned `send_id` location.

Tests 176 and 205 use the single typed payload boundary to require the exact
event name, payload name, scalar kind, and value evaluated by the inline block.
Test 178 additionally requires both duplicate names in document order, proving
that the callback-scoped payload remains an ordered entry sequence rather than
a collapsing map. Test 187 replaces the invoked child with one owning session.
That session commits a delayed parent request, reaches its top-level final, and
is then destroyed; the strict host's mandatory close path cancels its pending
message and rejects a later delivery attempt. This follows the documented
host-owned timer and session-owned lifecycle boundary without adding a second
timer registry to TurboSCXML.

Test 179 replaces upstream self-delivery with the single content-aware host Event I/O
adapter. The fixture still evaluates literal `content` when `send` executes;
the adapter is the external-service boundary and requires the exact UTF-8 bytes
`123` before the only terminal path can complete. Test 185 replaces wall-clock
delivery with ordered public admission: the host requires the delayed request
to carry 1000 ms, admits the zero-delay Event first, and admits the delayed
Event only after the session reaches its waiting state. Test 186 mutates the
source CMeta field after a delayed send; the typed host must already own scalar
payload 1 while the eventless terminal guard observes the new value 2. Tests
208 and 210 retain two live delayed-send identities in the session registry.
The strict host requires the cancellation to name the first identity, rejects
completion reporting for that cancelled identity, and admits only the second
Event. Test 210 obtains the first identity from `idlocation`, so its exact host
comparison also witnesses execution-time `sendidexpr` evaluation.

Tests 223 and 224 replace the
invoked child processor with the versioned host invoke adapter. The adapter
observes the committed generated ID and reports completion through that same
invocation token. The fixtures independently require the writable CMeta
`idlocation` to be nonempty and exactly `s0.1`, so removing the generated child
does not weaken either binding or `stateid.platformid` witness.

Tests 215, 216, 220, 225, 226, 530, and 554 use one bounded invoke host.
Tests 215 and 216 overwrite their initial CMeta strings in `onentry`; only the
new type/source is accepted. Test 220 requires the canonical SCXML type before
the host reports completion. Test 225 requires two committed requests to carry
different session tokens and generated IDs, while the fixture independently
compares both `idlocation` values. Test 226 requires the exact type, source, and
named scalar parameter before the host returns `varBound`. Test 530 changes its
content operand from 1 to 7 during entry and requires 7 at the adapter. Test 554
uses a legal expression whose read from the unbound startup Event fails at
execution; `error.execution` reaches `pass` while the host observes zero starts.
These transformations exercise the public materialization/report boundary and
do not claim that the core itself implements file loading or a child interpreter.

Tests 239-245 use a dedicated bounded child-materialization host. Every accepted
start request is copied into one of two fixed ticket rows before the callback
returns. The host resolves only `test239-child.scxml`, `test240-child.scxml`,
`test242-child.scxml`, and `test245-child.scxml`, or compiles the copied inline
XML directly; arbitrary paths and URLs are not accepted. Each child receives a
fresh CMeta state. Only an exact integer `child_value` entry is mapped into that
closed schema; unknown names are counted and ignored. Tests 239 and 242 prove
actual execution of `src` and inline markup through distinct/equivalent child
Events. Tests 240, 241, 243, and 244 prove namelist/param injection, while test
245 proves that an unmatched key cannot extend or mutate the child model. Child
programs, sessions, executors, source resolution, and Event relay remain owned
by the host; no production file loader or recursive child interpreter is added.

Tests 228, 232, 235, 236, and 247 use one strict bounded invoke-completion host.
Every case requires exactly one nonzero committed start token and exact start,
cancel, result-effect, `returned_accepted`, `returned_rejected`, `completed`, and
`active` counts. Test 228 reports completion through the committed token and
compares that completion Event's exact `_event.invokeid`. Test 232 reports two
normal Events and completion in order; the parent's three-state sequence proves
their actual observation order is FIFO. Test 235 requires `_event.name` to equal
`done.invoke.foo`, with a same-event fallback to `fail`. Test 236 admits a normal
Event before completion, waits for token terminalization, requires a late report to return
`INVALID_ARGUMENT`, and uses an independent `confirm` Event to prove the rejected
return never reached selection. Test 247 creates a second actual TurboSCXML
session from `test247-child.scxml`, observes that session's top-level-final
`done` state, destroys it cleanly, and only then reports one completion through
the parent token. The child remains host-owned; production code gains no child
interpreter, cross-session registry, transport, or thread.

Tests 237, 250, and 252 use a second real host-owned TurboSCXML session that remains
active until the parent leaves its invoking state. The committed cancel ticket
calls `scxml_session_cancel()` on that child without waiting or recursively
pumping either executor. Test 237 requires subsequent child admission to return
`CANCELLED` and stale completion reporting to return `INVALID_ARGUMENT`. Test
250 keeps nested `sub0/sub01` active, requests CFlow controlled exit, and
captures the real child `tlog` records in `sub01`, then `sub0` order while the
child terminates cancelled rather than normally complete. Test
252 attempts both a normal returned Event and completion after cancellation;
both are rejected before parent selection, and only an independent timeout can
reach pass.

Test 253 uses the same host-owned child boundary but keeps a live canonical
SCXML invocation in the parent. The host commits that exact start before
activating `test253-child.scxml`, maps child `#_parent` to the parent endpoint
and parent `#_foo` to the child endpoint, then pumps exactly three committed
external Events. Both CMeta sessions reject the route unless the received
`_event.origintype` is the W3C-permitted `scxml` processor short name. The
separate child fixture isolates the inline-markup interpretation tracked by
test 239; it does not add a child interpreter or file loader to production.

Tests 229 and 230 replace the invoked child implementation with one bounded
autoforward host. Test 229 reports `childToParent` through the committed child
token, copies the prepared autoforward Event, and makes `eventReceived`
deliverable only when that ticket commits; a host pump reports the response
after executor idle. Test 230 admits distinct values for every standard Event
field and compares a callback-owned copy of the versioned envelope before the
same committed host pump reports `fieldsEqual`. The targetless input
transitions only register these finite Event names and retain the waiting
configuration. No adapter callback admits an Event or recursively advances the
session.

Tests 233 and 234 use a bounded two-token-aware invoke host. Test 233 reports
`childToParent` through its sole committed token and can pass only when that
invocation's `finalize` assignment is visible to selection of the same Event.
Test 234 starts two live invocations, reports through the first token, and uses
distinct assignment results so running neither handler, the second handler, or
both handlers cannot pass. The CFlow V4 host transaction stages the CMeta write,
completion bookkeeping, and autoforward tickets together; rollback discards all
of them. Session destruction then verifies cancellation cleanup for every
invocation that remains active.

Tests 279 and 550 retain the upstream early-binding witness: each declaration
belongs to a state that is never entered, while the initial state's guard reads
the initialized CMeta field. Test 279 exercises the default binding and test 550
spells out `binding="early"` while requiring the exact `expr` result.

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
exposed as a bounded string. `scxml_session_try_send_with_metadata()` additionally
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
root CMeta object, then admit that object through the metadata API. This keeps codecs
and their errors out of transition selection. Invalid envelopes, unsupported
content, schema mismatches, bare/unknown `_event` paths, and every write to an
`_event` location fail fast without a compatibility fallback.

## Event I/O conformance boundary

The module deliberately retains a conforming-host adapter boundary instead of
bundling a cross-session registry or transport. The public location-copy API
lets a host register the exact address exposed through
`_ioprocessors.scxml.location`, and
`scxml_event_io_contract_test` demonstrates bounded routing and delivery
through only public APIs. Tests 189-192, 347-354, 495-496, and 500-501 therefore
exercise the logical SCXML Event Processor contract with that strict local host.
This evidence does not make the library alone a standalone network service and
does not claim the optional BasicHTTP Event Processor profile.

The upstream suite page offers the tests under the
[W3C Test Suite License](https://www.w3.org/copyright/test-suite/) or the
[W3C 3-clause BSD License](https://www.w3.org/Consortium/Legal/2015/copyright-software-and-document).
Keep this provenance and transformation record with any copied or extended
fixture set.

Copyright © 2013 World Wide Web Consortium. W3C liability, trademark, and
document-use rules are governed by the selected license above.
