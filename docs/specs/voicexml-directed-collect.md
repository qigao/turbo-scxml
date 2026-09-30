# VoiceXML directed field collect admission

This document describes the delivered Directed FIA collect admission and
fixed-scalar completion slices for the CMeta profile. `filled` processing
remains a later slice.

## Supported form shape

The first slice supports either the existing block-only form profile or a
field-only directed form:

```xml
<form id="order">
  <var name="flag" expr="true"/>
  <field name="first" cond="flag">
    <grammar type="application/srgs+xml" src="first.grxml"/>
  </field>
  <field name="second">
    <grammar type="application/srgs+xml" src="second.grxml"/>
  </field>
</form>
```

Field and block form items do not mix in this slice. That restriction is
deliberate: full mixed form-item document ordering belongs to the completion /
process phase of #45.

## Immutable compile model

Each field becomes one immutable CMeta row containing:

- owning form index;
- exact application-root CMeta field index, offset and semantic descriptor;
- copied field name;
- optional compiled boolean condition;
- copied grammar media type and source URI;
- required provider capability mask.

Only literal `application/srgs+xml` grammar descriptors are admitted here.
No grammar fetch, parse, ASR backend or media device exists in this layer.

## SELECT

After transactional document/form declaration initialization, the CMeta
session scans field rows in document order.

A field is skipped when:

- its bound application-root field is already defined; or
- its optional condition evaluates false.

The first remaining field becomes the active field and receives one nonzero
collect generation. If no field is eligible, the form exhausts normally.

The selected request borrows Program-owned name/type/source bytes. Those views
remain valid while the Program outlives the Session.

## Collect admission

The host explicitly drives provider admission:

```text
vxml_session_start()
    -> vxml_session_cmeta_collect_request()
    -> vxml_session_cmeta_collect_prepare()
         provider reservation only
    -> vxml_session_cmeta_collect_commit()
       OR
       vxml_session_cmeta_collect_discard()
```

The adapter is a borrowed versioned C contract. Capability mismatch is checked
before `prepare()` and therefore cannot publish a provider reservation.

A successful provider prepare returns a no-fail commit/discard ticket. The
ticket is stored inside the Session rather than exposed as a callback carrying
a Session pointer; this avoids stale-ticket use-after-destroy hazards.

Only one reservation or committed generation may exist at a time.

## Shutdown

Session close/destruction settles provider state before releasing CMeta session
storage:

- a prepared but uncommitted ticket is discarded;
- a committed generation invokes the provider's no-fail/nonblocking
  `cancel(generation)`.

The adapter and provider user remain host-owned and must outlive any Session
that borrows them.

## Completion ingress and owner progress

After provider commit, the Session arms one fixed mailbox with the active
collect generation and exact selected-field CMeta descriptor.

Completion admission is separate from semantic mutation:

```text
provider/host completion producer(s)
    |
    | generation + exact cmeta_data_desc* + native object
    v
vxml_session_cmeta_collect_try_complete()
    |
    +-- MPSC one-slot admission
    +-- fixed byte copy only
    +-- FULL / CLOSED / STALE / INCOMPATIBLE_RESULT
    |
    v
READY mailbox
    |
    | exactly one Session owner
    v
vxml_session_cmeta_collect_run_ready()
    |
    +-- CMeta transaction begin
    +-- semantic copy into staged root field
    +-- transaction commit
    +-- disarm generation
    +-- Directed FIA SELECT
```

The mailbox accepts only exact fixed scalar CMeta descriptors:
`BOOL`, `SINT`, `UINT`, and `FLOAT`. The native object is copied using
the descriptor's fixed `storage_type->size` into preallocated aligned
Session storage. No string, bytes, aggregate, or provider-owned view is retained
by this slice.

A wrong descriptor or stale generation does not claim the slot. A second
producer after one accepted completion sees `FULL`; no overwrite or drop is
performed.

The Session must outlive concurrent `try_complete` calls. Lifecycle
operations are control-plane operations: stop/cancel producers before destroy.
Exactly one owner thread calls `run_ready`, close, and destroy.

A successful progress turn makes the selected field defined before scanning the
form again. The next undefined/true-cond field receives a new nonzero
generation. If no field remains eligible the form exits normally.

## Completion shutdown

Close/destruction first closes completion admission, then settles provider
ownership:

- prepared tickets are discarded;
- committed generations are cancelled through the existing no-fail provider
  cancel callback;
- any copied but unprocessed fixed-scalar completion is simply discarded with
  Session-owned mailbox storage.

The provider must treat cancel of an already-completed generation as an
idempotent settlement.

## Deferred to the next #45 slice

Field/form `filled` handlers, multi-slot recognition results, managed
STRING/BYTES/aggregate semantic payloads, noinput/nomatch Events and prompt
media remain deferred. Managed results require an independent retained-byte
budget and are not approximated by unbounded copies.
