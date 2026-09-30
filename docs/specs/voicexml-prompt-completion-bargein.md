# VoiceXML prompt completion and barge-in

This slice extends the CMeta prompt-media boundary with bounded playback
completion and generation-scoped barge-in cancellation.

## Ownership model

```text
selected field / collect generation G
    |
    +-- prompt prepare -> provider reservation
    +-- prompt commit  -> one media generation in flight
    |
    +-- completion producer
    |      -> try_complete({G, outcome})
    |      -> one fixed mailbox slot
    |
    +-- single-owner run_ready()
           -> settle matching completion exactly once
```

The provider owns playback. TurboSCXML owns only the generation, one copied
completion outcome, and the provider ticket/cancel contract.

## Completion ingress

The prompt completion mailbox is fixed-capacity and generation scoped.

`try_complete()` returns:

- `ACCEPTED`: one outcome was copied into the empty mailbox;
- `FULL`: another completion is being written or is already ready;
- `CLOSED`: the Session no longer accepts producers;
- `STALE`: the generation is no longer active;
- `INVALID_ARGUMENT`: malformed completion ABI/outcome.

No provider-borrowed memory is retained.

`run_ready()` is the only single-owner consumption point. It clears the
in-flight prompt generation without calling provider cancel after a normal
completion.

## Barge-in

Prompt rows retain literal `bargein` and `bargeintype` policy.

A barge signal is valid only when:

- the supplied collect generation is the active collect generation;
- prompt playback for that same generation is committed;
- `bargein` is enabled;
- explicit `bargeintype` matches the signal when present.

Successful barge-in calls the provider's no-fail/nonblocking
`cancel(generation)`, disarms completion ingress, and records that collect
generation as barged.

The same collect generation is then barred from prompt re-admission. This is
required because prompt completion generation currently shares the collect
generation. Without the replay guard, a late completion from canceled playback
could otherwise match a newly admitted playback with the same generation.

A new prompt becomes admissible only after FIA advances to a new collect
generation.

## Shutdown

Session close/destroy:

- discards a prepared, uncommitted prompt ticket;
- cancels one committed prompt generation exactly once;
- closes completion ingress before freeing profile state.

Producers must stop before Session destruction. A completion arriving after
close observes `CLOSED`; a completion for a canceled generation while the
Session remains alive observes `STALE`.

## Non-goals

This slice does not add:

- TTS/audio devices or codecs;
- timing/fetchaudio policy;
- mark metadata;
- SSML execution beyond capability description;
- hidden workers or event loops.
