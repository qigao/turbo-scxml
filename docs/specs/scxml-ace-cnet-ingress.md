# ACE 2.3 / CNet ingress: bounded first slice

Tracks TurboSCXML #297, #298 and #299. **Development only; no merge or release.**

## Ownership table

| Participant | Authoritative owner |
| --- | --- |
| Configurator, Plugin code and generation scope | Salts ComponentPlugin / Plugin |
| Statechart, microsteps and external/internal Mailboxes | Existing SCXML Session and CFlow SerialExecutor |
| TCP listener, NativeIO progress and real connection terminal | Caller-owned CNet client/listener |
| Receive credits | Caller explicitly arms one receive at a time |
| Accepted CNet receive bytes | Optional CNetEventIO bounded Host ingress |
| SCXML external Event admission | Existing Session external FIFO |

No new CFlow Actor/Statechart, scheduler, poller, global provider registry or
Plugin lease is created. Half-Sync/Half-Async is a composition of existing CNet
owner callbacks and the Session SerialExecutor, not a second execution runtime.

## Byte chunk contract

CNet TCP receive callbacks report **byte chunks**, not application messages.
This first profile publishes one named external SCXML Event per CNet callback,
with raw bytes represented *losslessly* as lowercase hexadecimal UTF-8 text
in the Event data. For example 00 FF becomes 00ff. Application protocols
requiring framing or reassembly must do it separately; do not infer framing
from TCP callback boundaries or describe this as a complete Event I/O processor.

The caller configures a single bounded Event name and storage capacity.
Only callbacks for the exact CNet {slot, generation} are admitted.
Callback-borrowed data is copied before return and truncated data is never
published; excessive chunk size is an explicit error.

## Credit and backpressure

1. The host constructs the SCXML Session and independent CNet client/listener.
2. Ingress initialization allocates all bounded rows and copies the Event name.
   Its borrowed observer is installed on connect/accept and bound to the returned
   CNet connection before polling.
3. Once connected, explicit arm admits **one** CNet receive credit.
4. The CNet owner callback stages an owned byte chunk and settles the credit.
   A full Host ingress refuses further receive credits with SALTS_ENOBUFS.
5. Explicit drain attempts to copy the head row into the Session's existing
   external FIFO; CFLOW_MAILBOX_FULL retains the head in order. No hidden retry,
   worker or unbounded queue is permitted. Successful admission releases the row.
6. Close rejects further admission and explicitly accounts for cancelled rows.
   The host is responsible for closing/draining its real CNet connection;
   only CNet's authoritative terminal makes the observer safe to destroy.
   Session, Component generation, and borrowed callback users must live longer.

## Qualification / next slices

Current tests exercise real TCP loopback into a running Session, saturation,
stale CNet connection generation, explicit close and real terminal, plus
installed C11/C++17 package consumers. Linux ACE CI is a branch-only gate.

This first slice does **not** implement SCXML send/cancel effect tickets,
outbound transmission, delayed sends, host cross-Session routing or dynamic
Provider migration. All remain open tasks under #299/#300/#301. CHttp remains
the HTTP protocol owner. No Salts frozen ACE ABI changes are authorized here.
