# SCXML Structured Payload Closure Design

## Scope

This change closes two CMeta payload gaps without adding a second Event ABI:

- `<send target="#_internal" namelist="...">` and internal-send `<param>`
  children publish structured `_event.data`.
- `<donedata><content expr="path.to.struct"/></donedata>` publishes an owned
  structured completion value.

Scalar and inline content behavior, external adapter payload views, and the
existing `<donedata><param>` projection semantics remain unchanged.

## Representation

An internal named payload is a projection over the compiled CMeta root schema.
Admission resolves every payload name to a distinct root field and builds the
same bounded schema shape used by completion parameters. At execution, one
metadata row copies the root object, applies every payload expression to that
copy, and exposes only the selected fields through the projected schema.

A rich done-data content expression is restricted to a CMeta struct location.
At execution, the selected struct is copy-constructed into the existing
completion object slot and its own descriptor becomes the Event data schema.
The selected value is therefore the `_event.data` root: for example,
`<content expr="nested">` exposes `nested.value` as `_event.data.value`.
Event-data field operands retain their path and resolve it against the selected
Event's actual schema at evaluation time.

## Ownership and bounds

| Item | Contract |
|---|---|
| Authoritative internal Event data | The session metadata row reserved before the tagged internal Event is raised. |
| Internal named payload input | Expressions borrow staged state only for the executable callback. |
| Internal named payload output | The metadata row owns a copied root object until Event preprocessing releases it. |
| Rich done-data input | The selected object borrows staged state only while materializing completion data. |
| Rich done-data output | The completion slot owns an independent copy until completion Event preprocessing releases it. |
| Capacity | Objects must fit `SCXML_EVENT_DATA_CAPACITY` and its alignment; named payloads remain bounded by `SCXML_PAYLOAD_MAX_ENTRIES`. |
| Failure | A failed expression/copy publishes no partial object, releases its reserved row, and raises `error.execution`. |
| Shutdown | Existing metadata/completion slot drain and destroy paths remain the single release authority. |

No payload pointer crosses a callback or queue boundary without an owning copy.

## Validation

- Named internal payload fields must resolve to distinct writable root fields.
- Rich done-data content must resolve to a CMeta data descriptor with valid
  copy/destroy traits and storage that fits the completion slot.
- A literal immediate `#_internal` send remains entirely session-local and does
  not advertise Event I/O or adapter payload requirements.
- Late-bound data must be initialized before either source view is copied.
- Existing internal delayed-content rejection remains in force.

## Verification

Tests cover named internal payload observation through `_event.data`, source
projection and expression materialization, plus structured done-data observation
through the real parent completion Event. The complete preset regression covers
the existing scalar/content failure, ownership, and session shutdown paths.
