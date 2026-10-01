# VoiceXML subdialog parameter snapshot contract

This slice adds the parent-side parameter/admission boundary for static CMeta
`subdialog` form items. It does not fetch or start the child document; the
DocumentStore-backed owner is a later slice.

## Flow

```text
FIA SELECT subdialog
        |
        v
active_subdialog + generation
        |
        v
prepare()
  -> evaluate param@expr against committed parent scopes
  -> copy scalar values
  -> deep-copy STRING values
  -> copy decoded param@value literal bytes
  -> copy parameter names
        |
        v
parent Session-owned snapshot
        |
        v
adapter.prepare(request) -> ticket
        |
        +-- discard: release provider admission only
        |             snapshot stays frozen for this generation
        |
        +-- commit: child generation becomes in-flight
        |
        +-- destroy/cancel: provider cancellation + snapshot release
```

No parent scope pointer, root-storage pointer, expression scratch view, or
temporary decoder buffer is published to the child owner.

## Parameter forms

Each `param` requires a static `name` and exactly one source:

- `expr`: evaluated through the bounded CMeta expression runtime and tagged
  `VXML_CMETA_SUBDIALOG_PARAM_TYPED`.
- `value`: decoded UTF-8 bytes copied verbatim and tagged
  `VXML_CMETA_SUBDIALOG_PARAM_LITERAL`.

Typed BOOL/SINT/UINT/FLOAT values are copied by value. Typed STRING bytes are
deep-copied into snapshot storage. A typed STRING is never reinterpreted as a
literal child value.

## Bounds

Compile-time limits cover:

- subdialog count and URI bytes;
- parameter count per document/profile;
- decoded parameter name bytes;
- literal / typed STRING bytes per parameter.

The Session `max_subdialog_snapshot_bytes` bound covers the public parameter
array plus all copied names/literals/STRING data for one active generation.
Overflow, evaluation failure, allocation failure, and duplicate parameter
names all fail before the child-owner callback.

## Admission lifetime

A successful `prepare` returns a no-fail commit/discard ticket.

- `discard` releases only provider admission. The parent keeps the frozen
  snapshot so a retry of the same generation cannot observe changed parent
  values or re-run expressions.
- `commit` transfers the generation to in-flight child ownership.
- Session destroy discards an uncommitted ticket or cancels a committed
  generation exactly once, then releases snapshot storage.

The snapshot is tagged with its generation; a different generation is rebuilt
from that generation's parent state.

## Child-side validation

The request intentionally preserves typed values versus raw literals. The
child owner must resolve the target child declaration/schema and perform exact
conversion before publishing the child Session. The built-in validation and
DocumentStore-backed owner are tracked by #187; RETURN completion is #186.
