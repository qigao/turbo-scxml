# VoiceXML 2.1 Incubating Architecture Design

## Status and standards profile

TurboSCXML currently provides an SCXML execution engine and a bounded CCXML
profile. CCXML can prepare, start, and terminate VoiceXML-capable dialogs, but
the attached telephony provider still owns document retrieval, VoiceXML
execution, prompt and recognition media, and result Events. This design adds a
separate `TurboSCXML::VoiceXML` product that can become that dialog engine.

The standards baseline is VoiceXML 2.1, which incorporates VoiceXML 2.0 and
adds orthogonal features. The first release is explicitly an incubating profile,
not a claim of complete VoiceXML conformance. It accepts only documented
subsets and rejects unsupported elements or attributes instead of approximating
their behavior.

Normative references:

- VoiceXML 2.0 and the Form Interpretation Algorithm (FIA):
  <https://www.w3.org/TR/voicexml20/>
- VoiceXML 2.1 additions and conformance rules:
  <https://www.w3.org/TR/voicexml21/>
- CCXML dialog integration and returned Events:
  <https://www.w3.org/TR/ccxml/#dialogstart>

The VoiceXML namespace is `http://www.w3.org/2001/vxml`. The compiler accepts
root versions `2.0` and `2.1`, while each implementation slice publishes its
exact supported surface. XPath is not introduced. ECMAScript is not silently
reinterpreted as CMeta: the future datamodel boundary will identify the chosen
language explicitly, with typed CMeta as the bounded built-in profile and an
optional QuickJS profile for applications that require ECMAScript behavior.

## Dependency and ownership boundaries

```text
VoiceXML XML bytes
    -> VoiceXML compiler (Salts::XmlParser)
    -> immutable bounded vxml_program
    -> single-owner synchronous FIA core
       -> typed datamodel adapter
       -> future prompt/collect/resource adapters
       -> terminal result
    -> optional serial dialog manager
       -> asynchronous media/fetch completions
       -> CCXML dialog provider decorator
       -> dialog.prepared / dialog.started / dialog.exit / error.dialog.*
```

The FIA is a specialized deterministic interpreter. VoiceXML documents are not
translated into SCXML documents: doing so would obscure prompt counters,
form-item guards, collect/process phases, and VoiceXML event-handler scope.
Future asynchronous adapters may reuse CFlow effect tickets and executor
conventions, but VoiceXML semantics remain owned by the VoiceXML module.

The initial core is deliberately single-owner and synchronous. It runs until
it reaches a platform wait or a terminal state. An asynchronous host must
serialize `start`, completion, cancel, and destroy operations. The later dialog
manager supplies that serialization with a borrowed `cflow_executor`; the FIA
core does not create a hidden worker or block an executor waiting for itself.

## Products and files

The first product is a static library:

- target: `turbo_voicexml`
- installed alias: `TurboSCXML::VoiceXML`
- public header: `include/voicexml/voicexml.h`
- compiler: `src/voicexml_program.c`
- runtime: `src/voicexml_session.c`
- private representation: `src/voicexml_internal.h`

Its public header exposes C11 opaque program and session handles. It uses
`salts_xml_limits`/`salts_xml_location` for bounded compiler configuration and
diagnostics, so `Salts::XmlParser` is its only public dependency. CFlow becomes
a dependency only when an asynchronous adapter slice needs effect tickets.
`TurboSCXML::VoiceXML` does not link CCXML; the later bridge depends one-way on
both `TurboSCXML::CCXML` and `TurboSCXML::VoiceXML`.

Both fixed-size handles are single-owner and non-copyable despite their C struct
representation. Reuse requires destruction first. Ownership may be moved only
by copying the complete handle into an empty destination and immediately
zeroing the source, so exactly one handle remains responsible for destruction.

Optional products are introduced only by their owning roadmap slices:

- `TurboSCXML::VoiceXMLDialogManager`: serial asynchronous session registry
  and CCXML telephony-adapter decorator;
- `TurboSCXML::VoiceXMLCHttpResource`: URI retrieval policy backed by
  `Salts::CHTTP` without making CHTTP a core dependency;
- optional QuickJS datamodel support guarded by the existing project option.

## Core MVP document profile

The first executable slice accepts:

```xml
<vxml xmlns="http://www.w3.org/2001/vxml" version="2.1">
  <form id="main">
    <block>
      <exit/>
    </block>
  </form>
</vxml>
```

The root contains one or more `form` elements. A form has an optional unique
XML NCName `id` and contains one or more `block` control items. A block is empty
or contains one explicit `exit` action. This deliberately small slice proves
document admission, ordered control-item traversal, and terminal lifecycle
without defining any media API. `prompt`, `audio`, grammar, recognition,
recording, transfer, and their callbacks remain unsupported until explicitly
requested.

