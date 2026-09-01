# SCXML Macrostep Invocation Scheduling Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute this plan.

**Goal:** Prove and, only if necessary, minimally correct TurboSCXML's ordering for W3C mandatory tests 422 and 423: newly entered live invocations start after macrostep internal work settles, and external events are removed until one enables a non-empty transition set.

**Architecture:** CFlow remains the sole owner of active configuration, macrostep settlement, internal-before-external priority, and external queue removal. TurboSCXML remains the owner of SCXML invocation descriptors, session-owned invocation rows, adapter transactions, and W3C lowering. CFlow V4 `on_host_transaction` at `PREPARE_QUIESCENCE` is the synchronous boundary. Do not add a second scheduler, a process-global session registry, a child interpreter, or host callbacks under a session lock.

**Characterization gate:** Current code invokes `scxml_runtime_start_pending_invocations()` from CFlow's V4 quiescence transaction before macrostep settlement and the next external admission. Add real adapter-order and public-path regressions first. If they are GREEN without a production change, preserve the existing runtime and treat this as conformance characterization plus corpus promotion. Never manufacture a runtime diff merely to satisfy TDD; the required RED may be the registered harness cases failing because faithful fixtures are absent. If a direct adapter-order regression is RED, change only the owning layer exposed by that failure.

**State and failure contract:** One `scxml_session_impl` owns invocation rows and one CFlow instance owns the active configuration and queues. Start requests are prepared outside session locks, staged transactionally, and become visible only on commit. Preparation/evaluation/staging failure must discard owned adapter work, retain the first useful error, and publish no partial invocation. External events remain FIFO; unmatched external events are consumed, while internal work always settles before the next external event is considered.

**Compatibility:** No public API, adapter ABI, package target, data format, or dependency direction change is expected. If implementation evidence requires any such change, stop this task as a plan defect rather than widening it silently.

**Reference evidence:** W3C SCXML 1.0 interpretation algorithm; official IRP `test422.txml` and `test423.txml`; read-only uSCXML baseline `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16`, specifically the matching `.txml` files and scheduling-relevant interpreter/invoker regions. No reference implementation code is copied.

## Global Constraints

- The dependency direction remains `TurboSCXML -> installed TurboUtils`; TurboUtils must not depend on TurboSCXML or uSCXML.
- One owning `scxml_session` remains the sole mutable session-state owner.
- External effects cross bounded, versioned prepare/commit/discard adapters.
- Cross-session routing, timers after adapter commit, transport, authorization, HTTP, QuickJS, persistence, and deployment remain host responsibilities.
- Unsupported semantics fail admission or execution explicitly; there is no uSCXML fallback interpreter.
- `tests/w3c/manifest.tsv` remains the conformance fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A row changes to `PASS` only after its local fixture executes deterministically through the public TurboSCXML path.
- Preserve bounded allocation, checked arithmetic, deterministic ordering, transactional publication, and explicit ownership in every change.
- Never add `C:/projects/cpp/uscxml` to CMake, tests, CI, installed package metadata, or runtime search paths.

---

### Task 1: Prove macrostep invocation and external-event scheduling

**Files:**
- Create: `docs/specs/scxml-macrostep-invocation-scheduling-design.md`
- Modify only if RED proves necessary: `src/scxml_runtime.c`, `src/scxml_session.c`, `src/scxml_impl.h`
- Test: `tests/scxml_event_io_contract_test.c`
- Test: `tests/scxml_w3c_conformance_test.c`
- Create: `tests/w3c/test422.scxml`
- Create: `tests/w3c/test423.scxml`
- Modify after GREEN: `tests/w3c/manifest.tsv`, `tests/w3c/README.md`

**Interfaces:**
- Consumes: CFlow macrostep settlement, internal/external queues, V4 `on_host_transaction`, session invocation rows, and versioned invoke/Event I/O adapters.
- Produces: a deterministic trace in which eventless/internal work settles, live newly entered invocations start in document order, their effects commit, unmatched external events are consumed, and the first enabling external event starts the next macrostep.

- [ ] Write the focused design first. It must document state ownership, the exact scheduling sequence, lock/callback boundaries, bounded capacity, prepare/commit/discard rollback, close/cancel behavior, compatibility, and a rollback path for any production correction.
- [ ] Add named W3C harness registrations before fixtures and preserve the missing-fixture RED output.
- [ ] Add a strict adapter-order characterization using real TurboSCXML/CFlow execution. It must reject starting an invocation for a state entered and exited within the same macrostep, require live invocations in document order, and distinguish invocation commit from later external-event selection. If existing behavior passes, record it as characterization rather than weakening the test.
- [ ] Add faithful bounded null/CMeta transformations of tests 422 and 423. Test 422 must observe starts for the active ancestor and final active descendant but not the transient state. Test 423 must prove internal-before-external priority, removal of one unmatched external event, and selection of the later enabling external event.
- [ ] If and only if a direct regression is RED, implement the smallest correction in its owning layer. Keep callbacks outside locks and publish invocation lifecycle changes only transactionally.
- [ ] Promote only rows 422 and 423 to `PASS/TERMINAL_PASS`, update prose accounting from 116/52 to 118/50, and retain the non-certification statement.
- [ ] Run the focused adapter and W3C filters, the complete W3C executable, full Release build/CTest, manifest accounting, CodeGraph sync/affected, and `git diff --check`.
- [ ] Commit the design, tests, any necessary production correction, fixtures, manifest, and README as a reviewable task with complete RED/GREEN evidence.
