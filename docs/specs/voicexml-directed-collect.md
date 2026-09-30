# VoiceXML directed field collect admission

This document describes the first Directed FIA slice delivered by the CMeta
profile. It intentionally stops before recognition completion and `filled`
processing.

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

## Deferred to the next #45 slice

Recognition completion, copied bounded completion ingress, typed semantic-slot
assignment, stale-completion rejection, `filled` handlers and return-to-SELECT
processing are intentionally not approximated in this slice.