Root, form, block, and exit attributes outside the profile are rejected,
except `form@id`. Duplicate form
IDs, foreign namespace elements, invalid UTF-8, and non-whitespace content in
`exit` are rejected with source-located diagnostics. The first form is the
entry dialog. Multiple forms are compiled now so later `goto` support does not
require a program representation change; the MVP does not navigate away from
the first form.

The compiler produces tables for forms, blocks, and actions plus one immutable
string arena. Rows use indices and byte offsets rather than pointers during
measurement. The final allocation is overflow-checked and performed once.
Every retained string consumes its decoded byte length plus one trailing NUL.

The configurable defaults are:

- XML limits: `salts_xml_default_limits()`;
- `max_forms = 64`;
- `max_blocks = 1024`;
- `max_actions = 4096`;
- `max_name_bytes = 256 * 1024`.

Every limit must be positive. Compile failure leaves the output program empty
and destroys the XML document and every temporary allocation exactly once.

## Core MVP runtime protocol

The public session states are `READY`, `RUNNING`, `EXITED`, `FAILED`, and
`CLOSED`. A successful initialization borrows the immutable program, which
must outlive the session.

`vxml_session_start` is legal only in `READY`. The interpreter selects the
first form and visits its blocks in document order. An explicit `exit`, an
empty block followed by exhaustion, or exhaustion of the entry form changes
the session to `EXITED`. Repeated start and all operations after close are
rejected deterministically. There is no callback, ticket, token, wait state,
thread, executor, or media object in this slice.

An explicit `exit` or exhaustion of the entry form changes the session to
`EXITED`. The MVP result contains no data. Later `exit@namelist` and `exit@expr`
support extends the terminal result through size-versioned fields and maps it
to CCXML `dialog.exit` values.

`vxml_session_close` is legal from every non-destroyed state and idempotent.
Destruction releases session storage without touching the borrowed program.

## Future adapter and memory protocol

This protocol is deferred with the media roadmap slice and is not present in
the core ABI. When that slice is requested, one prompt will be in flight per
core session. There is one producer and one
consumer at the core boundary because the caller serializes operations. The
request contains a token and a borrowed program-owned text view valid only for
the prepare callback. Accepted prepare moves exactly one effect ticket to the
core; non-accepted prepare moves none. Every accepted ticket reaches exactly
one terminal operation: commit on successful admission or discard when the
callback contract is invalid before publication.

The core never retains provider output. The adapter owns in-flight media and
must remain alive through quiescence. `close` rejects new playback and requests
cancellation; native completion remains authoritative within the adapter, but
the closed core rejects all later completion delivery. Capacity exhaustion and
provider refusal are explicit `VXML_ADAPTER_ERROR` outcomes. There is no silent
drop, retry, hidden allocation, or blocking wait.

The asynchronous dialog manager adds a fixed-capacity dialog registry and a
bounded MPSC completion ingress. A committed CCXML dialog operation owns one
registry row. Completion ingress copies `{dialog_generation, prompt_token,
result}`; full returns a visible rejection to the media producer. Generation
checking suppresses late completions after termination or row reuse. Shutdown
order is: stop admission, close sessions, drain or cancel native work, observe
all adapters quiescent, destroy sessions, then destroy executor and backends.

## CCXML integration

The dialog manager is a decorator around an existing
`ccxml_telephony_adapter_v1`, not a replacement for call and conference
control. It copies an upstream adapter and user pointer, forwards non-dialog
operations unchanged, and owns the four dialog tail operations:

- `prepare_dialog_prepare` creates a reserved dialog row and begins resource
  preparation only after ticket commit;
- `prepare_dialog_start` creates and starts a direct-source row;
- `prepare_prepared_dialog_start` starts an existing prepared row on the
  supplied connection;
- `prepare_dialog_terminate` delivers
  `connection.disconnect.hangup`, then performs normal VoiceXML shutdown.

The bridge must preserve the current CCXML rule that returned dialog IDs are
visible in the CCXML datamodel before provider publication. It reports
`dialog.prepared`, `dialog.started`, `dialog.exit`, or `error.dialog.*` through
a host-owned bounded CCXML Event sink; it never recursively calls
`ccxml_session_dispatch` from a VoiceXML executor callback.

Direct CCXML calls currently carry URI and connection bytes only during the
prepare callback. The manager copies all retained fields into the reserved
dialog row. Prepared rows retain the fetched program and source identity until
start, termination, or manager shutdown. Registry full, duplicate operation,
invalid generation, resource error, and Event sink full are explicit outcomes
with exact ownership cleanup.

