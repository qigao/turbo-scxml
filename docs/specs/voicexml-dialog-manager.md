# VoiceXML dialog manager

`TurboSCXML::VoiceXMLDialogManager` is the bounded non-media bridge between
CCXML dialog lifecycle actions and the existing synchronous VoiceXML core.

## Ownership

```text
CCXML session
  -> VoiceXMLDialogManager telephony decorator
       -> fixed-capacity dialog registry
       -> document provider
       -> VoiceXML program/session
       -> bounded host Event sink
  -> upstream CCXML telephony provider for non-dialog operations
```

The manager owns copied source/media/connection bytes, VoiceXML sessions, and
registry rows. V1 also owns compiled VoiceXML Programs. V2/V3 instead retain
generation-safe borrows from `VoiceXMLDocumentStore`, which remains
caller-owned. The manager borrows the upstream telephony adapter/user and Event
sink/user until manager destruction.

## Ticket protocol

Dialog `prepare_*` callbacks do not fetch, compile, start, or publish Events.
They only validate/copy borrowed input and reserve one bounded row.

Successful prepare returns one move-only CFlow effect ticket:

- `commit` is nonblocking and infallible; it only changes the reserved row to
  pending;
- `discard` releases the reservation or restores the previous row state;
- CCXML's existing returned-ID datamodel ticket is committed before the
  provider ticket for `dialogprepare` and direct `dialogstart`.

The caller then invokes `vxml_dialog_manager_run_ready()` from its chosen
serialized progress point.

## Progress

`run_ready()` performs the expensive/fallible work:

```text
pending prepare/start
  -> document open
  -> bounded vxml_compile
  -> optional vxml_session_init/start
  -> nonblocking Event sink
```

A FULL Event sink publishes nothing and leaves the terminal Event pending. A
later `run_ready()` retries only publication; it does not reopen/recompile the
document.

## Configuration revisions

- **V1** — document-provider based. The manager opens, compiles, and owns one
  Program per active row.
- **V2** — DocumentStore based. Programs are immutable cached borrows; initial
  source fragments and external goto remain fail-closed.
- **V3** — navigation-enabled DocumentStore mode. It adds a hard
  `max_navigation_hops` bound, accepts initial source fragments, and consumes
  `VXML_SESSION_NAVIGATING` handoffs.

V1 and V2 retain their published behavior when V3 is introduced.

## V3 external navigation

External goto remains transport-independent:

```text
current cached document + Program
    |
    | VoiceXML core yields NAVIGATING + borrowed target
    v
DialogManager V3
    |
    +-- resolve target relative to current document URI
    +-- acquire/view next DocumentStore ref
    +-- destroy old VoiceXML session
    +-- release old document ref
    +-- initialize next session
    +-- optional fragment -> vxml_session_start_at_form()
    |
    v
continue until EXITED or another bounded handoff
```

The next document is acquired before the old borrow is released, so a failed
resolution/acquisition never leaves the row without an owned Program. Each
successful handoff increments one row-local counter. When
`max_navigation_hops` is reached, the manager publishes
`error.dialog.start` with `VXML_LIMIT_EXCEEDED` and cleanup follows the same
row terminal path.

`dialog.started` is published once for the CCXML dialog after the complete
synchronous navigation chain settles, not once per leaf document.

The non-media VoiceXML core exits synchronously, so a successful start produces
`dialog.started` followed by `dialog.exit`. The state machine leaves an
explicit RUNNING state for later asynchronous VoiceXML slices.

## Dialog operations

- `dialogprepare`: compile and retain one program, then publish
  `dialog.prepared`.
- direct `dialogstart`: compile, initialize, and start through the same
  session path used by prepared start.
- prepared `dialogstart`: reuse the retained program, bind the copied
  connection ID, and start.
- normal `dialogterminate`: close/destroy retained VoiceXML runtime state and
  publish `dialog.exit`.

Immediate termination is not part of the current non-media profile.

## Backpressure and limits

The config explicitly bounds:

- dialog rows;
- source bytes;
- media-type bytes;
- connection-ID bytes;
- generated dialog-ID bytes;
- acquired VoiceXML document bytes;
- VoiceXML compiler limits;
- V3 external navigation hops.

Registry exhaustion returns `SCXML_ADAPTER_FULL`. No operation silently
allocates an unbounded queue or starts a hidden worker.

## Upstream decorator

Non-dialog CCXML telephony operations are forwarded to the configured upstream
adapter with the original request/ticket semantics. The manager does not wrap a
rejected upstream operation in a synthetic ticket.

Manager close:

1. stops new dialog admission;
2. destroys manager-owned dialog/program/session rows;
3. calls upstream `close` exactly once.

Quiescence requires both an empty manager registry and upstream
`is_quiescent == true`.

## Future growth

Prompt/collect/media providers reuse the CMeta Interface composition model from
`scxml-cmeta-provider-interfaces.md`. `VoiceXMLCHttpResource` supplies one
optional transport-backed document provider below DocumentStore. Future
asynchronous completions must carry dialog generation/tokens through a bounded
ingress before they are allowed to mutate a row.
