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
