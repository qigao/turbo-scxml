# VoiceXML literal prompt marks

This slice adds static prompt marks to the CMeta prompt-media profile and keeps
their execution progress generation-safe.

## Scope

Supported:

```xml
<prompt>
  hello
  <mark name="ad_start"/>
  <audio src="ad.wav"/>
  <mark name="ad_end"/>
</prompt>
```

The literal `name` form is admitted for the supported VoiceXML 2.0/2.1
profiles. Dynamic `nameexpr` remains outside this slice and belongs to the
remaining VoiceXML 2.1 dynamic-expression work.

This slice does not expose or synthesize `marktime`; there is no timing
thread, clock provider, or playback-duration approximation.

## Immutable media representation

A mark is one ordinary ordered prompt-media segment:

```text
TEXT -> MARK -> AUDIO -> MARK
```

The mark segment stores:

- kind = `VXML_CMETA_PROMPT_MEDIA_MARK`;
- payload = immutable Program-owned literal name;
- empty media type.

A prompt containing any MARK segment requires
`VXML_CMETA_PROMPT_MEDIA_CAP_MARK`. Mixed multi-segment prompts additionally
retain the existing BATCH capability requirement.

No provider-owned mark name is retained.

## Progress ownership

Playback completion remains the existing generation-scoped MPSC mailbox.

Mark execution progress is intentionally different: it is a **single-owner**
Session operation.

```text
async provider callback
    -> host/provider marshal to Session owner
    -> vxml_session_cmeta_prompt_media_mark(G, relative_segment)
```

The mark operation validates:

1. the Session is alive and running;
2. prompt media generation `G` is committed and still in flight;
3. the batch-relative index belongs to the selected prompt;
4. the indexed segment is MARK;
5. progress moves strictly forward.

Wrong generation is STALE. A TEXT/AUDIO index is NOT_MARK. Repeated or backward
mark progress is OUT_OF_ORDER.

This avoids adding a second concurrent mailbox beside the already-bounded
playback-completion ingress.

## Last-mark lifetime

On every successful prompt-media commit:

```text
last_mark_generation = committed generation
last_mark_segment = none
```

Accepted mark progress advances the retained segment index.

Normal playback completion and barge-in cancellation settle playback but do not
clear the last mark. This lets the owner query the last executed literal mark
after either boundary.

The next committed prompt generation resets last-mark progress before provider
commit.

`vxml_session_cmeta_prompt_media_last_mark()` returns:

- the generation of the most recently committed prompt;
- the batch-relative segment index, or `SIZE_MAX` if no mark executed;
- a borrowed Program-owned literal name, or an empty view.

Session close/destroy invalidates the query with the ordinary closed/session
lifetime rules.

## Barge-in interaction

Barge-in keeps the last accepted mark for the canceled generation. The
generation replay guard from the prompt-completion/barge-in slice prevents a
new prompt from reusing that same collect generation.

After FIA advances to a new collect generation and commits a new prompt, old
mark progress is cleared and stale updates from the prior generation cannot
mutate current state.

## Deferred work

The remaining VoiceXML 2.1 resource/media slices may build on this foundation
for:

- dynamic `mark@nameexpr`;
- wall-clock `marktime`;
- fetch/timing policy;
- richer last-result metadata.

Those additions must preserve the same bounded generation and ownership rules.
