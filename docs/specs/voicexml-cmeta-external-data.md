# VoiceXML CMeta external data

This slice implements bounded VoiceXML 2.1 external `<data>` acquisition for
the explicit CMeta datamodel profile.

## Scope

Supported form:

```xml
<vxml xmlns="http://www.w3.org/2001/vxml"
      version="2.1"
      datamodel="cmeta">
  <data name="config" src="config.json"/>
  ...
</vxml>
```

The declaration is document-level and must precede forms. `name` must name one
top-level field of the application-root CMeta struct. Arbitrary DOM objects,
ECMAScript object identity, XPath and implicit type coercion are out of scope.

## Control plane

Compilation performs all schema admission needed by the runtime:

1. decode and validate `name` / `src`;
2. bind `name` to the exact application-root field descriptor and offset;
3. require canonical CMeta semantic move support;
4. compile one immutable `DataBindNativePlan` for that field;
5. retain exact decode-workspace/alignment requirements;
6. retain the configured DataBind depth/item admission limits.

The Program owns each NativePlan and frees it on every normal and partial
destruction path.

No runtime descriptor graph search or parser registry lookup is used.

## Runtime provider

`vxml_cmeta_data_resource_adapter_v1` is a synchronous, versioned acquisition
boundary. A successful open returns:

- byte-counted raw data;
- one explicit format: JSON, YAML, CSV or XML;
- one provider-owned lease.

VoiceXML never guesses the format from the URI, extension, Content-Type or
payload.

The provider remains responsible for URI policy, networking, deadlines,
authorization and transport caching. A future CHTTP adapter can project those
policies without changing the CMeta runtime.

## Decode and publication

Session initialization performs:

```text
provider open
  -> explicit DataBindFormatProvider
  -> CSerde reader
  -> precompiled DataBindNativePlan
  -> semantic-zero staging value
  -> canonical CMeta semantic move
  -> committed application-root field
```

The format reader is closed before the provider lease is closed.

Decode failure restores staging and does not publish a session. The
caller-owned `initial_root` is borrowed/copy-in only and is never mutated.

Existing application-root state is replaced only after a complete decode.
Descriptors admitted for external data must expose the canonical no-fail
semantic move/restore lifecycle.

## Bounds

Compile options explicitly bound:

- number of external data declarations;
- retained data URI bytes;
- DataBind descriptor depth;
- DataBind item count.

Session options explicitly bound:

- raw payload bytes;
- aggregate provider-owned decoded bytes.

Native decode workspace and value staging are measured from immutable plans,
allocated once during session initialization, and reused across declarations.

## Dependency boundary

`TurboSCXML::VoiceXML` remains dependent only on `Salts::XmlParser`.

Only `TurboSCXML::VoiceXMLCMeta` carries the DataBind/CSerde closure required
for typed external data. JSON/YAML/CSV/XML adapters are explicit static
providers; there is no runtime format registry or fallback.

## Compatibility

The existing V1 compile/session option records are append-only size-versioned
records. Historical prefixes ending at `max_conditional_depth` and
`max_execution_steps` remain valid for documents that do not use
`<data>`.

A document containing `<data>` requires the new compile limits and session
resource-provider tail. Zero external-data compile limits keep the feature
disabled.
