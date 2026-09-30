# VoiceXML goto fetchaudio language handoff

This slice wires literal `goto@fetchaudio` into the backend-neutral
DocumentStore fetch-audio boundary delivered by #144.

## Compile boundary

For an external goto, the compiler decodes and copies both `next` and
`fetchaudio` into immutable Program storage.

A local fragment goto accepts a syntactically valid `fetchaudio` attribute
but does not retain it in the executable action because no document fetch
occurs.

Empty or malformed literal fetchaudio values reject at compile time.

## Navigation ABI

The historical URI-only `vxml_session_navigation()` remains unchanged.

A size-versioned request exposes the optional fetch hint:

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

## DialogManager V3

For external navigation:

1. resolve the target document relative to the current document URI;
2. resolve fetchaudio independently against the same current document URI;
3. strip any fragment through the existing RFC3986 DocumentStore resolver;
4. build `vxml_document_fetch_policy_v1` with
   `has_fetchaudio=true`;
5. acquire the target through `vxml_document_store_acquire_with_policy()`.

The #144 store boundary guarantees that a cache hit invokes no fetch-audio
callback. On a real cache miss, the resolved absolute URI reaches the
fetch-audio adapter and STARTED/SKIPPED semantics remain authoritative.

This slice does not add fetchaudiodelay/fetchaudiominimum defaults. Those are
property/default inheritance work for #50.

## Ownership

The compiler owns retained URI bytes in the Program. Navigation requests only
borrow them. DialogManager uses bounded owner-owned scratch for both target and
fetchaudio URI resolution; no view survives the synchronous acquire call.

No audio decoder, codec, timer, worker, playback device, or second transport
stack is introduced.
