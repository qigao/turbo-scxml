# VoiceXML external goto fetchaudio handoff

This slice carries explicit VoiceXML `goto@fetchaudio` from immutable
document compilation to the existing document-fetch boundary. TurboSCXML does
not add an audio player, codec, worker, or second HTTP stack.

## Standard boundary

`fetchaudio` is wait audio for a VoiceXML document fetch. It is not a
general audio/script/grammar fetch property. A failure to retrieve or play the
wait-audio URI does not fail the document fetch.

This slice supports an explicit literal attribute:

```xml
<goto next="dialogs/next.vxml#main"
      fetchaudio="media/wait.wav"/>
```

Property inheritance remains a later VoiceXML 2.1/default-policy slice.

## Compile model

The literal compiler decodes both URIs once and copies them into Program
storage.

Local fragment navigation never fetches a document, so:

```xml
<goto next="#local" fetchaudio="wait.wav"/>
```

is rejected rather than silently ignoring the attribute.

## Navigation ABI

The historical `vxml_navigation_target` and
`vxml_session_navigation()` remain unchanged.

A new size-versioned request exposes optional fetch metadata:

```c
typedef struct vxml_navigation_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    const char *fetchaudio_uri;
    size_t fetchaudio_uri_size;
} vxml_navigation_request_v1;
```

All views borrow Program-owned immutable bytes.

## Document fetch policy

`vxml_document_fetch_policy_v1` appends the optional fetchaudio view.
The historical timeout prefix remains ABI-valid.

DocumentStore behavior:

- cache hit: no provider callback and no wait audio;
- cache miss + policy-aware provider: pass fetchaudio through
  `open_with_policy`;
- cache miss + legacy provider: perform the ordinary document fetch and ignore
  fetchaudio;
- an explicit timeout still requires `open_with_policy` and fails closed if
  unsupported.

Thus fetchaudio is non-fatal while timeout remains an enforceable fetch
contract.

## DialogManager V3

External navigation maps the versioned navigation request into the existing
DocumentStore policy. There is no extra resolver or transport path.

The provider controls how/if the wait-audio hint is realized and must not turn
wait-audio retrieval failure into document `badfetch`.
