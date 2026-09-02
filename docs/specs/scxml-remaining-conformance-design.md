# TurboSCXML Remaining W3C Conformance Design

**Date:** 2026-09-02

**Status:** Accepted for implementation

**Standards:** [SCXML 1.0](https://www.w3.org/TR/scxml/), W3C IRP tests
150, 151, 301-304, 307, and 552.

## Decision

The remaining assertions are implemented as four independently testable
features over one shared execution model:

1. `<data src>` uses a host-owned resource adapter at the exact early/late
   binding point. The adapter exposes a CSerde reader and TurboSCXML uses
   CBind to decode one value into the destination CMeta descriptor.
2. `<foreach>` may create a finite, program-declared supplemental variable
   when `item` or `index` is absent from the application CMeta root.
3. CMeta value expressions retain unresolved data paths under an explicit
   runtime policy. Both an unbound late declaration and a missing child path
   produce the same `error.execution` behavior.
4. `datamodel="quickjs-sandbox"` is an optional build feature. It evaluates
   scripts and ECMAScript expressions without filesystem, network, process,
   environment, native module, or credential access.

The Statechart instance remains the owner of active configuration, queues,
macrostep ordering, and published CMeta state. TurboSCXML owns compiled
resource/script descriptors and session-local supplemental variables. The
host owns resource authorization, logical-URI resolution, authentication, and
deployment. An optional TurboSCXML adapter may perform bounded HTTP/1
acquisition through `Rocida::CHTTP`; it is not linked into the SCXML core.

## Data resource boundary

`<data src>` is not fetched while XML is parsed or the program is compiled.
The program retains the URI bytes and the exact destination descriptor. A
session receives an optional versioned resource adapter through additive CMeta
session options V3.

```c
typedef enum scxml_resource_status {
    SCXML_RESOURCE_OK = 0,
    SCXML_RESOURCE_NOT_FOUND,
    SCXML_RESOURCE_TIMEOUT,
    SCXML_RESOURCE_DENIED,
    SCXML_RESOURCE_LIMIT_EXCEEDED,
    SCXML_RESOURCE_INVALID_DATA,
    SCXML_RESOURCE_FAILED
} scxml_resource_status;

typedef struct scxml_data_resource {
    cserde_reader reader;
    void *lease;
} scxml_data_resource;

typedef struct scxml_data_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_resource_status (*open)(
        void *user, const char *uri, size_t uri_size,
        const cmeta_data_desc *expected, scxml_data_resource *out);
    void (*close)(void *user, scxml_data_resource *resource);
} scxml_data_resource_adapter_v1;
```

Successful `open` returns one READY CSerde reader and an optional adapter-owned
lease. TurboSCXML calls `close` exactly once after decode, including decode
failure. The reader must contain exactly one value; trailing tokens are an
error. The adapter and its user pointer remain borrowed until session destroy.

The session owns aligned zero-state decode scratch, bounded CBind scratch, and
the staged destination. Decode completes before the destination is replaced.
Provider error, malformed tokens, type/range mismatch, trailing tokens, or a
limit violation raises `error.execution`; no partial destination is published.
Top-level environment overrides suppress resource loading exactly as they
suppress expression/inline initializers.

The core never interprets a URI, opens a file, performs HTTP, guesses a media
type, or falls back to inline content. A host may implement `file:`, HTTPS, an
artifact store, or an in-memory test source behind the same adapter and policy.

### Optional CHTTP transport adapter

`TurboSCXML::CHttpResource` is a separate optional target. It links
`Rocida::CHTTP` and adapts an authorized HTTP response into the generic
data or compile-time text resource contract. `TurboSCXML::SCXML` neither links
CHTTP nor exposes a CHTTP type.

CHTTP deliberately separates `connection_uri`, HTTP `authority`, and the
origin-form `target`. Therefore the adapter does not parse an arbitrary SCXML
`src` into a network destination. A host-supplied resolver must authorize the
logical URI and return those three values. Resolver output is bounded and
borrowed only for the call. A denied, ambiguous, or unsupported URI fails
before network admission. Production SSRF policy must use an allowlisted
mapping or a pre-authorized concrete endpoint; passing untrusted host text
through unchanged is outside the contract.

The adapter accepts only a final 2xx response. It never follows redirects,
retries a request, changes media type, or falls back to inline content. The
host must initialize the borrowed CHTTP client with positive
connect/read/write deadlines and a hard `max_response_body_bytes` no greater
than the adapter's acceptance limit; CHTTP currently provides no client-config
introspection, so the adapter cannot verify this precondition. The adapter
passes a positive HTTP-result wait deadline to CHTTP and checks its own
source/decode limit after transport completion. CHTTP may continue terminal
cancellation/drain after the result deadline, so that deadline is not a hard
wall-clock bound for `open()`. A data decoder selected by one unambiguous,
exact configured `Content-Type` owns any DOM and CSerde reader until `close`.
Script text additionally requires valid UTF-8 before compilation.

Current CHTTP supports HTTP/1 over `tcp://` or local Pipe only; it has no TLS.
Consequently the adapter rejects `https` rather than downgrading it. TLS can
be admitted only after CHTTP exposes a verified TLS transport contract, or by
using a different host resource adapter. Blocking acquisition occurs at the
W3C binding boundary and blocks that session's serial executor through any
CHTTP terminal drain. A host requiring a hard wall-clock bound or non-blocking
execution must use a bounded authorized artifact cache with equivalent
resource semantics or introduce a future suspend/resume resource protocol.

## Supplemental variables

The application CMeta root stays immutable. Analysis first collects every
undeclared `foreach` item/index name and every admitted script-declared global
into a finite program table. Duplicate names share one slot only when their
value contracts are compatible. A conflicting type rejects compilation.

Each session allocates one bounded aligned storage block plus bound bits from
that table. A slot records its name, value descriptor, offset, and lifecycle
traits. `foreach` element type determines an auto-item slot type; index slots
use the CMeta `size_t` descriptor. Script-created slots use the bounded dynamic
value representation owned by the optional QuickJS module.

The supplemental scope is staged once per rollback-capable microstep. Commit
publishes its storage and bound bits; discard restores the previous scope.
There is no process-global variable map and no unbounded name insertion at
runtime. Expression and location resolution checks the application CMeta root,
read-only SCXML system values, then the compiled supplemental table.

## Missing-path execution semantics

Expression compilation distinguishes syntax/type corruption from a valid
data-model path that is not addressable at compile time. Under runtime path
policy, the latter becomes an immutable unresolved operand rather than a
compile failure. Evaluation returns `SCXML_EXPR_UNKNOWN_LOCATION` for:

- a declared late-binding range whose initializer has not committed; and
- a syntactically valid child path absent from a currently loaded value.

Existing executable boundaries translate that one status to one internal
`error.execution`. Guards remain false plus the same queued error. No value is
fabricated and no undefined/null fallback is introduced.

## QuickJS sandbox

The prior QuickJS/HTTP design from the original TurboUtils repository is
retained with current names and boundaries:

- build option `TURBOSCXML_ENABLE_QUICKJS`, default `OFF`;
- exact `datamodel="quickjs-sandbox"` admission only through
  `scxml_compile_quickjs()`;
- no QuickJS type appears in an installed header;
- one runtime per session, called only by that session executor;
- hard heap, stack, source, conversion, collection, string, and evaluation
  time limits; one deadline covers the complete import/eval/export transaction,
  including accessor and Proxy traps reached during export;
- compile-time admission rejects a root plus supplemental-variable schema whose
  static minimum property count already exceeds the runtime conversion budget;
- no `quickjs-libc`, `std`, `os`, module loader, `fetch`, socket, filesystem,
  environment, process, thread, clock, random, or dynamic code by default;
- read-only `_name`, `_sessionid`, `_event`, `_ioprocessors`, and `In()`;
- scripts run synchronously; Promise/async work is rejected;
- schema-backed state and the staged supplemental scope are imported into a
  fresh working context and exported transactionally;
- CMeta signed/unsigned values are limited to ECMAScript's exact safe-integer
  range; enum values use the same exact numeric bridge, while nested generic
  sequences are rejected during admission because their type arguments cannot
  be reconstructed from a CMeta element descriptor;
- exception or conversion failure discards the working context and raises
  `error.execution`; timeout/OOM additionally rebuilds the session runtime.

`<script src>` is resolved during workflow admission, before program
publication, by a separate compile-time text resource provider. The optional
CHTTP target can implement that provider with the same resolver and transport
policy. Missing, non-2xx, timed-out, oversized, or invalid script source
rejects the document. Runtime scripts never fetch resources.

HTTP ingress/egress and Bearer handling remain outside TurboSCXML. Scripts
cannot see credentials and cannot initiate network operations. External
effects continue through the existing event-I/O/invoke prepare, commit, and
discard adapters.

## Compatibility and dependencies

- Existing null/CMeta compile and session entry points retain their layout and
  behavior. New behavior is exposed through additive versioned entry points.
- `Rocida::CSerde` is a public type dependency only for the V3 resource
  adapter; `Rocida::CBind` is a private implementation dependency.
- `Rocida::CHTTP` is required only by the separately exported optional
  `TurboSCXML::CHttpResource` target. Its blocking client has one progress
  owner and is not shared concurrently between sessions.
- QuickJS is optional and private. Disabled builds retain the current package
  surface and reject `quickjs-sandbox` explicitly.
- The resource adapter is synchronous at the binding boundary. A host that
  needs asynchronous download must resolve immutable workflow resources before
  session execution or provide a bounded synchronous artifact cache.

## Verification

Each feature starts with a failing focused TinyTest and then promotes only its
covered W3C rows. Required evidence is:

- 552: open timing for early and late binding, one decode, exactly-once close,
  environment override suppression, provider/decode/trailing-token failures,
  and bound limits;
- 150/151: declared reuse, undeclared item/index creation, persistence after
  foreach, snapshot iteration, rollback, managed element lifecycle, and slot
  capacity/type conflicts;
- 307: identical runtime status, `error.execution`, and no fabricated value for
  unbound late and missing child paths;
- 301-304: bad `src` rejects admission, root scripts run at load, nested scripts
  run in executable order, script variables are valid locations, transaction
  rollback, API absence, timeout/OOM isolation, and feature-OFF behavior.

Debug/ASan and Release builds, focused tests, complete CTest, strict W3C corpus,
`git diff --check`, and installed C/C++ consumer checks are the completion gate.
