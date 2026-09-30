# VoiceXML per-request document fetch policy

This slice adds a bounded per-request fetch deadline to the existing
DocumentStore/document-adapter boundary without adding a timer, worker, or
mutable global transport setting.

## Contract

`vxml_document_fetch_policy_v1` carries:

- `has_timeout` — distinguishes no override from an explicit zero deadline;
- `timeout_us` — exact integer microseconds.

The policy is borrowed only for the provider callback.

`vxml_dialog_document_adapter_v1` keeps its historical `open/close` prefix
and appends optional `open_with_policy`. Old adapters remain valid. The
DocumentStore copies only the provider-declared struct prefix.

## Cache behavior

`vxml_document_store_acquire_with_policy()` normalizes the URI and checks the
cache before requiring provider policy support.

- cache hit: no provider callback, so an old adapter can satisfy a timed
  acquire;
- cache miss without an explicit timeout: historical `open` path;
- cache miss with an explicit timeout: `open_with_policy` is required;
- missing capability fails before provider publication.

The cache key remains the normalized document URI. Timeout values do not create
parallel cache identities.

## Publication and lifetime

A successful provider lease is copied into store-owned immutable bytes and
closed exactly once before compilation. Compile failure, byte-limit failure,
or later cache-admission failure publishes no cache row.

The ordinary `vxml_document_store_acquire()` delegates with no policy and
therefore preserves historical behavior.

## CHTTP bridge

The current `scxml_chttp_resource` owns an immutable configured
`timeout_ms` and exposes no safe per-call override. The VoiceXML bridge
therefore does not pretend to support arbitrary request deadlines.

Its `open_with_policy`:

- delegates to ordinary `open` when `has_timeout == false`;
- fails closed with `SCXML_RESOURCE_FAILED` when an explicit timeout is
  requested;
- performs no resolver/network admission on that failure.

A future CHTTP per-request deadline API can replace that fail-closed branch
without changing the VoiceXML adapter contract.
