# VoiceXML tapered prompt selection

This slice adds deterministic prompt selection to the explicit CMeta VoiceXML
profile without introducing playback or media.

## Scope

Supported prompts are field-local literal text:

```xml
<field name="account">
  <prompt>Say your account number.</prompt>
  <prompt count="2">Please say it again.</prompt>
  <prompt count="3" cond="flag">One last try.</prompt>
  <grammar type="application/srgs+xml" src="account.grxml"/>
</field>
```

Prompt child markup is intentionally unsupported in this slice. SSML,
`audio`, timing and barge-in belong to the #47 media queue.

## Compile model

Each accepted prompt becomes one immutable Program row:

```text
field index
literal text bytes
count threshold
optional compiled CMeta condition
```

Text is copied into Program-owned storage. Comments and processing
instructions are ignored while text-node bytes are concatenated in document
order. Any element child fails closed instead of being flattened into text.

`count` defaults to 1 and must be a positive unsigned integer.

Compile options append two optional bounds:

- `max_prompts`
- `max_prompt_bytes`

Historical CMeta compile-option prefixes remain valid for documents that do
not contain prompts.

## Retry authority

Prompt tapering does not create a second retry counter.

The existing field-scoped Event counter table from the noinput/nomatch
recovery slice remains authoritative:

```text
prompt_count =
    1
  + field noinput occurrence count
  + field nomatch occurrence count
```

The value saturates at `UINT_MAX`.

The existing transactional retry-reset path resets those counters on form
entry, successful field completion and committed `clear`.

`reprompt` only publishes the existing one-shot control bit. It does not
increment retry state, so querying the prompt after consuming reprompt returns
the same taper level.

## Selection

`vxml_session_cmeta_prompt()` requires one active directed field.

For every prompt owned by that field:

1. `row.count <= prompt_count`;
2. optional `cond` evaluates true in form/document scope;
3. the highest eligible count wins;
4. declaration order breaks ties.

The returned `vxml_cmeta_prompt_view_v1` contains:

- current field name;
- borrowed immutable prompt text;
- selected row count;
- current derived prompt count;
- current collect generation.

If the active field has no eligible prompt, the query still succeeds and
returns empty text with selected count 0. This keeps prompts optional.

## Ownership

Prompt text and field-name views are borrowed from the immutable Program and
remain valid while the Program remains alive.

The selector allocates nothing, performs no XML parsing, does no runtime name
lookup and owns no provider/media resource.

## Boundary to #47

#47 may consume this descriptor as the control-plane source for prompt queue
admission. That later layer owns SSML/audio segmentation, timing, media
provider capability checks, queue reservation, completion and barge-in.

This slice must remain independently usable with no TTS, codec, audio device
or media-thread dependency.
