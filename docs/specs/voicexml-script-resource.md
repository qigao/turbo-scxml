# VoiceXML external script resource boundary

This component provides transport-neutral external script acquisition for
future explicit scripting profiles. It deliberately does not execute
ECMAScript or modify VoiceXML/CMeta state.

## Boundary

`TurboSCXML::VoiceXMLScriptResource` depends only on
`TurboSCXML::VoiceXMLDocumentStore`.

```text
script profile / manifest
        |
        | base document URI + script reference
        v
VoiceXMLScriptResource
        |
        +-- DocumentStore URI resolver
        |
        v
vxml_script_resource_adapter_v1
        |
        +-- package provider
        +-- authorized CHTTP bridge (future/optional)
        +-- test/memory provider
```

The base VoiceXML and CMeta runtimes do not accept a script and then silently
ignore it. A later explicit QuickJS profile is responsible for scope and
execution semantics.

## Request

`vxml_script_request_v1` contains:

- current/base document URI;
- script URI reference;
- optional charset;
- maximum resolved URI bytes;
- maximum source bytes.

Empty charset means UTF-8. This initial bounded profile accepts UTF-8
case-insensitively and fails closed for unsupported charset before provider
admission. UTF-16 decoding remains an explicit follow-up rather than being
approximated.

The script reference is resolved with the same hierarchical URI rules as
VoiceXML documents. A fragment is rejected because this boundary acquires one
complete source resource.

## Provider lifetime

A provider open returns immutable bytes plus one non-null lease. The caller
borrows those bytes until `vxml_script_resource_close()`.

- provider failure publishes no caller-visible lease;
- a provider that accidentally publishes a lease on failure is closed by the
  boundary;
- source body overflow closes the successful lease before returning;
- malformed successful source descriptors close any live lease;
- close clears the caller handle;
- closing an already-empty handle is an idempotent success.

No cache, worker, timer, transport client, or script engine is owned by this
component.
