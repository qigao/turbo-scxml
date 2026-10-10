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

## Real DSO-resident CNet callbacks and safe unload (qualified slice)

The strengthened joint test `scxml_component_cnet_invoke_test` now adds a
second test within the original DSO-backed Invoke + CNet fixture. Both DSOs
implement a **test-only CMeta Object Interface**
(`tests/scxml_component_invoke_probe.h`) that explicitly projects a CNet
observer and returns copied completion/terminal statistics. The observer's
`on_state` and `on_send` function pointers reside **inside the corresponding
DSO**, not in the test executable. The test obtains each callback through an
authoritative live `scxml_component_scope`; there is no `dlsym`, direct
Plugin registry handle bypass or new production ABI.

gN and gN+1 have separate CNet connections/Observers on the **same** domain
CNet owner and **same** bound TCP listener. After a send under gN is admitted,
publication switches the fence to gN+1; the gN send still completes through
its original DSO callback while fresh gN writes fail. gN+1's independent
DSO callback and Invoke remain active. Attempting to drain or unload gN
before real `on_state(CLOSED/FAILED)` is observed returns BUSY. After old
CNet terminal, Invoke token completion and Scope release, the original DSO
unloads successfully. The test then sends a new byte through gN+1 **after
gN's code is unloaded**, ensuring the new generation is independent.

The reverse ordering is also covered on gN+1: an accepted CNet write is
pending when its DSO-backed Invoke cancels/exits. It cannot retire before
`on_send`; it still cannot retire after `on_send` but before real
`on_state(CLOSED/FAILED)` and existing Component Scope release. Generation
drain relies on `salts_component_plugin_generation_drain`, not a synthetic
scope flag. The CNet terminal evidence is a value copied from the live
DSO's CMeta snapshot **before** Scope release; its code pointers are never
called after unload.

**Exact-code Linux CI:** [#37974124859](https://github.com/qigao/turbo-scxml/actions/runs/37974124859),
commit `cbd5bca37286d19251c28bb9b8a13df944b94178`: **15/15**
focused Release tests, **12/12** installed C11/C++17 consumers and **8/8**
ASan+UBSan CNet/Host/DSO tests, `Salts.Native 2.3.0-rc.1` and
`SaltsUtils.Native 4.3.0-rc.1`. `ccache`: 173/179 compilation hits.
The CNet/CMeta observer interface exists only in test sources, not the
installed TurboSCXML API.

## Cross-thread DSO / Invoke / CNet conformance (2026-10-10 Linux)

The joint `scxml_component_cnet_invoke_test` now has test-only,
address-stable C11 atomic callback gates projected through the existing
borrowed CMeta Service; the installed TurboSCXML ABI is unchanged. The CNet
owner actually enters the gN DSO `on_send` or the gN+1 DSO terminal
`on_state(CLOSED/FAILED)`, while a different real thread attempts
`cmeta_plugin_registry_unload`. The registry's authoritative lease/lock
path returns BUSY. After the callback resumes and the *actual* CNet terminal
is observed, Session/Invoke/Component Scope retirement proceeds in order.
No observer function pointer is used after its originating DSO unloads.

A separate two-gate case places the real gN+1 DSO Invoke
`prepare_cancel` on its CFlow SerialExecutor worker in flight at the
**same time** as its real DSO `on_send` completion on the CNet owner. Both
gates must be entered before a coordinator thread releases either side.
The single Statechart effect journal still settles the Invoke cancellation;
the CNet callback does not synthesize a second CFlow settlement, and a
subsequent stale `done.invoke` is rejected. This tests **cross-lane
overlap**, not yet the deeper NativeIO *same-request* ACT cancel-vs-terminal
linearization proof.

The owner-affine domain fence also has a real two-thread negative test:
foreign activation during an in-flight owner admission fails without
invalidating the accepted command or switching epochs. Switching on the
owner lane afterward fences gN admission and preserves gN+1 operation.

**Exact-head qualified CI:** [#37977909381](https://github.com/qigao/turbo-scxml/actions/runs/37977909381)
at [`37785b5`](https://github.com/qigao/turbo-scxml/commit/37785b551f5e465afcf86703c9528ca290c5785a):
15/15 focused Release tests, 12/12 installed C11/C++17 consumers,
8/8 Linux ASan+UBSan and **2/2 separate Linux TSan tests**. TSan instruments
the local TurboSCXML targets and test DSOs against an installed
`Salts.Native 2.3.0-rc.1` / `SaltsUtils.Native 4.3.0-rc.1` graph; the
external SDK native binaries are not TSan-instrumented. Repeated TSan
stress qualification is tracked by the later workflow change `36dd6f3`;
report it only once its own exact-head CI finishes.

## Remaining conformance

- NativeIO-authoritative ACT cancellation versus completion on **the same request identity**, including exactly-once terminal and request-slot/generation reuse. The cross-lane DSO overlap above is not a substitute.
- Failed candidate activation with deterministic rollback, stale Scope invalid release, close/reopen and exhaustion with live native work; no alternate registry, fallback, or automatic settlement retry.
- Windows IOCP and macOS Kqueue runtime/DSO race tests and exact installed multi-RID Salts 2.3/SaltsUtils 4.3 SDK qualification. A Linux-only sanitizer pass cannot close those gates.
- Draft PR #307 remains **DO NOT MERGE / DO NOT PUBLISH**; no Salts 2.3 release admission is implied.
