# Architecture qualification matrix

This document maps the canonical architecture rules to executable regression
witnesses. It is not a substitute for those tests; it is an index for them.

## ActionPlan / FunctionDesc

| Boundary | Executable witness |
| --- | --- |
| admitted FunctionDesc actions execute from immutable ActionPlan state | `scxml_cmeta_test`: "executes admitted binary FunctionDesc actions from measured session scratch" |
| current CMeta VALUE callable universe is enforced before Program publication | `scxml_cmeta_test`: "rejects FunctionDesc arity outside the current CMeta VALUE callable universe before publication" |
| semantic / ABI / callable mismatch rejects before publication | `scxml_cmeta_test`: "rejects FunctionDesc-first semantic or ABI mismatch before publication" |
| runtime does not repeat FunctionDesc/name/signature admission | `scxml_architecture_runtime_contract` scans the runtime boundary and fails if control-plane reflection APIs return to `scxml_runtime.c` |
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

The existing `Native SDK CI` remains the single architecture qualification
workflow.

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

No architecture slice adds a dependency version pin or `EXACT` requirement.
