# VoiceXML CMeta scoped Events

This slice adds synchronous scoped Event dispatch to the explicit CMeta
VoiceXML profile. It does not add prompt/media generation, noinput/nomatch
production, or an asynchronous Event queue.

## Public surface

`vxml_session_cmeta_raise()` injects one byte-counted Event on the Session
owner thread.

The call borrows the supplied bytes only until it returns. Handler and thrown
Event names retained by the Program are immutable Program-owned storage.

## Handler scopes

Compiled handlers belong to exactly one immutable scope:

1. active directed field;
2. active form/dialog;
3. document.

The runtime never searches by XML node or string scope name.

Within one scope, one Event occurrence selects:

1. a dot-prefix matching handler;
2. the longest/more-specific matching Event prefix;
3. the highest eligible `count` threshold;
4. declaration order for remaining ties.

Event occurrence counters are Session-owned and bounded by
`max_event_counters`. Event names are copied into fixed-stride bounded
counter storage.

## Transaction boundary

One selected handler executes through the existing CMeta transaction engine.

```text
raise(event)
  -> select scoped handler
  -> transaction_begin
  -> execute existing CMeta actions
  -> rollback on failure
  -> commit on success
```

Handler-local `var` is intentionally deferred in this slice; ordinary
assignment/clear/if/exit reuse the already-qualified CMeta action paths.

## throw

Inside catch/help handler executable content:

```xml
<throw event="application.retry"/>
```

is a transfer barrier. The current handler transaction commits, then the new
Event restarts lookup from the innermost active scope. The complete chained
dispatch is bounded by `max_event_dispatch_depth`.

This first scoped-Event slice intentionally rejects throw/rethrow from ordinary
block/filled content so no transfer barrier can be silently ignored outside
the Event dispatcher.

## rethrow

`<rethrow/>` is legal only inside catch/help content.

After the current handler transaction commits, lookup continues with the same
Event at the next outer scope. The current handler cannot immediately select
itself again.

A rethrow beyond document scope is uncaught.

## help

`<help>...</help>` is compiled as a normal scoped handler for the literal
Event name `help`; there is no separate runtime code path.

## Failure behavior

An uncaught Event places the Session in `VXML_SESSION_FAILED` with
`VXML_SEMANTIC_ERROR`.

Handler action failure rolls back the complete handler transaction and
preserves the first useful runtime status as the stable Session error.

Event counter overflow, Event-name bound overflow, or chained-dispatch depth
overflow fail closed.

## Deferred

The following remain in later #46 slices:

- generation of noinput/nomatch Events;
- tapered prompt counters and prompt selection;
- default platform handlers;
- prompt/media interactions;
- normal CCXML hangup Event injection.

The scoped selector and transaction boundary delivered here are the foundation
those slices reuse.
