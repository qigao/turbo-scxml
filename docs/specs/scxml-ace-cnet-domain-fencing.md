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

The deterministic native test binds one real loopback CNet listener, connects one sender/receiver pair, submits write A under gN, atomically activates gN+1 without rebinding or draining that socket, denies old generation write, accepts write B under gN+1, observes both real CNet on_send completions, then tests BUSY drain, third-generation rejection, staged rollback, cross-owner rejection and owner-affine teardown. Two-stage test quiescence flags are **test evidence only**; actual ComponentPlugin DSO scope and async Invoke-in-flight proof must be added before issue #301 can be declared complete.

## Remaining conformance

- Combine real DSO/ComponentPlugin Scope admission with the **same** CNet listener/terminal fixture; demonstrate old Invoke pinning gN while new work binds gN+1.
- Negative races on switch-versus-submit, receive/send callbacks, cancellation terminal versus Session teardown, failed candidate activation and rollback.
- ASan+UBSan, TSan where supported, Windows IOCP and macOS Kqueue runtime, exact installed Salts 2.3/SaltsUtils 4.3 multi-RID contract.
- No automatic retry or fallback generation, and no release/merge authorization implied by one Linux gate.
