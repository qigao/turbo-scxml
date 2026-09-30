# VoiceXML literal audio prompt V1

This slice extends the transactional prompt-media V1 path with one standard
literal VoiceXML `audio@src` prompt.

## Supported form

```xml
<field name="value">
  <prompt count="2">
    <audio src="retry.wav"/>
  </prompt>
  <grammar type="application/srgs+xml" src="value.grxml"/>
</field>
```

The compiled prompt row retains:

- field ownership;
- taper count;
- optional compiled `cond`;
- media kind `VXML_CMETA_PROMPT_MEDIA_AUDIO`;
- decoded immutable `src` URI bytes.

The source document may be destroyed immediately after compilation.

## Standard boundary

VoiceXML `audio` uses `src` or `expr` to identify the resource. It does
not define a media-type attribute. This profile therefore does not invent one.

V1 admits literal `src` only. The media request exposes:

```text
kind = AUDIO
payload = exact decoded URI bytes
media_type = empty
required capability = VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO
```

## Fail-closed V1 restrictions

The following remain unsupported:

- `audio@expr`;
- fetch-control attributes;
- fallback content inside `audio`;
- more than one audio element in a prompt;
- non-whitespace prompt text surrounding audio;
- SSML or other prompt child markup.

Those shapes fail during compilation rather than being flattened or silently
approximated.

Whitespace-only formatting around the one audio element is ignored by the
media payload.

## Selector compatibility

The existing retry/taper selector remains authoritative for TEXT and AUDIO
rows.

For an AUDIO row:

- `vxml_session_cmeta_prompt()` still reports selected count, prompt count,
  field and generation;
- its legacy `text` view is empty;
- `vxml_session_cmeta_prompt_media_request()` returns the AUDIO segment URI.

This preserves the published prompt-view ABI while giving the media provider
the richer payload.

## Provider behavior

A TEXT-only provider rejects the AUDIO request before its callback.

An AUDIO-capable provider uses exactly the same transactional prepare,
commit/discard and generation-cancel lifecycle introduced by #121. No fetch,
codec, device or playback worker exists in core.

## Deferred V2

Mixed text/audio ordering and VoiceXML audio fallback require more than one
segment and cannot be represented honestly by V1's inline single segment.

A later #47 V2 request will carry a bounded immutable segment array and explicit
fallback structure while preserving this V1 behavior.
