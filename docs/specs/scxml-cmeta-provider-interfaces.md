# CMeta provider interfaces

TurboSCXML keeps its existing bounded adapter ABIs as the session-facing
execution contract, but publishes canonical provider identity through CMeta
Interface.

## Why Interface, not FunctionDesc

Event I/O, invocation and resource callbacks are stateful provider methods.
They carry borrowed request pointers, output tickets/resources and error
pointers. They are not ordinary CMeta VALUE callables and must not be assigned
fake scalar FunctionDesc semantics.

CMeta Interface already models this exact shape:

```text
provider
  = { self, vtable }
  + implementation identity
  + capability bits
  + method names / dispatch arity
```

TurboSCXML therefore uses CMeta legacy exact-ABI interface rows for these
pointer-rich callbacks. FunctionDesc/FunctionAbi remain reserved for methods
whose semantic/native callable contract is actually representable.

## Canonical providers

```mermaid
flowchart LR
    STATIC["existing static adapter"]
    CMETA["CMeta Interface<br/>self + vtable + capabilities"]
    PLUGIN["Salts Plugin<br/>Interface export + lease"]
    BRIDGE["TurboSCXML adapter bridge"]
    SESSION["existing SCXML Session ABI"]

    STATIC -->|adapter → provider| CMETA
    PLUGIN -->|require_interface| CMETA
    CMETA -->|provider → adapter| BRIDGE
    BRIDGE --> SESSION
```

Current interfaces:

- `scxml_event_io_provider`
- `scxml_invoke_provider`
- `scxml_data_resource_provider` for canonical raw DataBind resources
- `scxml_text_resource_provider` for compile-time text resources

Future VoiceXML prompt/collect/telephony providers should reuse this pattern
rather than create another provider registry or dynamic-module ABI.

## Capability and authority rules

Reflection is descriptive, not authority-granting.

- Event I/O capability dependencies remain identical to
  `scxml_event_io_adapter`.
- Invoke capability dependencies remain identical to
  `scxml_invoke_adapter`.
- Session initialization still checks the Program requirements against the
  final adapter capabilities.
- Effect-ticket ownership, commit/discard, close and quiescence semantics are
  unchanged.
- Resource open/close lifetime and byte budgets are unchanged.
- No hidden worker, queue, timer, transport or event loop is introduced.

## Plugin lifetime

`TurboSCXML::Plugin` admits Interface exports with
`cmeta_plugin_export_require_interface()`, comparing the plugin descriptor to
the canonical TurboSCXML descriptor by semantic interface shape.

The resulting provider wrapper retains one Plugin lease:

```text
STARTED plugin
    |
    +-- acquire lease
    |
    +-- Interface export
    |      |
    |      +-- provider → existing adapter
    |                    |
    |                    +-- SCXML session borrows adapter/user
    |
    +-- caller destroys session
    |
    +-- provider wrapper destroy
           |
           +-- release lease
```

A provider wrapper must outlive every session borrowing its adapter/user.
Destroying the wrapper while a session is attached is caller misuse. Copying a
CMeta Interface handle or adapter does not make the plugin DSO unload-safe.

The bridge does not load, start, stop or unload plugins. Registry lifecycle
policy remains owned by the application.

## Scoped typed Interceptor qualification

The Component integration tests use CMeta's `CMETA_INTERCEPTOR_TYPE` at the
native `prepare_send` boundary. The chain and adapter in
[`scxml_component_interceptor_probe.h`](../../tests/scxml_component_interceptor_probe.h)
are test fixtures, not an installed TurboSCXML Interceptor API. Their explicit
FunctionAbi describes the typed interception target; it does not replace the
existing provider Interface or infer a generic callable from its vtable.

The chain borrows its provider, hooks and contexts. The containing Component
generation scope pins the DSO; interception does not acquire another lease.
Hooks run forward before the target and backward after success or on error.
A rejected call transfers no effect ticket. If rejection happens after the
provider prepared a ticket, the target discards that ticket before returning.
Only an accepted ticket transfers to the Session's CFlow effect journal for
commit or rollback. Policy mutation in these fixtures occurs only while the
Session executor is idle.

[`scxml_component_dso_test.c`](../../tests/scxml_component_dso_test.c) checks
these contracts using actual loaded Provider DSOs and Sessions:

- A rejected hook raises `error.execution` without entering the provider.
- A post-provider rejection discards exactly once, stops the remaining sends
  in that executable block, and reaches the error recovery state. A later
  explicit event succeeds through the same binding and commits once.
- A journal rollback discards an accepted ticket after internal queue overflow.
- Publishing a new generation leaves each live Session's interceptor bound to
  its original provider. The old scope prevents unload until Session/provider
  retirement; the new Session still sends through its own interceptor after
  the old DSO is unloaded.

Run the existing Component-enabled test graph through the user presets; on a
Windows VS developer environment with matching SDK roots:

```powershell
cmake --preset win-release-user -DTURBOSCXML_ENABLE_COMPONENT=ON
cmake --build --preset win-release-user --target scxml_component_dso_test scxml_component_test scxml_provider_test scxml_plugin_test
ctest --preset win-release-user --output-on-failure -R "^scxml_(component_dso|component|provider|plugin)_test$"
```

These cases qualify synchronous `prepare_send` composition. They do not prove
concurrent mutation of a chain, arbitrary callback interception or concurrent
DSO unload without an external scope. Broader acceptance is tracked in
[issue #302](https://github.com/qigao/turbo-scxml/issues/302).

## Static compatibility

Existing static adapters require no rewrite. They can continue to be passed
directly to session APIs. When reflection/composition is needed, the
adapter-to-provider bridge copies the adapter ops and retains the original user
pointer as a borrow.

Conversely, provider-to-adapter bridges let a CMeta Interface implementation,
including a Plugin Interface export, use the same existing session APIs.

This preserves one execution ABI and one reflection model without creating a
second runtime.
