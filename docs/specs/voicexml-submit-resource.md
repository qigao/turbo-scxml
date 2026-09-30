# VoiceXML one-attempt submit resource boundary

This component provides the transport-neutral resource transaction used by the
VoiceXML `submit` language/runtime layers. It owns no HTTP client and never
retries a request.

## Contract

`vxml_submit_resource_execute()` receives:

- current document URI and literal/resolved target;
- GET or POST;
- ordered bounded name/value fields;
- POST enctype;
- URI/body/response hard limits;
- one provider adapter.

The target is resolved with the existing VoiceXML DocumentStore RFC3986
resolver. Fragments are retained separately from the wire document URI.

## Encoding

The first slice supports
`application/x-www-form-urlencoded`.

Fields preserve caller order. Names and values are UTF-8 byte views and are
encoded deterministically:

- unreserved bytes remain literal;
- space becomes `+`;
- every other byte is percent-encoded with uppercase hexadecimal;
- fields are joined with `&`;
- name and value are joined with `=`.

Empty field lists are valid. Empty names and duplicate names are rejected
before provider admission.

For GET, the encoded form is appended to the resolved URI query using `?` or
`&` as appropriate. For POST, the encoded form is the request body and the
content type is exactly `application/x-www-form-urlencoded`.

## One-attempt rule

The provider `execute` callback is called at most once for each public
`vxml_submit_resource_execute()` invocation.

There is intentionally no retry loop. In particular,
`VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED` means a POST may have reached the
peer and is returned unchanged. A later retry requires a new language-level
submit action or a future explicit idempotency contract.

## Response lease

On success the provider returns one lease containing VoiceXML bytes, exact
VoiceXML media type, and optional effective URI.

TurboSCXML validates the response byte bound, MIME type, URI bytes, and lease
shape. Any validation failure closes a published lease exactly once.

A provider failure that accidentally publishes a lease is also closed before
return. A successful lease remains caller-owned until
`vxml_submit_resource_close()`, which clears the handle.

## Dependency boundary

`TurboSCXML::VoiceXMLSubmitResource` links only
`TurboSCXML::VoiceXMLDocumentStore`.

The component has no CHTTP dependency. An optional transport bridge can be
added separately without changing this one-attempt contract.

Multipart/form-data is intentionally deferred to the recorded-audio/#49
boundary.
