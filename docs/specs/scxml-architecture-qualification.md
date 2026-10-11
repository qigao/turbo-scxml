# Architecture qualification matrix

This document maps the canonical architecture rules to executable regression
witnesses. It is not a substitute for those tests; it is an index for them.

## ActionPlan / FunctionDesc

| Boundary | Executable witness |
| --- | --- |
| admitted FunctionDesc actions execute from immutable ActionPlan state | `scxml_cmeta_test`: "executes admitted binary FunctionDesc actions from measured session scratch" |
| current CMeta VALUE callable universe is enforced before Program publication | `scxml_cmeta_test`: "rejects FunctionDesc arity outside the current CMeta VALUE callable universe before publication" |
| semantic / ABI / callable mismatch rejects before publication | `scxml_cmeta_test`: "rejects FunctionDesc-first semantic or ABI mismatch before publication" |
| static guard against reintroducing selected control-plane lookups | `scxml_architecture_runtime_contract` scans `scxml_runtime.c`; this is a source-layout guard, not executable proof of runtime lookup behavior |
| direct CFlow execution and owning Session execution use the same compiled action | the binary FunctionDesc test executes both `run_to_idle()` and `run_direct_to_idle()` |

The current CMeta VALUE callable policy is unary/binary. TurboSCXML does not
invent a wider private callable ABI.

## Plugin Function lifetime

| Boundary | Executable witness |
| --- | --- |
| Plugin Function uses the ordinary FunctionDesc ActionPlan compiler | `scxml_plugin_test`: "holds the plugin lease through Program execution and releases it after Program destruction" |
| live Program borrow keeps DSO lease active | same test checks `active_leases == 1` and unload returns BUSY |
| stop / quiescence / unload complete only after Program destruction | same test releases Program then proves quiescence and unload |
| incompatible export releases temporary lease | `scxml_plugin_test`: "releases its lease when export contract admission fails" |
| stale ref/generation fails closed | `scxml_plugin_test`: "fails closed when a Plugin ref became stale before compilation" |

## Provider interfaces

| Boundary | Executable witness |
| --- | --- |
| static Event I/O adapter ↔ CMeta Interface | `scxml_provider_test`: "projects a static Event I/O adapter through CMeta Interface and back" |
| static Invoke adapter ↔ CMeta Interface | `scxml_provider_test`: "projects a static Invoke adapter through CMeta Interface and back" |
| capability contradictions reject before publication | `scxml_provider_test`: "rejects capability contradictions before bridge publication" |
| Plugin Event I/O Interface uses the same session-facing adapter with a retained lease | `scxml_plugin_test`: "bridges a Plugin Event I/O Interface export with a lease-safe adapter" |
| Plugin Invoke Interface and capability mismatch fail closed | `scxml_plugin_test`: "bridges a Plugin Invoke Interface export and rejects capability mismatch" |
| raw DataBind and text resources reuse the same CMeta provider model | `scxml_provider_test` resource bridge cases |

Session ticket commit/discard, adapter close, quiescence and queue semantics stay
owned by the existing adapter/session implementation. The CMeta Interface layer
only describes and adapts the provider contract.

## Ownership and failure-atomic cleanup

Three separate owners govern cleanup. Native CMeta value lifecycle balances
COPY/MOVE/DESTROY for constructed values. Plugin leases retain module code and
data. SCXML Session and provider protocols govern asynchronous completion,
close and quiescence. Releasing a native value does not settle an outstanding
request, and observing a Session terminal state does not authorize DSO unload.

| Boundary | Executable witness and assertions |
| --- | --- |
| provider wrappers retain leases outside a borrowed Session | [`scxml_plugin_test.c`](../../tests/scxml_plugin_test.c): "keeps Plugin provider leases outside a live SCXML Session" attaches both wrappers, observes two leases and BUSY unload after stop, destroys the Session first, then releases wrappers with lease counts 2 → 1 → 0 before successful quiescence/unload |
| both attached providers must quiesce before Session storage is released | [`scxml_test.c`](../../tests/scxml_test.c): "closes combined Event I/O and Invoke providers exactly once across destroy retry" checks repeated WOULD_BLOCK, a live Session handle, one close per provider, and final success only after both providers report quiescence |
| failed initialization releases staged managed roots without publishing a Session | [`scxml_cmeta_test.c`](../../tests/scxml_cmeta_test.c): "balances managed CMeta root lifecycle when Session init fails after staging" forces top-level data initialization, reaches the initial send adapter once, rejects its invalid ticket with ACTION_FAILED, leaves `session.impl == NULL`, and checks nonzero COPY+MOVE equals DESTROY |

