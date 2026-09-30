# VoiceXML INITIAL prompt ownership

INITIAL prompt support reuses the existing prompt/media pipeline. No public
prompt-media struct, provider ticket, completion, barge-in, or MARK ABI is
extended.

## Private Program ownership

```text
vxml_cmeta_prompt_row
  owner_kind = FIELD | INITIAL
  owner       = immutable row index
  ... existing prompt metadata ...
```

FIELD keeps its historical `first_prompt/prompt_count` range. INITIAL now
owns an equivalent private range. A prompt row is valid only when its private
owner kind/index exactly matches the active form item.

## Selection

```text
active FIELD
  -> retry count from FIELD noinput/nomatch counters
  -> FIELD prompt range

active INITIAL
  -> retry count from INITIAL noinput/nomatch counters
  -> INITIAL prompt range

both/none active
  -> fail closed
```

The same declaration-order, count, condition, capability, segment, fallback,
barge-in, timeout and MARK checks are then applied.

INITIAL prompt conditions evaluate in form/document lexical scope.

## Public projection

The existing public APIs are unchanged:

- `vxml_session_cmeta_prompt()`
- `vxml_session_cmeta_prompt_media_request()`
- `vxml_session_cmeta_prompt_media_batch_request()`
- prompt prepare/commit/discard/completion
- barge-in and MARK progress

For INITIAL, the historical public `field` view is empty rather than exposing
a fake application-root field. The generation is still the active collect
generation.

No application-root storage is allocated or mutated for INITIAL prompt
ownership.
