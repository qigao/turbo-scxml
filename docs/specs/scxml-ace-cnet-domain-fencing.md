# ACE 2.3 CNet domain-owned exclusive resource fencing

Tracks TurboSCXML #297 and #301. Implementation: optional `TurboSCXML::CNetEventIO` target and `scxml_cnet_domain_fence`. Development branch / Draft PR #307 remains **DO NOT MERGE / DO NOT PUBLISH**.

## Ownership, not duplication

CNet/NativeIO alone owns the listener, connection, request generation, poll/terminal callback and buffer lease. Salts ComponentPlugin/Plugin alone owns the Provider DSO and its generation Scope; TurboSCXML's Component bridge already pins the typed Service while a Session or Invoke runs. The domain admission fence **does not** retain a Plugin Scope, own a socket, store NativeIO request state, publish a replacement Provider, create a worker or implement a second reference count. It tracks only three generation identities (current, staged, draining) and a monotonically increasing owner epoch.

A caller constructs and binds the actual CNet listener **once**, then initializes a fence on that CNet owner's native callback/poll thread. Every fence API is owner-affine; a control-plane publication on another thread must first be marshalled through the existing serial CNet Owner lane. That is the synchronization boundary: there is no cross-thread check-then-send window.

## Allowed transition

```text
single domain-owned CNet listener / CNet owner lane
       |
       +-- attach gN (becomes current, epoch 1)
       |      gN try_submit -> owner checks identity -> real CNet send
       |      gN already accepted command/NativeIO terminal remains gN's
       |
       +-- attach gN+1 (staged; no listener rebind)
       +-- activate gN+1 (owner lane, epoch 2)
       |      current := gN+1; draining := gN
       |      gN new exclusive I/O -> rejected before CNet callback
       |      gN+1 new I/O -> allowed
       |      gN old CNet terminal + Plugin Scope remain authoritative
       |      third attachment -> BUSY
       |
       +-- retire draining gN only after explicit owner quiescence proof
       |      (native terminal observed AND existing Component Scope drained)
       +-- now third staged generation may be admitted
```

`try_submit` executes an exact caller-provided, nonblocking CNet operation in the same owner-lane call after checking the published generation. Reentrant activation or close during submission returns BUSY. A successful submit means **CNet accepted a command**, not that the operation is complete, an Invoke has ended, the Event has been delivered, or the Plugin can be unloaded. Do not use a fence generation id as an async completion id; the CNet connection/request slot+generation and external ComponentScope remain authoritative.

`retire` for a draining or current-closed generation invokes a caller-provided quiescence probe on the same owner lane. The probe must check the actual provider generation/Scope and native terminal; it must not merely observe a local cancellation request. Retiring a staged, never-activated generation is a rollback and does not require a native terminal, since it has never admitted I/O. Generation IDs must be strictly increasing within the domain and cannot be reattached after retirement.

## Scope of tested implementation

The admission-focused `scxml_cnet_domain_fence_test` binds one real loopback CNet listener, connects one sender/receiver pair, submits A under gN, activates gN+1 without rebinding, rejects an old-generation write, and submits B under gN+1. It observes both real CNet `on_send` completions, then tests busy drain, third-generation rejection, staged rollback, cross-owner rejection and owner-affine teardown. Its simple terminal/scope booleans remain isolated *test stubs*, not production Component ownership.

The stronger `scxml_component_cnet_invoke_test` exercises the SAME identities together: it loads two **real CMeta Invoke Provider DSOs**, builds/publishes Salts ComponentPlugin generations gN and gN+1, acquires their authoritative `scxml_component_scope` leases, binds `scxml_component_invoke_provider` into two real SCXML Sessions, and reads generated Invoke tokens only via borrowed DSO CMeta projections. Both Sessions run concurrently with one actual CNet listener and CNet owner-fenced sends. The retirement callback checks genuine CNet `on_send` completion counts and delegates the final decision to **`salts_component_plugin_generation_drain`**; it does not consult substitute Scope booleans.

The joint test qualifies two orderings. First, gN's native write completes but its Invoke/Scope stays live: the old DSO remains loaded, gN retirement is BUSY, gN+1 is independently active, and a third generation is rejected. A real `scxml_session_report_invoke_done` succeeds once, stale/duplicate tokens fail, then Session/Invoke Provider and Scope retire before ComponentPlugin can drain and the old DSO unloads. Second, gN+1 accepts another CNet write **before** its Invoke exits: the actual DSO `prepare_cancel` effect commits and the Session closes while that native write is pending. Quiescence remains BUSY until the later CNet terminal callback; it remains BUSY again while the original Component Scope is still pinned, and succeeds only after explicit Scope release. A failed native send admission is separately rejected without fabricated completion.

[Exact-head Linux qualification (2026-10-10)](https://github.com/qigao/turbo-scxml/actions/runs/37971670736) for commit `c51f1e4`: **15/15** focused Release tests, **12/12** installed C11/C++17 SDK consumers and **8/8** targeted Linux ASan+UBSan tests; based on `Salts.Native 2.3.0-rc.1` and `SaltsUtils.Native 4.3.0-rc.1`.

## Remaining conformance

- Qualify true DSO-executed CNet callback code or an explicitly retained DSO Provider wrapper for every accepted native I/O callback; the joint test's CNet observer functions still live in the test executable, while the Invoke and Scope code genuinely live in separate DSOs. Do not claim full callback-unload fencing yet.
- Adversarial controlled races for true simultaneous ACT cancel versus kernel completion, Session restart/request-slot reuse, and failed live Component candidate activation with rollback; preserve exactly-once effect-ticket and `done.invoke.*` behavior.
- TSan where supported, Windows IOCP and macOS Kqueue runtime and exact installed Salts 2.3/SaltsUtils 4.3 multi-RID release qualification. Linux ASan+UBSan is qualified only for the focused tests listed above.
- No automatic retry or fallback generation, and no release/merge authorization implied by one Linux gate.
