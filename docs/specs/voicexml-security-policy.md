# VoiceXML security and sensitive-data policy

## Status

This document defines the security boundary of TurboSCXML's incubating
VoiceXML runtime. It does not claim to provide network authorization, TLS
policy, host allow-lists, redirect policy, TTS/ASR isolation, SIP/RTP security,
or a browser security model.

TurboSCXML owns bounded language/runtime state and explicit provider handoffs.
Applications and providers own the trust decisions around external resources.

## Sensitive data

Treat the following as sensitive by default:

- prompt text and SSML;
- recognition values and application variables;
- document, data and external-script response bodies;
- JavaScript source, results and thrown exception payloads;
- submit scalar values and encoded request bodies;
- recording bytes and recorded-utterance content;
- provider-specific diagnostic strings;
- URIs that may contain credentials, query tokens or application identifiers.

TurboSCXML must not emit these payloads through generic diagnostics or logs by
default.

## Safe diagnostic metadata

TurboSCXML-owned diagnostics may expose bounded structural metadata such as:

- stable status / Event category;
- XML source location;
- expression or path byte offset;
- configured limit category;
- generation/state classification;
- protocol status code when it is already part of the public provider result;
- byte counts or media type when explicitly documented as metadata.

A diagnostic must not interpolate the corresponding payload merely because the
payload was available while producing that metadata.

## Logging policy

Production VoiceXML sources currently contain no direct `SALTS_LOG`,
`printf` or `fprintf` payload logging path.

The default rule is:

```text
payload bytes
   -> never generic log/diagnostic text

status + bounded metadata
   -> allowed when useful for diagnosis
```

Tests may print diagnostics on assertion failure, but the diagnostics
themselves must already obey this policy.

## QuickJS

QuickJS is opt-in and uses the private hardened sandbox:

- fresh contexts for supported activation paths;
- bounded heap and stack;
- non-blocking runtime;
- monotonic wall-clock deadline;
- no host `fetch`, filesystem, process, `Function` constructor or `eval`
  surface in the supported sandbox contract;
- transactional typed-state publication.

Raw JavaScript exception text is **not a safe diagnostic channel**. A script
can throw application data deliberately or accidentally.

TurboSCXML therefore converts non-deadline JavaScript exceptions to the fixed
diagnostic:

```text
QuickJS exception
```

The exception value is consumed and released but not stringified into the
caller-visible diagnostic. Deadline failures retain the fixed
`QuickJS evaluation deadline exceeded` category.

## Provider errors

CMeta provider `prepare` callbacks have a historical
`const char **out_error` detail channel. Provider implementations may place
arbitrary application text there, so TurboSCXML treats that text as sensitive
and untrusted.

Public Session prepare wrappers do **not** forward the provider string.
They expose only fixed category strings on provider refusal:

- `VoiceXML subdialog provider error`;
- `VoiceXML record provider error`;
- `VoiceXML transfer provider error`;
- `VoiceXML collect provider error`;
- `VoiceXML prompt media provider error`.

Capability/shape failures detected before provider admission leave
`out_error` empty and preserve their normal typed status/Event result.

This is deliberately lossy. Applications that need provider-specific
telemetry should handle it inside the provider's own logging/observability
boundary, where redaction and authorization policy can be applied explicitly.

## URI, host, redirect and transport policy

TurboSCXML core owns URI syntax, resolution and configured byte bounds where
required by the language/runtime contract. It does **not** decide which
schemes, hosts, ports, redirects, certificates, credentials or authentication
headers are trustworthy.

Those decisions belong to the configured document/script/data/submit provider
or to the application that creates it.

Consequences:

- no core-wide network allow-list is invented;
- no silent HTTP fallback is added;
- CHTTP adapters must preserve the provider/application policy they are given;
- TLS/auth/redirect policy remains outside VoiceXML language semantics;
- applications should avoid embedding secrets in URIs because URI bytes are
  part of resource identity even though TurboSCXML does not print them in
  generic diagnostics.

## Resource-body policy

### Documents

DocumentStore copies bounded source bytes into its cache and owns that copy for
the cached Program lifetime. Generic error records carry status categories, not
document body bytes.

### Data

CMeta DataBind may decode provider bytes into typed state. QuickJS no-DOM
`<data>` deliberately discards response bytes. Neither path places the
response body in generic diagnostic text.

### Scripts

ScriptResource leases source bytes for bounded execution. Script bodies and
exception payloads are not emitted by generic VoiceXML diagnostics.

### Submit

Submit scalar values are Session-owned bounded snapshots. Multipart recording
parts borrow Session-owned recording payload bytes. SubmitResource encodes or
streams the request but does not log request values and does not retry POST
after provider admission.

`POSSIBLY_PROCESSED` is a status category, not a reason to copy request
payload into diagnostics.

### Recordings

Recording payloads are sensitive Session-owned leases. Generic transport
components may borrow the bytes only through explicit recording views and must
never release, duplicate for diagnostics, or stringify them.

Media type, byte length and duration may be exposed where their public result
contract explicitly defines them as metadata; the payload remains excluded.

## Prompt and recognition data

Prompt text, SSML and recognition values may carry user or application data.
Provider adapters receive those views because execution requires them, but
TurboSCXML-owned generic error strings do not reproduce them.

Providers are responsible for any external logging they perform. Applications
should configure provider logging with an explicit redaction policy.

## Ownership and settlement

Security depends on lifetime correctness as well as redaction:

- document/script/data/submit response leases close exactly once;
- recording leases remain owned by the CMeta Session;
- multipart submission only borrows recording bytes;
- stale generation completion cannot mutate a newer activation;
- closed ingress is rejected;
- provider tickets are committed/discarded/cancelled according to the
  documented single-owner lifecycle.

No failure path should retain sensitive bytes merely to improve a later error
message.

## Executable redaction witnesses

The security contract is directly qualified by tests including:

- `tests/quickjs_sandbox_test.c :: "redacts thrown exception payloads from diagnostics"`;
- `tests/voicexml/test_voicexml_cmeta_session.c :: "owns typed and literal subdialog parameters across discard retry and cancel"`
  (provider refusal asserts the public error is the fixed redacted category);
- `tests/voicexml/test_voicexml_cmeta_session.c :: "moves one accepted recording lease without copying payload bytes"`;
- `tests/voicexml_submit_resource_test.c :: "preserves explicit global text-recording-text multipart order"`;
- `tests/voicexml_submit_resource_test.c :: "surfaces multipart POSSIBLY_PROCESSED after exactly one borrowed attempt"`.

## Audit checklist

When adding a new VoiceXML feature or provider surface:

1. classify every input/output byte view as payload or metadata;
2. keep payload ownership explicit and bounded;
3. do not interpolate payload into generic diagnostics;
4. do not forward provider-controlled diagnostic text by default;
5. add a negative redaction witness if the feature can observe sensitive data;
6. keep transport authorization outside core language semantics;
7. preserve exact lease/ticket settlement on every failure path.

The current support surface is tracked by
`docs/specs/voicexml-support-matrix.md`; the high-level ownership model is in
`docs/specs/voicexml-architecture-design.md`.
