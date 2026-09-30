# VoiceXML transactional prompt media admission

This slice connects the existing CMeta tapered-prompt selector to a bounded
provider-facing media admission contract without adding a TTS/audio backend.

## Control-plane source

Prompt selection remains owned by the existing retry/taper model:

```text
field retry counters
  -> vxml_session_cmeta_prompt()
  -> selected immutable prompt row
```

The media layer does not select prompt rows, recompute `count`, reevaluate a
second retry table, or parse VoiceXML.

## V1 request

`vxml_cmeta_prompt_media_request_v1` represents at most one segment.

For this slice the only admitted segment is:

```text
kind = VXML_CMETA_PROMPT_MEDIA_TEXT
payload = exact Program-owned literal prompt bytes
media_type = empty
```

The enum already reserves SSML and AUDIO kinds. Those later #47 slices can
extend the request model without changing the current TEXT semantics.

If no prompt is eligible, the request query succeeds with
`segment_count == 0`. Provider admission is not attempted; calling prepare in
that state returns `VXML_INVALID_STATE`.

## Transactional provider

The optional append-only CMeta session tail borrows one
`vxml_cmeta_prompt_media_adapter_v1`.

`prepare()` receives callback-scoped immutable request views and returns one
move-only ticket:

- commit publishes exactly one media generation;
- discard publishes nothing;
- both callbacks must be nonblocking and no-fail;
- provider refusal or capability mismatch stores no ticket.

The provider must advertise `VXML_CMETA_PROMPT_MEDIA_CAP_TEXT` for the V1
literal segment.

## Generation authority

Prompt media does not own a second generation counter.

The currently selected directed-field collect generation is authoritative and
is copied into the media request. A committed media request is canceled when
that generation is no longer valid.

Current settlement points are:

- Directed FIA selects the next collect generation;
- `noinput` or `nomatch` begins a new retry/prompt admission opportunity;
- Session close/destroy.

Pending reservations are discarded. Committed generations receive one
no-fail/nonblocking `cancel(generation)`.

This same generation boundary is where later barge-in and playback completion
logic will attach.

## Ownership

Field names and TEXT payloads borrow immutable Program storage.

The Session owns only provider ticket/generation state. It never retains a
provider callback-scoped request pointer, never owns a provider/device thread,
and never creates a playback worker.

Provider and provider-user pointers are borrowed until Session destruction.

## Dependency boundary

No TTS, codec, audio device or media transport dependency enters
`TurboSCXML::VoiceXML` or `TurboSCXML::VoiceXMLCMeta`.

Tests use a deterministic fake provider only.

## Deferred #47 work

Later slices will build on this ABI for:

- SSML segmentation;
- `audio` URI plus fallback content;
- queue capacity across multiple segments;
- playback completion ingress;
- timing / fetchaudio inputs;
- barge-in cancellation;
- marks and progress metadata.

Those features must preserve the same ticket, generation and ownership rules.
