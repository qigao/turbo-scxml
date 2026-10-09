# ACE 2.3 CNet: framed Host routing slice

Tracks TurboSCXML #297, #298 and #299 in Draft PR #307. **DO NOT MERGE / DO NOT PUBLISH.**

## One semantic owner per Session

`TurboSCXML::Host` owns the bounded Session Host router and its
SEND/CONTENT SCXML Event I/O adapter. It links only `TurboSCXML::SCXML`,
without CNet or a second Actor/Statechart. `TurboSCXML::CNetEventIO`
depends on Host plus `Salts::CNet` and owns optional network ingress/egress.
A Host endpoint is `{slot, generation}`, paired with a borrowed
`scxml_session`; CFlow remains its only Statechart instance and SerialExecutor.
The router retains accepted, not-yet-transferred external Events. Its
endpoint generation advances on slot reuse and stale IDs are rejected.
Detach returns BUSY while RESERVED or READY events still borrow that Session.

### Transactional and transport admission

- **SCXML producer:** `host_router_prepare_target` resolves the destination
  and copies a complete Event plus optional `sendid` and bounded TEXT_UTF8
  or XML_UTF8 `_event.data` into a RESERVED row. The Host SEND/CONTENT
  adapter returns one CFlow effect ticket. `commit` publishes READY;
  `discard` releases it. Neither callback drives CNet or calls a Session.
- **CNet producer:** `host_router_enqueue` copies an already decoded complete
  Event into the same bounded Host queue. It has no SCXML microstep to commit.
- **Host consumer:** explicit `host_router_drain` transfers ready Events in
  commit order via `scxml_session_try_send_named_with_metadata`. Session FULL
  retains the same head; no busy-loop, automatic retry or unbounded backup.
- **Lifecycle:** Host target cancel drops READY rows; source close and
  aborted initialization also cancel unexpired DELAYED rows. RESERVED and
  FIRING/INFLIGHT work still requires natural settlement. Router close
  cancels READY and DELAYED globally; detach requires all borrows gone.

## CFlow Clock delayed sends and sendid cancellation

The base Host adapter remains SEND|CONTENT. An explicit delayed binding
requires a valid borrowed monotonic `cflow_clock`, positive
`timer_capacity` and `cancel_capacity`, and opts in to DELAYED_SEND|CANCEL.
No second Scheduler or Statechart timer queue is created. Deadlines are
measured at effect-ticket **commit**, and the Host owner alone invokes
`scxml_host_router_run_due` after serialized CFlow Clock progress.

A bounded Host Event row keeps the borrowed request's copied sendid and
content while RESERVED/DELAYED. At the deadline, the Host claims FIRING,
releases its mutex, calls the existing SCXML Session sendid registry's
`scxml_session_report_send_done`, and only then publishes READY. Target
Mailbox FULL retains READY without automatic retry.

Cancel has its own finite ticket pool. Preparing it pins the source and
the original Event-row identity, guarding against slot reuse. CFlow
commit cancels RESERVED/DELAYED rows; discard leaves them intact. If
the deadline claim has already won, late cancel is a terminal no-op.
The runtime may clear its sendid registry while its Host cancel intent is
still live. The Host recognizes this specific completion race by the
matching row identity and cancel intent; a false sendid completion without
such proof remains a protocol failure and never invents a successful Event.

Closing a source only cancels its unexpired READY/DELAYED rows; a still
FIRING/INFLIGHT row retains Session/Plugin generation ownership until
natural settlement. Clock mutation must be externally serialized with
Host due-driving and commit. Tests cover exact virtual deadline, same
microstep send+cancel, discard, capacity, fire-wins, source-close,
invalid sendid and Release/ASan+UBSan memory safety.

## Experimental framed TCP wire profile

The opt-in frame ingress consumes **2-byte big-endian nonzero payload length**
followed by exactly that many raw bytes. It is not the W3C SCXML Event
Processor and not a general-purpose SCXML serialization. Each complete
frame is represented byte-exactly as lowercase hex TEXT_UTF8 content of
one configured external Event; its destination is an explicit Host endpoint.

Limits are fixed at initialization:

- Frame size `1..SCXML_EVENT_METADATA_CAPACITY/2`; zero/oversized prefixes
  reject the stream without truncation.
- One callback-owned copied chunk of `max_receive_bytes` with an upper hard
  bound of `SCXML_EVENT_DATA_CAPACITY`. The decoder owns one partial/complete
  frame, not an unbounded accumulation of callbacks.
- One explicit CNet receive credit at a time. Decoding must consume the
  prior chunk (and transfer any complete frame to Host) before rearming.
- When the Host queue is FULL, the complete frame and the remainder of
  the current callback buffer remain owned and unmodified. Further receive
  credits are denied until the caller drains Host and invokes process again.

TCP may split one 2-byte prefix across callbacks or coalesce multiple
frames into one callback. The parser reconstructs frame boundaries without
using callback boundaries as message boundaries. A truncated prefix/payload
at authoritative connection terminal is a protocol error, not a successful
empty Event. A complete retained frame can still be handed off after
transport terminal if Host capacity becomes available.

## Ownership and acceptance

- Caller owns and progresses the CNet client/listener; bridge callbacks run
  only on that client owner lane. No bridge starts a poller or owns NativeIO.
- CMeta Component/Plugin generation scopes remain held by the surrounding
  Host. Neither router nor decoder creates a second DSO lease.
- Real CNet close/terminal precedes decoder observer destruction; Session
  and Host endpoint remain live through the final Host delivery or cancel.
- CHttp continues to own HTTP; there is no HTTP parser in this profile.

The installed SDK is qualified with **independent C11/C++17 Host-only consumers**
and CNet consumers. An explicit CMake option enables Host; CNetEventIO
requires Host rather than providing an implicit fallback. Core SCXML remains
provider- and transport-neutral.

Tests validate two independent Session destinations and generation slot reuse,
microstep ticket commit/discard, a blocked executor causing true external
FIFO FULL with retained Host head, TCP split/coalesced frames, Host FULL,
stale CNet generation rejection, invalid length and terminal teardown.

**Remaining:** named/scalar/CMETA data-model conversion and complete normative
SCXML Event wire codec/target mapping, bidirectional observer composition,
cross-Owner Host Actor controls, full Invoke ACT and NativeIO terminal
cancellation races, generation fencing, TSan, Windows/macOS and complete
stable SDK qualification. Linux ASan+UBSan does not replace these gates.
