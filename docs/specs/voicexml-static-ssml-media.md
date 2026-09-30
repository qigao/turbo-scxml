# VoiceXML static SSML media descriptors

This slice turns the already-published SSML prompt-media kind and capability
into an actual CMeta compiler output.

## Static profile

The compiler recognizes the following static VoiceXML/SSML speech-markup
elements inside a prompt:

`break`, `emphasis`, `p`, `phoneme`, `prosody`, `say-as`, `s`,
`sub`, and `voice`.

A recognized subtree may contain text, comments, processing instructions, and
other elements from that same static set. Dynamic VoiceXML prompt elements
(`value`, `enumerate`), audio fallback, metadata, lexicon, and arbitrary
foreign markup remain fail-closed/deferred.

## Compile boundary

```text
parsed prompt child
    |
    +-- validate static SSML subtree
    +-- salts_xml_node_serialize()
    +-- copy serialized bytes into Program string storage
    +-- publish immutable SSML segment
         media_type = application/ssml+xml
```

The parser-owned serialized temporary is freed during compilation. Runtime
requests borrow only Program-owned payload bytes.

## Ordering and admission

SSML participates in the existing ordered segment table exactly like TEXT,
AUDIO, and MARK. A mixed prompt therefore preserves document order and requires
the existing BATCH capability in addition to the union of its segment
capabilities.

A one-segment SSML prompt uses the historical single-segment transactional
prepare path.

Provider capability mismatch is rejected before `prepare` or
`prepare_batch` is called.

## Limits

Serialized SSML bytes count toward `max_prompt_bytes`; the segment counts
toward `max_prompt_segments`. No TTS engine, codec, device, timer, or worker
is added.