## Datamodel and FIA growth

The MVP has no expressions. The next data slice introduces a versioned
VoiceXML datamodel adapter instead of reaching into SCXML private expression
headers. It owns document, application, dialog, form-item, and anonymous
scopes; represents undefined distinctly from empty string; compiles conditions
and values during program admission; and stages mutation so failed executable
content cannot partially update live state.

The built-in CMeta profile requires a host-supplied root schema and uses typed
supplemental slots for compiler-created form-item variables and prompt
counters. Metadata equality is semantic, never descriptor-pointer equality.
An optional QuickJS adapter may later implement ECMAScript, but the selected
datamodel is explicit and programs requiring unsupported object/DOM behavior
fail admission.

Directed form support follows the normative FIA phases:

1. initialize form variables and prompt counters;
2. select the first eligible form item in document order;
3. queue prompts, activate grammars, and collect input or an Event;
4. map recognition results into form-item variables;
5. execute applicable `filled` handlers in document order;
6. resolve scoped catches or return to selection.

Prompt/media, recognition/grammar, resource retrieval, and outbound navigation
remain separate versioned adapters. SRGS, SSML, codecs, TTS, and ASR engines
are provider capabilities, not algorithms hidden in CMeta or the FIA.

## GitHub-tracked roadmap slices

Each slice receives its own design/implementation plan before code changes and
must end in a runnable test boundary:

Umbrella: [GitHub issue #41](https://github.com/qigao/turbo-scxml/issues/41).

1. [#42](https://github.com/qigao/turbo-scxml/issues/42): core MVP compiler
   and non-media `form/block/exit` runtime.
2. [#43](https://github.com/qigao/turbo-scxml/issues/43): serial dialog manager
   and CCXML prepare/start/terminate bridge.
3. [#44](https://github.com/qigao/turbo-scxml/issues/44): typed CMeta datamodel,
   `var/assign/clear/if`, block guards, exit data, and transactional scopes.
   Prompt-only `value` and prompt counters remain with the media roadmap.
4. [#45](https://github.com/qigao/turbo-scxml/issues/45): directed FIA fields,
   grammar/collect adapter, semantic results, and `filled`.
5. [#46](https://github.com/qigao/turbo-scxml/issues/46): scoped event handling,
   `catch/throw/help/noinput/nomatch/reprompt`, and tapered prompt counters.
6. [#47](https://github.com/qigao/turbo-scxml/issues/47): prompt queue,
   SSML/audio provider surface, timing, and barge-in cancellation.
7. [#48](https://github.com/qigao/turbo-scxml/issues/48): URI resource policy,
   CHTTP adapter, application/document navigation, `goto`, `submit`, `data`,
   and optional script resources.
8. [#49](https://github.com/qigao/turbo-scxml/issues/49): menus and advanced
   form items: `choice`, `subdialog`, `record`, and `transfer`.
9. [#50](https://github.com/qigao/turbo-scxml/issues/50): remaining VoiceXML
   2.1 additions, including dynamic resources, prompt foreach, and marks.
10. [#51](https://github.com/qigao/turbo-scxml/issues/51): conformance manifest,
    fuzzing, security hardening, and the published support matrix.

## Explicit non-goals

- No complete-conformance claim during the incubating profile.
- No XPath dependency or DOM/XPath compatibility layer.
- No implicit network access in the core library.
- No bundled TTS, ASR, SIP, RTP, codec, or audio-device backend.
- No unbounded DOM retention, queues, retry loops, recursion, or string growth.
- No hidden worker threads or callback waits on the owning serial executor.
- No approximation of unsupported VoiceXML elements or ECMAScript expressions.
- No change to existing SCXML or CCXML behavior in the core-MVP slice.

## Verification strategy

Every slice starts with focused TinyTest RED/GREEN coverage and then runs the
Release preset. Parser tests cover namespace/version/structure diagnostics,
UTF-8, duplicate IDs, source independence, checked limits, and failure cleanup.
Core runtime tests cover document order, explicit and implied exit, invalid
state transitions, close, and allocation-failure cleanup. Future adapter and
manager tests add registry full, late completion,
generation reuse, Event sink backpressure, prepared/direct start parity, and
normal termination.

Changes to targets or installation additionally run the install preset and an
external C and C++ `find_package(TurboSCXML COMPONENTS VoiceXML)` consumer.
Optional CHTTP and QuickJS builds are tested both disabled and enabled. A
conformance manifest records every upstream-derived case as `PASS`,
`UNSUPPORTED`, or `N/A`; rows move to `PASS` only with an executable local
witness that preserves the normative assertion.
