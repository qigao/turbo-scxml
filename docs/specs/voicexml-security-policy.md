# VoiceXML security and sensitive-data policy

## Scope

This document defines the security boundary owned by TurboSCXML's VoiceXML
runtime and its provider interfaces.

TurboSCXML is a language/runtime library. It does **not** own application
network trust policy, authorization, TLS configuration, redirect policy,
credential storage, or a global logging backend. Those concerns stay with the
application and the configured document/script/data/submit/media providers.

The core rule is:

> VoiceXML runtime control flow may expose bounded status, Event names and
> structural diagnostics, but it must not create a default payload/body/
> recording logging channel.

## Sensitive data classes

Treat all of the following as potentially sensitive:

- VoiceXML document source;
- prompt text, SSML and audio resource references;
- recognition inputs/results and menu speech values;
- CMeta variables, namelist values and external-data values;
- fetched document/data/script response bodies;
- QuickJS source, expression results and exception details;
- submit scalar values and encoded request bodies;
- recording payload bytes, recording metadata and recorded-utterance results;
- provider/application credentials or authorization context.

Provider applications may have stronger classifications. TurboSCXML does not
downgrade those classifications.

## Logging and diagnostics

### Runtime

The VoiceXML runtime has no generic stdout/stderr logger and no default
`SALTS_LOG` path.

Runtime failures are represented by:

- `vxml_status`;
- fixed/bounded VoiceXML Event names;
- provider result enums;
- generation-safe ingress results.

Runtime QuickJS failures map to status plus fixed Events such as
`error.semantic`; external-script evaluation does not publish the engine
exception string through the Session API. The QuickJS runtime regression test
throws a distinctive sensitive sentinel and still exposes only the fixed
`error.semantic` Event.

Fetched document/data/script bodies, submit field values and recording bytes
must not be written by TurboSCXML to stdout/stderr or a default log sink.

A source-level CI contract enforces that `src/voicexml*.c` does not introduce
direct stdio output calls or SALTS logging macros.

### Compile diagnostics

`vxml_diagnostic` is different from runtime logging.

Compile diagnostics are caller-requested, bounded by
`VXML_DIAGNOSTIC_CAPACITY`, and may include parser or expression-engine
syntax information. Because syntax diagnostics can reveal source-related
information, applications **must treat compile diagnostics as sensitive** and
must decide explicitly whether/how to persist or expose them.

TurboSCXML does not promise that compile diagnostics are redacted source.

## URI, transport and trust policy

The core runtime does not invent an allow-list for URI schemes, hosts or
redirect destinations.

Ownership is:

| Concern | Owner |
| --- | --- |
| URI syntax/resolution bounds | DocumentStore / resource boundary |
| allowed scheme/host/port | application/provider |
| DNS/proxy/redirect behavior | transport provider |
| TLS versions/certificates | transport provider |
| authorization/cookies/tokens | application/provider |
| timeout/fetch policy projection | VoiceXML Session/hand-off |
| network enforcement | configured provider |

The optional CHTTP adapters do not weaken this separation. They adapt the
VoiceXML resource contracts to the installed CHTTP package; they do not move
transport policy into VoiceXML core.

## External scripts / QuickJS

QuickJS is opt-in.

The supported hardening contract includes:

- bounded heap;
- bounded stack;
- non-blocking runtime setup;
- monotonic evaluation deadline;
- bounded source/result/conversion storage;
- fresh execution context for each external script activation;
- transactional typed-state import/export.

The pinned QuickJS ABI does not provide a deterministic bytecode-instruction
counter, so TurboSCXML does not claim one.

External script source comes from ScriptResource. A successful resource lease
is closed exactly once. Runtime exception text is not exposed through the
Session event API.

## Data resources

CMeta external data:

- provider bytes are bounded;
- typed decoding is performed through DataBind;
- publication is transactional;
- a failed decode does not publish partial typed state;
- provider leases close exactly once.

QuickJS no-DOM data:

- performs a bounded provider request;
- does not bind a response DOM/object;
- discards response bytes after settlement;
- exposes fixed runtime Events/status rather than response-body diagnostics.

Transport/provider code must not assume response bodies are safe to log.

## Submit and recordings

SubmitResource performs exactly one provider attempt per language-level
submission.

For POST:

- TurboSCXML never retries after provider admission;
- `POSSIBLY_PROCESSED` is terminal;
- scalar fields are callback-borrowed from bounded Session-owned snapshots;
- multipart recording bytes are callback-borrowed directly from the
  Session-owned recording lease;
- SubmitResource and DialogManager do not release or re-own the recording
  lease;
- no multipart fallback concatenates the recording payload into a hidden
  unbounded copy.

Recording payload bytes must not appear in generic diagnostics/logging.

Recording ownership remains with the CMeta Session until its documented
replacement/clear/close/destroy point.

## Prompt and recognition providers

Prompt text, speech recognition values and recording results cross explicit
provider boundaries.

Providers may internally log data; that is outside TurboSCXML core and must be
governed by the embedding application's policy.

TurboSCXML-owned diagnostics should use stable status/Event metadata rather
than interpolating prompt/recognition payload values.

## Provider error strings

Provider APIs that accept or return diagnostic/error strings use bounded
callback-borrowed views.

Embedding providers must not use those fields as an uncontrolled channel for
credentials, request/response bodies or recording data.

TurboSCXML should prefer stable Event/status codes for public runtime outcomes.

## Source-level no-output contract

`tests/voicexml/validate_security_policy.cmake` scans
`src/voicexml*.c` and fails if VoiceXML implementation files introduce:

- `printf`, `fprintf`, `vprintf`, `vfprintf`;
- `fwrite`, `puts`, `perror`;
- `SALTS_LOG` or `salts_log` calls/macros.

Formatting into bounded local/diagnostic buffers via `snprintf` is allowed;
that is not an output sink.

If VoiceXML later gains an explicit application logging API, this contract must
be deliberately redesigned together with redaction tests rather than bypassed.

## Security review checklist for new features

Before merging a feature that handles external or user-derived bytes:

1. identify the byte owner and maximum size;
2. state whether bytes are copied or borrowed;
3. define exact lease/ticket settlement;
4. define stale/full/closed behavior for async ingress;
5. ensure provider refusal happens before partial publication where required;
6. ensure runtime errors expose stable status/Event metadata rather than
   payload values;
7. ensure POST/side-effecting operations do not introduce hidden retry;
8. update the VoiceXML support matrix and security policy when the trust
   boundary changes.

## Related evidence

- canonical architecture:
  `docs/specs/voicexml-architecture-design.md`
- support matrix:
  `docs/specs/voicexml-support-matrix.md` (after #278)
- hardening umbrella: #51
- fuzz/regression corpus: #276
- supported option-matrix CI: #279