These are the executable witnesses requested by
[PR #289](https://github.com/qigao/turbo-scxml/pull/289),
[PR #291](https://github.com/qigao/turbo-scxml/pull/291), and
[PR #293](https://github.com/qigao/turbo-scxml/pull/293), indexed for
[issue #294](https://github.com/qigao/turbo-scxml/issues/294).
They reuse canonical lifecycle callbacks and existing owners; they introduce
no cleanup registry or lexical substitute for asynchronous teardown.

The lease fixture uses an initially final Session and proves attachment and
release ordering, not an in-flight DSO callback. The combined-close fixture
uses controlled quiescence probes. The rollback fixture proves balanced
lifecycle accounting for the startup-action failure path, not every allocation
failure. Component/Actor concurrency evidence remains separately described in
the [VoiceXML architecture](voicexml-architecture-design.md#independent-host-active-object).

## XmlParser / DataBind

| Boundary | Executable witness |
| --- | --- |
| malformed language XML fails through the document parser | `scxml_test`: "leaves output empty when XML syntax or configured limits fail" |
| external JSON data uses DataBind | `scxml_cmeta_test`: "loads early data src from raw JSON through DataBind" |
| malformed external XML is routed through DataBind rather than SCXML parsing | `scxml_cmeta_test`: "routes malformed external XML through DataBind without reparsing the SCXML document" |
| unsupported format fails explicitly with resource context | `scxml_cmeta_test`: "retains resource context for an unsupported DataBind format" |
| compile/runtime DataBind limits fail before unsafe I/O/publication | the external-plan and runtime-budget tests adjacent to the raw resource witnesses |

See also `scxml-xml-databind-boundary.md`.

## CFlow Statechart ownership

TurboSCXML publishes its compiled `cflow_statechart` and executable/guard
bindings rather than maintaining a second machine kernel.

Executable evidence already exists throughout `scxml_test` and
`scxml_cmeta_test`, where tests initialize `cflow_statechart_instance`
directly from `scxml_program_statechart()`. The FunctionDesc ActionPlan
qualification also executes one Program both through an owning SCXML Session
and through direct CFlow bindings.

The CMake architecture contract additionally rejects XML/CSerde/SCXML
dependencies leaking into `Salts::CFlow`.

## Package / CI qualification

[`Native SDK CI`](../../.github/workflows/native-sdk-ci.yml) qualifies the
released SDK consumer graph, including the ownership witnesses above.

It covers:

- Linux release;
- Linux ASan + UBSan;
- Windows release;
- macOS release;
- Android build/install consumer qualification;
- installed C and C++ consumers;
- Plugin-enabled `TurboSCXML::Plugin`;
- Plugin-disabled core/package configure, build, tests, install and consumer
  verification on Linux;
- public `provider.h` C and C++ install consumers.

[`Component runtime conformance`](../../.github/workflows/component-runtime-conformance.yml)
separately covers Component/Host/CNet integration, real DSO generations,
installed consumers and selected sanitizer gates.
[`Component upstream source TSan`](../../.github/workflows/component-upstream-source-tsan.yml)
instruments the first-party dependency graph and runs its selected concurrency
tests. Neither selected suite replaces the native ownership or language tests.

CI records coordinated release/source identities for reproducibility; public
consumer package discovery does not impose a new `EXACT` version requirement.
Android evidence is build/install-consumer qualification, not device execution.
Workflow configuration describes intended coverage; actual pass/fail claims
must identify the tested commit and completed run.

## Mainline workstream evidence

This ledger reconciles the implementation with the open tracking issues in
[ACE epic #297](https://github.com/qigao/turbo-scxml/issues/297). It records
delivered code and bounded qualification, not issue closure or stable release
admission. Historical draft-branch restrictions and earlier SDK failures in
those issues describe their original snapshots.

| Workstream | Mainline executable evidence | Scope / remaining boundary |
| --- | --- | --- |
| [#295](https://github.com/qigao/turbo-scxml/issues/295) Component selection and scopes | [`scxml_component_test.c`](../../tests/scxml_component_test.c), [`scxml_component_dso_test.c`](../../tests/scxml_component_dso_test.c) | Static and real DSO provider admission, borrowed scope lifetime and generation retirement; no live interpreter-state migration |
| [#287](https://github.com/qigao/turbo-scxml/issues/287), [#292](https://github.com/qigao/turbo-scxml/issues/292), [#294](https://github.com/qigao/turbo-scxml/issues/294) ownership | [Ownership witnesses above](#ownership-and-failure-atomic-cleanup) | Provider leases, combined close/quiescence and managed-root startup rollback; not exhaustive allocation-failure injection |
| [#298](https://github.com/qigao/turbo-scxml/issues/298) Host routing | [`scxml_host_router_test.c`](../../tests/scxml_host_router_test.c), [`scxml_host_event_io_test.c`](../../tests/scxml_host_event_io_test.c), [`scxml_host_delay_test.c`](../../tests/scxml_host_delay_test.c) | Two Sessions, distinct Host/FIFO FULL, transaction tickets, slot generations and delayed-send cancellation; each Session retains one Statechart owner |
| [#299](https://github.com/qigao/turbo-scxml/issues/299) optional CNet bridge | [`scxml_cnet_ingress_test.c`](../../tests/scxml_cnet_ingress_test.c), [`scxml_cnet_egress_test.c`](../../tests/scxml_cnet_egress_test.c), [`scxml_cnet_frame_ingress_test.c`](../../tests/scxml_cnet_frame_ingress_test.c) | Real loopback transport, bounded ownership and explicit owner progress; experimental framing is not a complete SCXML Event wire codec |
| [#300](https://github.com/qigao/turbo-scxml/issues/300) ACT / Invoke terminal | [`scxml_component_cnet_invoke_test.c`](../../tests/scxml_component_cnet_invoke_test.c), [`scxml_cnet_request_generation_test.c`](../../tests/scxml_cnet_request_generation_test.c), [`scxml_host_delay_test.c`](../../tests/scxml_host_delay_test.c) | Actual DSO Invoke, NativeIO ACT, stale request generations and controlled cancel/terminal/slot-reuse overlap; not every possible concurrent interleaving |
| [#301](https://github.com/qigao/turbo-scxml/issues/301) domain fencing | [`scxml_cnet_domain_fence_test.c`](../../tests/scxml_cnet_domain_fence_test.c), [`scxml_component_cnet_invoke_test.c`](../../tests/scxml_component_cnet_invoke_test.c) | One real listener, generation fencing, failed activation, native drain and restart; domain ownership stays distinct from code generation lifetime |
| [#302](https://github.com/qigao/turbo-scxml/issues/302) Interceptor / Extension Interface | [`scxml_component_test.c`](../../tests/scxml_component_test.c), [`scxml_component_dso_test.c`](../../tests/scxml_component_dso_test.c), [`scxml_component_interceptor_benchmark.c`](../../tests/scxml_component_interceptor_benchmark.c) | Exact static/DSO admission, rejection cleanup and Session effect journal; benchmark output is observational, not a speedup guarantee |
| [#303](https://github.com/qigao/turbo-scxml/issues/303) VoiceXML / CCXML and independent Host Actor | [`voicexml_component_dialog_test.c`](../../tests/voicexml_component_dialog_test.c), [`ccxml_session_test.c`](../../tests/ccxml_session_test.c) | Real controllers and DialogManager, DSO publication, rollback, terminal Event backpressure, bounded Actor FULL/STALE and teardown; fixture composition does not expose an installed Actor facade or qualify concurrent dispatch into one Session |
| [#51](https://github.com/qigao/turbo-scxml/issues/51), [#276](https://github.com/qigao/turbo-scxml/issues/276), [#286](https://github.com/qigao/turbo-scxml/issues/286) VoiceXML hardening | [Support matrix](voicexml-support-matrix.md), [bounded fuzz targets](../../tests/fuzz/README.md), [security policy](voicexml-security-policy.md), Native SDK profile tests | All four scoped smoke targets are present; local evidence and explicit profile limits do not imply full VoiceXML conformance |

The distinct Statechart Actor adoption study
[#304](https://github.com/qigao/turbo-scxml/issues/304) remains deferred. It
requires comparable throughput, latency, memory and shutdown measurements plus
a single-instance ownership design before a GO/NO-GO decision. The current
implementation deliberately keeps the existing Session owner; a test-local
independent Host Actor is not evidence for replacing that owner.

### Completed qualification snapshot

At code [`ef370be985949deb22cbcd198f770747042fba8e`](https://github.com/qigao/turbo-scxml/commit/ef370be985949deb22cbcd198f770747042fba8e),
all three workflows completed successfully:

- [Native SDK CI #38107251325](https://github.com/qigao/turbo-scxml/actions/runs/38107251325):
  Linux, Windows and macOS release, Linux ASan+UBSan, Android arm64
  build/install-consumer and the configured installed/profile checks.
- [Component conformance #38107251320](https://github.com/qigao/turbo-scxml/actions/runs/38107251320):
  selected Linux/Windows IOCP/macOS Kqueue runtime and DSO tests, installed
  consumers, W3C SCXML gate and selected sanitizer tests.
- [First-party source TSan #38107251414](https://github.com/qigao/turbo-scxml/actions/runs/38107251414):
  selected concurrency tests against instrumented Salts, SaltsUtils and
  TurboSCXML source, rather than only instrumented consumer objects.

The coordinated graph uses Salts `2.3.0-rc.10` at
`6ba9868a965f3273cf87ab243dab1884072eb7a3`, SaltsUtils `4.3.0-rc.8` at
`3d39b8c23403bad5fc3e04241bb74a7889bd8a03`, and CHTTP `2.1.0-rc.3` in the
CHTTP-enabled native profiles. The source-TSan lane validates the published
Salts/SaltsUtils manifest identities before instrumenting those sources.
This is a prerelease host qualification snapshot, not a stable SDK release,
all-downstream acceptance, Android/iOS device execution, or exhaustive race
proof. Later changes need their own completed run evidence.
