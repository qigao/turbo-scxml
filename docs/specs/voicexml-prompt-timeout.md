# VoiceXML literal prompt timeout

This slice adds the static VoiceXML prompt input timeout to the CMeta collect
boundary without introducing a timer, clock provider, worker, or backend.

## Semantics

VoiceXML `prompt@timeout` is the interval the platform waits for input after
prompt playback. It is therefore collect policy rather than prompt-media
playback policy.

Supported literal Time Designations are non-negative decimal values with an
explicit `ms` or `s` suffix.

Examples:

```text
250ms  -> 250000 us
1s     -> 1000000 us
1.5s   -> 1500000 us
0ms    -> explicit zero
```

The compiler stores exact integer microseconds. Values finer than one
microsecond are rejected rather than rounded.

## Compile boundary

Prompt compilation parses the time designation once and stores:

```text
has_timeout
timeout_us
```

in the immutable prompt row.

Malformed, negative, unitless, overflowing, or over-precise values reject at
compile time.

There is no runtime string parsing.

## Prompt selection

The existing canonical prompt selector remains authoritative:

```text
field prompt rows
   -> prompt count
   -> cond evaluation
   -> highest eligible count
   -> selected immutable prompt
```

The collect request uses the timeout from that exact selected row.

If no eligible prompt exists, or the selected prompt omitted `timeout`, the
request reports `has_timeout=false`.

An explicit `timeout="0ms"` reports `has_timeout=true` with
`timeout_us=0`, so absence is not conflated with zero.

## Provider contract

`vxml_cmeta_collect_request_v1` receives an append-only tail:

```c
bool has_timeout;
uint64_t timeout_us;
```

The request remains callback-borrowed and generation scoped.

TurboSCXML does not start or own a timer. The recognition provider or host is
responsible for applying the supplied interval to its own wait mechanism.

Existing providers compiled against the historical V1 prefix continue to read
only their known prefix. No adapter ABI version is changed.

## Deferred fetch timing

`fetchaudio` and `fetchtimeout` are resource-fetch policy, not recognition
wait timing. They remain a separate DocumentStore/resource-provider slice.

That separation prevents prompt playback timeout, recognition timeout, and
resource-fetch timeout from being approximated as one mechanism.
