# VoiceXML static audio fallback ranges

VoiceXML `audio` alternate content is represented as immutable batch metadata.
TurboSCXML does not fetch, decode, retry, or play media in core.

## Why the segment ABI is unchanged

`vxml_cmeta_prompt_media_segment_v1` is an array element consumed by external
providers. Appending fields would change the array stride for newly compiled
TurboSCXML while an older provider would still advance by its historical
`sizeof(segment)`. That is not a safe tail extension.

Fallback is therefore a separate Program-owned side table:

```c
typedef struct vxml_cmeta_prompt_media_fallback_v1 {
    size_t audio_segment_index;
    size_t first_fallback_segment;
    size_t fallback_segment_count;
} vxml_cmeta_prompt_media_fallback_v1;
```

The batch request appends a borrowed `fallbacks` pointer plus
`fallback_count`. Requests carrying this metadata require
`VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK`.

## Compile shape

```text
Before <audio src="a.wav">fallback <emphasis>voice</emphasis></audio> After
   |
   v
segments:
  0 TEXT  "Before "
  1 AUDIO "a.wav"
  2 TEXT  "fallback "
  3 SSML  "<emphasis>voice</emphasis>"
  4 TEXT  " After"

fallbacks:
  { audio_segment_index=1, first_fallback_segment=2,
    fallback_segment_count=2 }
```

The range is conditional metadata, not ordinary unconditional playback.

## Provider contract

For an AUDIO segment with a fallback row:

- if AUDIO is playable, play it and skip its fallback range;
- if AUDIO cannot be played, play the fallback range and continue after it;
- a provider that does not advertise `CAP_AUDIO_FALLBACK` is rejected before
  `prepare_batch`;
- no fallback row preserves the historical AUDIO behavior.

The whole batch still has one prepare/commit/discard ticket, so no partial
fallback publication is observable.

## Static scope

This slice accepts literal `audio@src` plus fallback text and the static SSML
profile. Dynamic `expr`, resource fetch policy, codec selection, and media
execution remain outside the compiler boundary.

Fallback payloads and rows are Program-owned and immutable. Existing prompt
byte and segment limits remain hard admission bounds.
