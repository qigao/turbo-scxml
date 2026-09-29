# VoiceXML CHTTP document resource

`TurboSCXML::VoiceXMLCHttpResource` is a thin projection from the existing
`TurboSCXML::CHttpResource` text-resource profile to the VoiceXML dialog
manager's document-provider ABI.

It deliberately does **not** create a second HTTP stack.

## Composition

```text
VoiceXMLDialogManager
    |
    | vxml_dialog_document_adapter_v1
    v
VoiceXMLCHttpResource
    |
    | scxml_text_resource_adapter_v1
    v
TurboSCXML::CHttpResource
    |
    +-- logical URI resolver / authorization
    +-- CHTTP client
    +-- exact Content-Type admission
    +-- timeout / body-size limits
    +-- UTF-8 validation
    +-- response lease
```

The underlying `scxml_chttp_resource`, its resolver, CHTTP client and transport
remain caller-owned. They must outlive the VoiceXML bridge.

## Open / close lifetime

A successful VoiceXML document open delegates to
`scxml_chttp_resource_text_adapter()`.

The underlying response remains live until the matching VoiceXML document
close:

```text
document open
  -> CHTTP response acquired
  -> scxml_text_resource lease
  -> vxml_dialog_document lease
  -> VoiceXML compiler copies/consumes bytes
document close
  -> underlying text close
  -> CHTTP response destroyed
```

Only one active document lease exists per bridge because the underlying
single-owner CHTTP resource also permits only one active response. A second
open is rejected before another transport operation is admitted.

Destroying the VoiceXML bridge while a document is active returns
`VXML_CHTTP_RESOURCE_BUSY`. Destroy never destroys the borrowed underlying
CHTTP resource.

## Policy ownership

The bridge adds no URI or HTTP policy. The existing CHTTP resource remains
authoritative for:

- logical URI resolver admission;
- connection URI / authority / target bounds;
- scheme/target policy;
- exact expected Content-Type;
- HTTP status handling;
- request timeout;
- configured and caller response-body bounds;
- UTF-8 validity for text resources;
- active response lease cleanup.

Consequently policy changes belong in one place:
`TurboSCXML::CHttpResource`, not in VoiceXML.

## Diagnostics

The document-provider ABI reports the coarse manager result
`VXML_DIALOG_MANAGER_DOCUMENT_ERROR`.

For exact diagnosis the bridge records the most recent underlying
`scxml_resource_status`, available with
`vxml_chttp_resource_last_status()`. Examples include:

- `SCXML_RESOURCE_DENIED`;
- `SCXML_RESOURCE_TIMEOUT`;
- `SCXML_RESOURCE_NOT_FOUND`;
- `SCXML_RESOURCE_LIMIT_EXCEEDED`;
- `SCXML_RESOURCE_INVALID_DATA`.

A failed open never publishes a document lease.

## Package boundary

The component exists only when `TURBOSCXML_ENABLE_CHTTP_RESOURCE=ON`:

```text
TurboSCXML::VoiceXMLCHttpResource
    -> TurboSCXML::VoiceXMLDialogManager
    -> TurboSCXML::CHttpResource
    -> CHttp::Client
```

Neither `TurboSCXML::VoiceXML` nor
`TurboSCXML::VoiceXMLDialogManager` gains a CHTTP dependency.

This is slice 1 of issue #48. URI resolution/application scope, navigation,
cache, `data`, external script resources and `submit` build on this resource
boundary in later slices.
