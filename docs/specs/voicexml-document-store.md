# VoiceXML document store

`TurboSCXML::VoiceXMLDocumentStore` is the bounded URI-resolution and
immutable document/program cache used by multi-document VoiceXML features.

It is transport-independent. The store consumes the same
`vxml_dialog_document_adapter_v1` boundary used by
`VoiceXMLDialogManager`, so CHTTP, memory, packaged resources, or another
authorized provider share one cache/compile path.

## Resolution model

The store owns one copied absolute application URI. Callers resolve a reference
relative to either:

- the current leaf document URI; or
- the application URI when no current document is supplied.

Resolution produces two caller-owned outputs:

```text
reference
   |
   +-- normalized fetch URI (fragment removed)
   +-- fragment bytes (without '#')
```

Absolute, network-path, absolute-path, relative-path, query-only, empty, and
fragment-only references follow the hierarchical RFC3986 resolution model.
Path dot segments are removed. Scheme/authority/query bytes are otherwise kept
as policy identity; the store does not silently lowercase, decode percent
escapes, or reinterpret a URI.

Fragments never enter the provider fetch/cache key. Two references to
`dialog.vxml#a` and `dialog.vxml#b` therefore share one immutable source and
one compiled `vxml_program`.

## Cache ownership

Each occupied row owns:

- normalized document URI bytes;
- copied immutable source bytes;
- one compiled `vxml_program`;
- one nonzero row generation.

Cache capacity bounds rows. `max_cache_bytes` bounds copied URI+source bytes
across rows. The configured `vxml_limits` independently bounds compiled
program storage.

On a cache miss:

```text
provider open
  -> copy source under byte bound
  -> provider close exactly once
  -> vxml_compile from owned bytes
  -> evict unpinned LRU rows if required
  -> atomically publish new cache row
```

Provider or compile failure publishes no partial row.

## Borrow protocol

A document reference carries both:

- cache entry slot + generation; and
- borrow slot + generation.

The store has a fixed borrow table equal to cache capacity. Every acquire owns
one unique borrow row, including cache hits. Releasing a copied stale reference
cannot decrement another live borrow of the same document.

Pinned entries are never evicted. If no cache row or borrow row can be safely
reused, acquire returns `VXML_DOCUMENT_STORE_FULL` before provider I/O when
possible.

`vxml_document_store_view()` exposes borrowed URI/source/program pointers only
while that exact borrow reference remains live.

## Eviction and shutdown

Eviction is least-recently-used among unpinned entries. Row reuse increments
the entry generation, making old references stale.

`clear()` removes every unpinned entry and reports BUSY when any borrow
remains. `destroy()` also refuses while a borrow is live.

The store is synchronous/single-owner. Callers serialize operations; it does
not create a worker, lock, network client, or hidden progress loop.

## Language integration

This slice does not yet change VoiceXML syntax. Later #48 slices use this owner
for:

- document/dialog `goto`;
- application/leaf scope transitions;
- VoiceXML 2.1 `data`;
- external script acquisition for explicit script profiles;
- bounded navigation generations and cache policy.

The transport layer remains separate: `VoiceXMLCHttpResource` is one optional
document provider, not part of the document store itself.
