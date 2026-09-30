# VoiceXML document fetchaudio handoff

This slice provides the backend-neutral boundary required to play VoiceXML
`fetchaudio` during a real document fetch. DocumentStore does not fetch or
decode audio, own a clock, create a timer, or create a playback thread.

## Cache boundary

```text
normalized document URI
    |
    +-- cache hit -> borrow cached entry
    |               no document provider call
    |               no fetchaudio callback
    |
    +-- cache miss
          -> optional fetchaudio begin
          -> document provider open/open_with_policy
          -> fetchaudio finish if it started
          -> copy/close/compile/publish document
```

Fetch-audio therefore corresponds to an actual fetch delay rather than a cache
lookup.

## Request policy

`vxml_document_fetch_policy_v1` keeps its historical timeout prefix and
appends:

- `has_fetchaudio`;
- one already-resolved absolute URI;
- `has_fetchaudio_delay + fetchaudio_delay_us`;
- `has_fetchaudio_minimum + fetchaudio_minimum_us`.

Presence flags distinguish an absent platform-default value from explicit
`0us`.

The URI is resolved by the language/navigation owner because only that owner
knows the correct current document base. DocumentStore validates it as an
absolute bounded URI but does not rebase it against the target document.

## Playback adapter

A store may borrow one `vxml_fetch_audio_adapter_v1`.

`begin()` receives callback-borrowed URI/timing views and returns:

- `VXML_FETCH_AUDIO_STARTED`: one move-only finish ticket;
- `VXML_FETCH_AUDIO_SKIPPED`: no ticket.

SKIPPED covers missing/unsupported/unavailable fetch-audio. It never changes
the result of the main document fetch.

For STARTED, `finish()` is called exactly once after the document provider
returns, whether that provider succeeded or failed. The playback provider owns
the meaning of delay/minimum and may synchronously honor minimum playback
before finish returns.

No ticket survives `vxml_document_store_acquire_with_policy()`.

## Failure semantics

Fetch-audio retrieval/playback unavailability is deliberately separate from
the main document resource status. A SKIPPED fetch-audio request therefore:

- does not synthesize `error.badfetch`;
- does not replace a successful main document result;
- does not replace a failed main document result.

Malformed adapter contracts are programming errors and fail the acquire rather
than being disguised as an ordinary missing audio resource.

## ABI compatibility

The existing policy prefix through `timeout_us` remains accepted.

The existing DocumentStore config prefix through `document_user` remains
accepted. Fetch-audio adapter/user are an optional append-only tail.

The document provider's `open_with_policy` still consumes only the historical
timeout prefix unless it explicitly understands later policy fields.
