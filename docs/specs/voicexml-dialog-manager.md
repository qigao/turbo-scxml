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

The manager owns copied source/media/connection bytes, compiled VoiceXML
programs, VoiceXML sessions, and registry rows. It borrows the upstream
telephony adapter/user, document adapter/user, and Event sink/user until
manager destruction.

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
- VoiceXML compiler limits.

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
`scxml-cmeta-provider-interfaces.md`. #48 may add a CHTTP-backed document
provider without changing this manager ABI. Future asynchronous completions
must carry dialog generation/tokens through a bounded ingress before they are
allowed to mutate a row.
