# VoiceXML support and conformance evidence matrix

## Status

TurboSCXML VoiceXML support is **incubating and profile-specific**. This matrix
states the currently qualified surface; it is not a claim of full VoiceXML 2.0
or VoiceXML 2.1 conformance.

Normative references:

- VoiceXML 2.0: https://www.w3.org/TR/voicexml20/
- VoiceXML 2.1: https://www.w3.org/TR/voicexml21/
- CCXML dialog integration: https://www.w3.org/TR/ccxml/#dialogstart

The SCXML W3C manifest under `tests/w3c/` is **not VoiceXML evidence** and is
never used to promote a row in this matrix.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| **SUPPORTED** | The listed profile/contract is implemented and has executable positive and negative evidence. |
| **PARTIAL** | A deliberate subset is implemented; the restriction is explicit and fail-closed. |
| **UNSUPPORTED** | TurboSCXML deliberately does not implement the surface and does not approximate it. |
| **N/A** | The construct is outside the named profile/provider boundary. |

A SUPPORTED row means only the exact profile and provider contract stated in the
row. Provider availability, media engines, network policy, TTS/ASR/SIP/RTP and
application authorization remain external.

## Executable evidence matrix

Witness notation is `path :: "test name"`.

| Surface | Version | Profile / provider | Status | Positive executable witness | Negative / fail-closed witness | Standards / restriction |
| --- | --- | --- | --- | --- | --- | --- |
| document / form / block / exit | 2.0 | Base | **SUPPORTED** | `tests/voicexml/test_voicexml_program.c :: "compiles explicit exit and empty blocks into ordered rows"` | `tests/voicexml/test_voicexml_program.c :: "requires at least one form and one block per form"` | VoiceXML 2.0 core dialog structure |
| named form entry / fragment handoff | 2.0 | Base | **SUPPORTED** | `tests/voicexml/test_voicexml_session.c :: "starts a literal session at one named form for external fragment handoff"` | `tests/voicexml/test_voicexml_session.c :: "fails a start-at-form request when the compiled form ID is absent"` | VoiceXML 2.0 form identity |
| local and external `goto` | 2.0 | Base + DocumentStore for external documents | **SUPPORTED** | `tests/voicexml/test_voicexml_session.c :: "follows local goto with fetchaudio without entering fetch navigation"`; `tests/voicexml_dialog_manager_test.c :: "V3 honors an initial source fragment before any external navigation"` | `tests/voicexml/test_voicexml_program.c :: "rejects self and multi-form literal goto cycles before publication"` | VoiceXML 2.0 control transfer; external resource ownership is provider-backed |
| literal GET/POST urlencoded `submit` | 2.0 | Base handoff + SubmitResource | **SUPPORTED** | `tests/voicexml/test_voicexml_program.c :: "retains one POST urlencoded submit target in immutable Program storage"`; `tests/voicexml_submit_resource_test.c :: "encodes one POST body and exact urlencoded content type"` | `tests/voicexml/test_voicexml_program.c :: "rejects unsupported literal submit shapes before publication"` | VoiceXML 2.0 submit; one provider attempt, no implicit POST retry |
| typed variables / assign / clear / conditional executable content | 2.0 | CMeta | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_program.c :: "lowers typed declarations actions and block metadata"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "takes the first matching branch and declares only executed vars"` | `tests/voicexml/test_voicexml_cmeta_program.c :: "rejects unknown incompatible and non Boolean semantics"` | CMeta is an explicit typed profile, not ECMAScript emulation |
| directed `field` + `filled` FIA | 2.0 | CMeta + collect provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_program.c :: "compiles one directed field and literal SRGS grammar into immutable rows"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "runs field filled against the newly staged collect value"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects collect capability mismatch before calling the provider"` | Recognition engine is external and capability-gated |
| scoped catch / throw / rethrow / help / noinput / nomatch / reprompt | 2.0 | CMeta | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "selects the innermost most-specific catch deterministically"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "tracks noinput and nomatch independently and publishes reprompt once"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "turns an uncaught Event into a stable Session failure"` | VoiceXML event/recovery subset with bounded retry counters |
| prompt text / audio fallback / static SSML | 2.0 | CMeta + prompt/media provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "reserves one mixed TEXT AUDIO TEXT prompt batch in exact document order"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "projects one static SSML prompt and enforces SSML capability"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "does not call the media provider for no prompt capability mismatch or refusal"` | TurboSCXML projects media; it is not a TTS/audio engine |
| prompt marks / `mark@nameexpr` / timing result | 2.1 | CMeta + prompt/media provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "evaluates mark nameexpr when the selected prompt is queued and keeps that generation-owned name"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "publishes timed dynamic mark results on completion and snapshots filled fields"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "does not fabricate mark timing from V1 terminal observations"` | VoiceXML 2.1 mark additions; provider-reported timing only |
| prompt `foreach` | 2.1 | CMeta | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "expands nested foreach in outer-major order and retains distinct outer item state"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects nested snapshot arena overflow before provider publication"` | Bounded shallow typed snapshots; not arbitrary JS object iteration |
| static grammar and dynamic `grammar@srcexpr` | 2.0 / 2.1 | CMeta + collect provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "reevaluates field grammar srcexpr for every activation"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects invalid dynamic grammar URI before provider admission"` | Dynamic grammar URI is the VoiceXML 2.1 extension |
| `menu` / `choice` | 2.0 | CMeta + collect provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "projects static menus through collect ABI and re-arms after a handled Event"` | `tests/voicexml/test_voicexml_cmeta_program.c :: "fails closed for invalid or deferred static menu syntax"` | Exact/approximate speech and explicit grammar require matching provider capability |
| `initial` mixed initiative | 2.0 | CMeta + collect provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "commits initial multi-slot semantics and fills every initial before PROCESS"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "fails closed when INITIAL and FIELD owners are simultaneously active"` | Bounded multi-slot typed completion |
| `subdialog` | 2.0 | CMeta + subdialog provider / owner | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "atomically binds RETURN_DATA before subdialog and form filled PROCESS"`; `tests/voicexml_subdialog_owner_test.c :: "routes RETURN_EVENT through the parent subdialog scope"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects unknown duplicate and incompatible RETURN_DATA atomically"` | Generation-safe child lifecycle; no hidden worker |
| `record` and owned result lease | 2.0 | CMeta + record provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "moves one accepted recording lease without copying payload bytes"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects missing record capabilities before provider prepare"` | Recording bytes remain Session-owned; provider capability is explicit |
| `transfer` + VoiceXML 2.1 `transfer@type` | 2.0 / 2.1 | CMeta + transfer provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "admits VoiceXML 2.1 transfer type and preserves consultation request policy"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "preserves exact transfer connection and unsupported Event names"` | Blind / bridge / consultation are capability-gated |
| CMeta external `data` + typed DataBind decode | 2.1 | CMeta + data provider + DataBind | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "evaluates document data srcexpr and ordered POST namelist in source order"`; `tests/voicexml/test_voicexml_cmeta_session.c :: "loads JSON external data into the CMeta root before form execution"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "fails dynamic data before provider admission when V3 is unavailable"` | Typed profile only; no DOM approximation |
| QuickJS no-DOM `data` side effect | 2.1 | QuickJS + data provider | **PARTIAL** | `tests/voicexml_quickjs_test.c :: "projects dynamic data URI namelist and fetch policy into one V3 attempt"` | `tests/voicexml_quickjs_test.c :: "rejects named data before provider admission with the exact Event"` | Response bytes are deliberately discarded; named DOM/object result is unsupported |
| external QuickJS script `src` / `srcexpr` | 2.0 / 2.1 | QuickJS + ScriptResource | **PARTIAL** | `tests/voicexml_quickjs_test.c :: "executes static and dynamic scripts transactionally and resumes after each action"` | `tests/voicexml_script_resource_test.c :: "rejects invalid external script language shapes before resource admission"` | External script profile only; no persistent browser DOM/global object model |
| QuickJS execution budget | profile contract | QuickJS | **PARTIAL** | `tests/voicexml_quickjs_test.c :: "interrupts a timed-out external script, closes its lease, and rolls back state"`; `tests/voicexml_quickjs_test.c :: "rolls back typed state and closes the lease when external script exhausts the QuickJS heap"` | `docs/specs/quickjs-execution-quota.md` | Heap, stack, result storage and monotonic deadline are enforced; exact deterministic bytecode-instruction quota is not claimed |
| typed CMeta urlencoded submit | 2.0 | CMeta + Session V2/V3 + SubmitResource | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "publishes ordered typed POST submit fields through generic V2 and stops later actions"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "fails unsupported float submit value before publishing the handoff"` | Static target; bounded bool/int/string snapshot; one attempt |
| typed CMeta multipart recording submit | 2.0 / 2.1 policy additions | CMeta + Session V4 + SubmitResource | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "publishes ordered scalar-recording-scalar multipart submit with policy and borrowed lease"`; `tests/voicexml_dialog_manager_test.c :: "V4 forwards ordered scalar and recording multipart views through one segmented attempt"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "fails a missing lastresult recording before publishing multipart submit"` | Explicit recording selection only; recording lease remains Session-owned |
| submit timeout / fetchaudio policy | 2.1 policy surface | Generic Session V3/V4 + DialogManager / Store | **SUPPORTED** | `tests/voicexml_dialog_manager_test.c :: "V4 projects submit timeout and brackets one attempt with Store-owned fetchaudio"` | `tests/voicexml_dialog_manager_test.c :: "V4 rejects unsupported submit timeout before fetchaudio or provider admission"` | Timeout capability and fetch-audio provider are explicit; no fallback that drops policy |
| recorded utterance metadata / recording shadows | 2.1 | CMeta + collect provider | **SUPPORTED** | `tests/voicexml/test_voicexml_cmeta_session.c :: "owns recorded-utterance V3 results only after committed recognition"` | `tests/voicexml/test_voicexml_cmeta_session.c :: "rejects recorded-utterance policy outside the explicit bounded contract"` | Generation-owned Session result; stale aliases lose replaced lease |
| CHTTP document resource adapter | deployment option | VoiceXMLDocumentStore + installed CHTTP | **SUPPORTED** | `tests/voicexml_chttp_resource_test.c :: "borrows the underlying CHTTP text lease until document close"` | `tests/voicexml_chttp_resource_test.c :: "fails closed when a per-request timeout cannot be honored"` | Optional adapter only; no CHTTP dependency in VoiceXML core |
| arbitrary VoiceXML DOM / persistent browser-style object graph | 2.0 / 2.1 | Base / CMeta / QuickJS | **UNSUPPORTED** | N/A | `tests/voicexml_quickjs_test.c :: "rejects named data before provider admission with the exact Event"` | Intentional architecture restriction; no fake DOM |
| unsupported elements/attributes outside the selected profile | 2.0 / 2.1 | all | **UNSUPPORTED** | N/A | `tests/voicexml/test_voicexml_program.c :: "rejects unsupported attributes on every admitted element"`; `tests/voicexml/test_voicexml_cmeta_program.c :: "explicitly rejects disabled or deferred syntax and implicit prompt text"` | Fail closed rather than approximate semantics |

