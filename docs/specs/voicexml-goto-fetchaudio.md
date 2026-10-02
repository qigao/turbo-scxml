# VoiceXML goto fetchaudio language handoff

The original #154 slice wired literal `goto@fetchaudio` into the
backend-neutral DocumentStore fetch-audio boundary delivered by #144. #224
extends the same handoff with static document/form property inheritance for
`fetchaudio`, `fetchaudiodelay`, and `fetchaudiominimum`.

## Compile boundary

The compiler resolves the effective fetch-audio policy once:

```text
document properties
  -> form overrides
     -> explicit goto@fetchaudio URI override
        -> immutable goto action policy
```

URI, delay, and minimum inherit independently. An explicit
`goto@fetchaudio` replaces only the URI and keeps inherited timing. Delay and
minimum use the existing bounded VoiceXML Time Designation parser and are
stored as exact microseconds with explicit presence bits.

A local fragment goto accepts syntactically valid fetch-audio inputs but clears
the effective policy because no document fetch occurs. Duplicate properties in
one scope, empty fetchaudio values, and invalid timing designations reject at
compile time.

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

    bool has_fetchaudio_delay;
    uint64_t fetchaudio_delay_us;
    bool has_fetchaudio_minimum;
    uint64_t fetchaudio_minimum_us;
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
5. copy the inherited delay/minimum presence bits and microseconds unchanged;
6. acquire the target through `vxml_document_store_acquire_with_policy()`.

The #144 store boundary guarantees that a cache hit invokes no fetch-audio
callback. On a real cache miss, the resolved absolute URI plus the exact
delay/minimum policy reaches the fetch-audio adapter and STARTED/SKIPPED
semantics remain authoritative.

## Ownership

The compiler owns retained URI bytes in the Program. Navigation requests only
borrow them. DialogManager uses bounded owner-owned scratch for both target and
fetchaudio URI resolution; no view survives the synchronous acquire call.

No audio decoder, codec, timer, worker, playback device, or second transport
stack is introduced.
