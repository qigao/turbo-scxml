# ACE 2.3 CNet outbound: transactional raw-byte transport

Tracks #297, #299 and #300. Development-only: Draft PR #307; no merge or release.

## Type and ownership

Outbound is a **nonstandard, explicitly selected raw-byte transport strategy**. The
processor type is `urn:turboscxml:cnet-raw:1`; the only supported target is
`cnet://bound`. Default/empty SCXML Event I/O type is rejected, rather than
silently usurping the W3C SCXML Event Processor. The bytes written are either
the Event name (no payload) or the literal TEXT_UTF8 content; there is no
message framing, event envelope, or HTTP encoding. Other content kinds,
delayed send and cancel are explicitly unsupported.

Runtime owners:

| Data or operation | Owner |
| --- | --- |
| CMeta Component generation / Plugin lease | Existing Salts ComponentPlugin runtime |
| SCXML macrostep / effect ticket | Existing CFlow Statechart/Session SerialExecutor |
| Prepared bytes, send admission slots | CNetEventIO bounded egress |
| CNet send commands, NativeIO requests and authoritative completion | Caller-owned CNet owner |
| TCP transport / bound connection | Host CNet client |

## Four-stage transactional publication

1. The SCXML SerialExecutor calls prepare_send to reserve one of the
   preallocated rows. The borrowed request bytes are copied in full before
   returning ACCEPTED and one move-only CFlow effect ticket. FULL leaves
   the request unaccepted and returns no ticket.
2. CFlow publishes the macrostep, then invokes exactly one ticket callback:
   commit marks the row READY in commit order, discard releases it. Neither
   callback calls CNet or waits for another executor/owner.
3. The host CNet owner explicitly calls pump, which selects READY rows in
   commit order and submits an immutable Salts Core buffer through
   cnet_send_buffer. CNet retains its own buffer reference through the real
   completion. ENOBUFS/EBUSY retain READY for *explicit* re-admission; no
   automatic settlement retry or unbounded queue exists.
4. CNet on_send or the connection's authoritative terminal releases
   submitted rows. The Session's close callback rejects new prepare work,
   cancels committed-but-not-submitted rows, and retains in-flight state.
   The CNet owner must close/drain its connection before egress destroy.

One CNet connection accepts only one observer. The ingress and egress
observers can be used on separate connections; a future bidirectional
Host adapter must be the single observer that forwards state, receive and
send terminal callbacks to both modules. Do not install both independently
on the same connection.

## Negative contracts

- No default type alias, implicit target rewriting, plaintext send of
  malformed CMETA objects or delayed-send acceptance.
- No second Statechart Actor, CNet poller, worker, Resource Provider
  registry, Plugin lease, or runtime interpreter.
- No unbounded storage, no bypass of CFlow effect journal, no CNet call
  from ticket commit/discard, no early destruction before terminal.
- Cancellation of READY state is local; a submitted NativeIO request is
  not terminal merely because close/cancel was requested.
- A failed CNet submission is observable through stats; it is not
  silently reclassified as an SCXML send success. Full SCXML domain
  asynchronous error reporting and wire-level codec remain future work.

## Qualification checklist

- Deterministic ticket tests: accepted versus FULL, discard, close while
  ticket is reserved, wrong type/target, delayed send rejection.
- SCXML end-to-end: <send> commits before owner pump and a real loopback
  TCP receiver observes the exact expected bytes and CNet send terminal.
- Stale CNet generation callbacks, real close/terminal, Session
  quiescence, and installed C11/C++17 consumers must remain valid.
- Retain W3C regression, ACE Component DSO tests and SDK manifest
  verification. Cross-platform/sanitizer gates and full #299/#300
  feature requirements remain open until separately proven.