## Profile capability summary

| Capability | Base | CMeta | QuickJS |
| --- | --- | --- | --- |
| immutable form/block/control core | yes | yes | yes |
| typed application state | no | yes | via transactional CMeta bridge |
| fields / FIA / Events | no | yes | no |
| prompt / collect / media provider projection | no | yes | no |
| data response decode into application state | no | yes, DataBind | no; response discarded |
| external JavaScript execution | no | no | yes, opt-in |
| submit scalar snapshot | literal zero-field only | yes | current generic handoff only |
| recording multipart submit | no | yes | no |
| arbitrary DOM | no | no | no |

“no” here means not part of that profile; it does not imply that another
profile silently supplies the feature.

## Provenance policy

### Current rows

All executable witnesses referenced above are **locally authored repository
tests**. No upstream VoiceXML implementation-report test body, assertion text,
fixture, or corpus has been copied into this matrix or into the cited local
tests for the purpose of claiming support.

The W3C documents above are normative standards references only.

The separate SCXML W3C corpus / manifest is intentionally excluded from
VoiceXML evidence because SCXML and VoiceXML are different language surfaces.

### Future upstream-derived rows

If an upstream VoiceXML implementation-report or third-party corpus case is
introduced, its row must record all of the following before it may become
SUPPORTED/PASS:

1. exact source URL/document;
2. upstream test or assertion identifier;
3. license / redistribution provenance;
4. description of the local transformation;
5. local executable witness that preserves the upstream assertion;
6. any deliberate profile restriction that changes applicability.

A source URL without a local executable witness is evidence for provenance, not
evidence for support.

## Support claim boundary

This matrix is the support truth for the incubating VoiceXML runtime. It does
not imply:

- full VoiceXML 2.0 or 2.1 conformance;
- built-in TTS, ASR, SIP, RTP or media transport;
- built-in network authorization, redirect or host policy;
- DOM compatibility;
- hidden retries for POST;
- unbounded runtime/provider queues;
- a deterministic QuickJS instruction counter not provided by the selected
  engine contract.

The canonical ownership and dependency model remains
[VoiceXML architecture](voicexml-architecture-design.md). Security policy,
bounded fuzzing and supported build-option qualification remain tracked by
issue #51.
