# VoiceXML explicit external script descriptor profile

This profile adds static external-script acquisition metadata without adding
ECMAScript execution to base VoiceXML or CMeta.

## Admission boundary

Base `vxml_compile()` remains fail-closed for `script`.

The `VoiceXMLScriptResource` component exposes:

```c
vxml_compile_external_script_profile(...)
```

which enables only the static external-script compiler feature.

Accepted first slice:

```xml
<script src="scripts/app.js" charset="UTF-8"/>
```

Rules:

- `src` is required and copied into immutable Program storage;
- charset defaults/canonicalizes to UTF-8;
- UTF-16/other charset values fail closed;
- `src` plus inline content is invalid;
- inline-only script execution is unsupported in this profile;
- `srcexpr` remains #50.

## Runtime handoff

Executing the descriptor moves the literal Session to
`VXML_SESSION_SCRIPTING` and exposes:

```c
vxml_external_script_target_v1 {
    src,
    charset
}
```

Both views borrow Program-owned bytes.

The caller combines this descriptor with its current document URI and calls the
already-delivered `vxml_script_resource_acquire()`.

```text
Program-owned script descriptor
       |
       v
VXML_SESSION_SCRIPTING
       |
       v
VoiceXMLScriptResource
       |
       +-- DocumentStore RFC3986 URI resolution
       +-- charset/source bounds
       +-- provider source lease
       |
       v
explicit script execution profile
```

This slice stops before the final box: it does not execute JavaScript and does
not retain provider-owned source pointers in the Session/Program.

## Lifetime

A successful resource acquire returns one provider lease. The execution
profile must consume/copy/compile the callback-visible source before calling
`vxml_script_resource_close()`.

Provider failure, oversized output, invalid URI or unsupported charset follows
the existing ScriptResource fail-closed/close-exactly-once rules.

## Dependency boundary

Base `TurboSCXML::VoiceXML` still links only `Salts::XmlParser`.

The explicit compiler is exported from
`TurboSCXML::VoiceXMLScriptResource`, which already links
`TurboSCXML::VoiceXMLDocumentStore`. QuickJS and CHTTP are not added to the
VoiceXML core dependency graph.

Dynamic `srcexpr`, JavaScript scope effects, instruction/memory/stack/
wall-clock limits and actual QuickJS execution are #50 work.
