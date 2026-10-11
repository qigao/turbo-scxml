# VoiceXML dialog manager

## Asynchronous profile progress

The factory's append-only `run_ready` / `quiesce` pair connects a profile's
existing completion mailboxes to the manager. Both callbacks must be supplied
or both omitted. Historical prefixes ending at `init` or `max_script_steps`
retain their behavior; partially supplied tails are rejected.

The manager remains the sole owner of each Session, Program and document
borrow. Provider producers use the profile's existing bounded, generation-
checked ingress; the manager adds no payload queue or second interpreter.
The owner calls `run_ready`, which visits each eligible row at most once per
call, with a round-robin cursor and at most `max_work` visits. An idle visit
counts toward that budget. A profile callback performs at most one admission
or completion, never waits for a completion, and returns its exact status.

After a successful callback the manager observes Session state, follows
navigation/submit/script handoffs using the existing per-dialog limits, and
publishes `dialog.exit` at EXITED. `dialog.started` is published only once,
including across asynchronous navigation and submit. Profile failures use
the existing `error.dialog.start` status event. FULL retains the event and
Session; retries publish only, without repeating profile work.

Before releasing any initialized Session (normal exit, failure, terminate,
close, navigation or submit replacement), the manager calls factory `quiesce`.
It must stop and join external completion producers for that exact Session;
no producer may enter it after return. The callback may block and cannot
reenter the manager or destroy the Session/providers. Session close then
settles outstanding tickets and owned results before the manager releases
the Program/document. Callback context and providers outlive manager close.
Owner-affine providers with no external producers can implement a no-op
barrier. Cancellation alone is not a barrier.

This keeps profile execution outside the neutral manager and preserves its
dependency graph. An alternative direct CMeta dependency would make optional
profiles mandatory; exposing manager-owned Session pointers without a barrier
would leave replacement/destruction unsafe. Existing applications migrate by
supplying the complete factory tail and the existing profile/provider context;
omitting the tail restores manual asynchronous progression. No live Session
or provider payload is migrated between owners.

For CMeta, forward `run_ready` to `vxml_session_cmeta_run_ready(session,
out_progressed)`. This selects the active FIA item, admits the selected prompt
once per collect generation, and prepares/commits the configured collect,
subdialog, record or transfer provider. It settles at most one ready completion
per call; playback completion precedes recognition when both are ready.
No completion is an idle result. A handled admission Event counts as progress,
so an `error.noresource` or `error.semantic` catch is not replaced by a manager
failure. Existing manual prepare/commit APIs remain available for hosts that
choose their own scheduling; do not mix them with the aggregate driver on one
Session. The application still supplies provider engines, timers, child-owner
progress, barge-in signals and the producer barrier.

Executable evidence: `voicexml_dialog_cmeta_test` covers real delayed collect,
FULL, idle-row fairness, failure settlement and asynchronous navigation;
`voicexml_cmeta_session_test` covers aggregate prompt/collect, record lease,
transfer, subdialog completion and handled admission errors. These are local
provider fixtures, not TTS/ASR/telephony device qualification.

`TurboSCXML::VoiceXMLDialogManager` is the bounded, media-engine-neutral bridge
between CCXML dialog lifecycle actions and VoiceXML profile Sessions.

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

`ccxml_session_test` covers direct start through a real CCXML Session: an
Event sink returning FULL retains the dialog until publication succeeds, then
the single slot can be reused with a new ID. A later telephony action failure
discards both the dialog reservation and returned-ID ticket before any document
opens; a subsequent dispatch can use the released capacity. Both literal and
typed dynamic `src` variants exercise these paths. CCXML evaluates a dynamic
source immediately before prepare; the manager copies that borrowed result
under the same capacity and ownership limits as a literal source.

With Component enabled, `voicexml_component_dialog_test` additionally dispatches
real CCXML prepare, prepared start and terminate through an old DSO resource
generation after a new generation is published. It checks returned-ID writeback,
matching Event IDs, no refetch on prepared start/terminate, BUSY controller
destruction until upstream quiescence, and new-generation direct start after
the old module unloads. The controller and manager are destroyed before their
borrowed Component scope is released.

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

The non-media base core exits synchronously, so a successful start produces
`dialog.started` followed by `dialog.exit`. Asynchronous profiles remain
RUNNING and advance through the optional factory progress callback.

## Profile-neutral script progress

Store-backed V2/V3/V4 managers pair a DocumentStore compiler with
`vxml_session_factory_v1`. Its optional `execute_script`/`max_script_steps`
tail lets the same serialized `run_ready()` owner consume `SCRIPTING` handoffs.
This closes the gap where a QuickJS Session could be initialized but an external
script left the dialog waiting indefinitely after `dialog.started`.

The manager copies the complete factory table and borrows its user until
destruction. Each callback borrows the current Session and absolute document
URI only during the call, executes one script, settles its resource leases,
and resumes the Session. It must not reenter the manager. The Session remains
the committed-state owner; the manager neither inspects JavaScript nor copies
profile state. A QuickJS application adapter can use:

```c
/* app owns the Store, script provider/user, and matching Session init options. */
static vxml_status execute_script(void *user, vxml_session *session,
    const char *uri, size_t uri_size) {
    struct app *app = user;
    vxml_quickjs_script_execution_v1 execution =
        VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT;
    execution.resolver = app->store;
    execution.script_resources = app->scripts;
    execution.script_resource_user = app->script_user;
    execution.base_document_uri = uri;
    execution.base_document_uri_size = uri_size;
    return vxml_quickjs_session_execute_script(session, &execution);
}
```

Set the callback and a nonzero `max_script_steps` together. Every invocation
consumes one step before executing; the counter belongs to the dialog row and
survives navigation and submit-response Session replacement. It resets only
when the row is released. Exhaustion produces `error.dialog.start` with
`VXML_LIMIT_EXCEEDED`. This bounds even a callback returning success without
advancing the Session. It is independent of `max_navigation_hops` and of
QuickJS's per-evaluation heap, stack, and time limits. `max_work` still counts
dialog rows, not scripts; a callback must enforce its own time/I/O bounds.

Script, navigation, and submit handoffs compose in the same start chain.
Navigation/submit updates the base URI before the next script callback.
Callback failures produce `error.dialog.start` with their `vxml_status`, and
the existing terminal-row path destroys the Session and releases the Program.
`dialog.started` is published once after the chain settles; an exited Session
then produces `dialog.exit`. FULL retries only the pending event and never
reexecutes a script. Close releases retained rows before upstream close; the
application must retain the factory user/providers until manager destruction.

The additive tail preserves factories ending at the historical `init` prefix
and full tables with both new fields zero. These keep the original manual
handoff behavior. Partial tails or mismatched callback/budget pairs fail
initialization. Migration consists of wiring the application callback and
budget; rollback removes both fields. No additional package dependency is
introduced. Directly linking QuickJS into DialogManager was rejected because
it would couple neutral host progress to one profile; a separate executor
would duplicate the existing serialized progress and ownership boundary.

## Dialog operations

- `dialogprepare`: compile and retain one program, then publish
  `dialog.prepared`.
- direct `dialogstart`: compile, initialize, and start through the same
  session path used by prepared start.
- prepared `dialogstart`: reuse the retained program, bind the copied
  connection ID, and start.
- normal `dialogterminate`: close/destroy retained VoiceXML runtime state and
  publish `dialog.exit`.

Immediate termination is not part of the current manager contract. Normal
termination quiesces external producers before dispatching hangup and closing
the Session; the barrier is called only once for that Session.

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
