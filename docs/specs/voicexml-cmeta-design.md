# VoiceXML Typed CMeta Datamodel Design

## Status and scope

This design is the non-media data slice tracked by
[GitHub issue #44](https://github.com/qigao/turbo-scxml/issues/44), under the
[VoiceXML roadmap #41](https://github.com/qigao/turbo-scxml/issues/41). It
depends on the non-media core from #42.

The slice adds an explicit, bounded `datamodel="cmeta"` profile. The
`datamodel` root attribute is a TurboSCXML extension, not a VoiceXML 2.0
attribute. The profile implements
typed variables, lexical scope, transactional executable content, conditional
selection, block form-item guards, and terminal data. It does not claim to
implement ECMAScript. A document that needs the standard ECMAScript language
must use a future explicitly selected adapter.

The following elements and attributes enter the accepted profile:

- root, form, and executable-content `var`;
- `assign` and `clear`;
- `if`, `elseif`, and `else`;
- `block@name`, `block@expr`, and `block@cond`;
- `exit@expr` or `exit@namelist`;
- root `datamodel="cmeta"` for the CMeta compiler entry point.

The following remain explicitly unsupported: `value`, `prompt`, `audio`,
SSML, grammar, recognition, collection, fields, `filled`, `record`, `transfer`,
media callbacks, prompt/event counters, QuickJS, DOM objects, scripts, `data`,
and network access. VoiceXML `value` is prompt/log rendering content, so it is
deferred rather than evaluated and discarded. `log` and non-whitespace PCDATA
in executable content are likewise rejected: the latter is implicit prompt
content. `clear` resets block form-item eligibility in this slice, but its
prompt/event-counter effects are deferred.

Normative behavior is drawn from VoiceXML 2.0 sections on variables,
assignment, clear, conditionals, blocks, forms, and exit:

- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml5.3.1>
- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml5.3.2>
- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml5.3.3>
- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml5.3.4>
- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml5.3.9>
- <https://www.w3.org/TR/2004/REC-voicexml20-20040316/#dml2.3.2>

## Product boundary

`TurboSCXML::VoiceXML` remains installable with `Salts::XmlParser` as its only
public dependency. The typed extension is a second static library:

- target: `turbo_voicexml_cmeta`;
- installed alias: `TurboSCXML::VoiceXMLCMeta`;
- public header: `include/voicexml/cmeta.h`;
- public dependencies: `TurboSCXML::VoiceXML` and `Salts::CMeta`;
- private implementation dependency: `Salts::QueryVM`.

`VoiceXMLCMeta` does not link `TurboSCXML::SCXML` and does not translate a
VoiceXML document into an SCXML machine. The implementation reuses neutral
CMeta storage/location primitives extracted from the SCXML implementation and
uses a VoiceXML-specific restricted scalar expression front end. The latter
has no CFlow state IDs, `In()`, SCXML system operands, external evaluator,
QuickJS, or runtime-missing path mode.

Both compilation entry points produce the existing opaque `vxml_program`, and
both session initializers produce the existing opaque `vxml_session`. Private
program and session profile vtables own extension destruction and execution.
The base target calls only generic callbacks and contains no CMeta references.
Plain `vxml_compile` rejects `datamodel="cmeta"`; `vxml_compile_cmeta` requires
it. Plain `vxml_session_init` rejects a CMeta program, and
`vxml_session_init_cmeta` rejects a literal-core program.

## Public API

`include/voicexml/voicexml.h` appends `VXML_INVALID_CONTRACT` and
`VXML_SEMANTIC_ERROR` to `vxml_status`. Existing numeric values do not move.
The CMeta extension installs the following ABI:

```c
#define VXML_CMETA_COMPILE_OPTIONS_ABI_V1 1u
#define VXML_CMETA_SESSION_OPTIONS_ABI_V1 1u

typedef struct vxml_cmeta_name_view {
    const char *data;
    size_t size;
} vxml_cmeta_name_view;

typedef enum vxml_cmeta_value_kind {
    VXML_CMETA_VALUE_UNDEFINED = 0,
    VXML_CMETA_VALUE_BOOL,
    VXML_CMETA_VALUE_SINT,
    VXML_CMETA_VALUE_UINT,
    VXML_CMETA_VALUE_FLOAT,
    VXML_CMETA_VALUE_STRING
} vxml_cmeta_value_kind;

typedef struct vxml_cmeta_value_view {
    vxml_cmeta_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint_value;
        double number;
        struct { const char *data; size_t size; } string;
    } data;
} vxml_cmeta_value_view;

typedef struct vxml_cmeta_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    const cmeta_data_desc *const *semantic_data;
    size_t semantic_data_count;
    size_t max_expression_bytes;
    size_t max_expression_instructions;
    size_t max_expression_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    size_t max_scope_slots;
    size_t max_scope_storage_bytes;
    size_t max_conditional_depth;
} vxml_cmeta_compile_options_v1;

typedef struct vxml_cmeta_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_root;
    const vxml_cmeta_name_view *initially_undefined;
    size_t initially_undefined_count;
    size_t max_transaction_bytes;
    size_t max_execution_steps;
} vxml_cmeta_session_options_v1;

typedef enum vxml_cmeta_exit_kind {
    VXML_CMETA_EXIT_EMPTY = 0,
    VXML_CMETA_EXIT_EXPRESSION,
    VXML_CMETA_EXIT_NAMELIST
} vxml_cmeta_exit_kind;

vxml_status vxml_compile_cmeta(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program *out, vxml_diagnostic *diagnostic);

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options);

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value);

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind);

size_t vxml_session_cmeta_exit_count(const vxml_session *session);

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index,
    vxml_cmeta_name_view *out_name,
    vxml_cmeta_value_view *out_value);
```

Every options structure is size-versioned. Unknown ABI versions, undersized
required prefixes, invalid known fields, and zero hard limits fail before
publishing an owner. A v1 implementation ignores bytes beyond the complete v1
structure so a future caller can retain ABI v1 while adding an optional tail.
The root descriptor and every descriptor reachable from it or `semantic_data`
remain borrowed until program
destruction. The `semantic_data` pointer array is copied; descriptor objects
remain borrowed. The pointer/count pair must both be empty or both be present;
each entry must be a valid addressable data descriptor, and semantic duplicate
storage types are rejected with `cmeta_type_equal`. Type-to-data lookup uses a
registered descriptor first and root traversal second. Metadata compatibility
uses `cmeta_type_equal`, never pointer identity.

`initial_root` is borrowed only during session initialization. Each top-level
field is independently copied into a session-owned application-scope slot; the
aggregate object is not retained as mutable session state. Every field must
have either trivial copy/destruction or complete copy/move/destroy traits. The
listed `initially_undefined` names must be unique top-level root fields; those
fields are not constructed, while all other fields begin defined. Clearing a
managed slot destroys it before clearing the bound bit. This lets undefined
remain distinct from a zero, false, empty, or zero-restored object.

Exit-result string bytes are session-owned until close or destruction. An
ordinary public read copies borrowed CMeta string bytes into bounded
session-owned scratch; that view remains valid until the next read, mutating
session call, close, or destruction.

## Typed name admission

The root struct is the application-scope schema, initialization source, and
declaration type catalog. Its top-level fields become predeclared independent
application slots. Every source `var@name` must match one top-level root field.
Its local slot has that field's semantic type, even when the variable starts
undefined. This rule supplies compile-time typing without inventing a
nonstandard XML type attribute.

Root-level `var` creates a document-scope slot. A `var` directly under `form`
creates a dialog-scope slot. A `var` in block executable content creates an
anonymous block-scope slot. A declaration shadows the same name in an outer
scope. Duplicate root or form declarations fail referential-correctness
admission. Only a repeated executable-content `var` in the same anonymous
scope acts on the existing variable: an `expr` assigns it and omission of
`expr` leaves its current value state unchanged.

Each named block has a compiler-created Boolean form-item variable in the same
dialog namespace as form-level `var`. A collision between any form item and
form variable fails admission. Anonymous blocks receive private compiler IDs
and are not valid in `clear@namelist`. This bounded profile requires
`block@expr` to compile to Boolean. It is evaluated during dialog initialization
and initializes the form-item variable; a defined value makes the block
initially ineligible.

Name lookup is deterministic and compiled into a typed handle:

```text
anonymous block scope
    -> dialog/form scope
    -> document scope
    -> application root
```

`assign@name`, expression operands, `clear@namelist`, and `exit@namelist` use
the closest admitted binding. Dotted paths may traverse a structured root or
local variable but the final value must be one of the supported scalar kinds.
Unknown paths, incompatible expression/result types, writable nonlocations,
and paths beyond `max_path_depth` fail compilation with the XML location and
expression byte offset retained in the diagnostic message.

A compiled name operand retains the ordered same-name candidate chain rather
than one prematurely selected slot. At runtime it chooses the innermost
currently declared candidate, so an outer variable remains visible until an
inner executable declaration actually runs. If no candidate is declared, the
operation fails with `VXML_SEMANTIC_ERROR`.

Every slot has separate `declared` and `bound` bits. Application bindings are
declared at session initialization. Root/form variables become declared in
their source initialization order, and executable variables become declared
only when their statement executes. A compiled handle may therefore resolve a
later lexical declaration, but evaluation before its declaration is a runtime
`VXML_SEMANTIC_ERROR`. A declaration in an untaken branch remains undeclared.

This first custom profile deliberately models the host root as application
scope and root `var` as a distinct document scope. It does not yet expose the
standard `application.x`, `document.x`, `dialog.x`, or `session.x` qualified
objects, a read-only session scope, or application-root document aliasing.
Those constructs are rejected until application/document navigation defines
their ownership in roadmap #48. This is an explicit CMeta-profile limitation,
not a VoiceXML conformance claim.

## Expression profile

The CMeta expression grammar is the existing bounded typed scalar grammar used
by TurboSCXML CMeta: Boolean, signed/unsigned integer, floating-point, and
string literals; typed locations; parentheses; unary operators; arithmetic,
comparison, equality, and Boolean operators. It is named the CMeta expression
profile and is not advertised as ECMAScript compatibility.

Conditions must compile to Boolean. Value expressions produce a supported
scalar or undefined. Reading a declared undefined binding yields undefined;
copying it with `var`/`assign` clears the target, and exit may snapshot it.
Applying an arithmetic/comparison/Boolean operator to undefined, or using it
as a condition, is a semantic failure rather than an ECMAScript coercion.
Assignment conversion is checked against the exact target descriptor;
narrowing overflow, incompatible kinds, and invalid string storage are
semantic failures. Strings are bounded by both expression and target limits
and use the descriptor's CMeta buffer operations. The compiler retains
immutable expression programs; the program destructor releases them exactly
once.

## Scope lifecycle and FIA behavior

Session initialization copies each constructed host-root field into the
application frame and creates document, dialog, form-item, and anonymous
storage plus independent declared/bound bitmaps. Starting the session
initializes document variables in source order, enters the first form, and
initializes dialog variables and block `expr` values in source order.

The non-media FIA repeatedly selects the first block in document order whose
form-item variable is undefined and whose optional condition is true. It marks
the block's form-item variable true before executing the block body. When no
block is eligible, the session exits with an empty result. `clear` can make a
named block eligible again. `max_execution_steps` counts selection and action
steps so a document such as an unconditional self-clearing block fails with
`VXML_LIMIT_EXCEEDED` instead of looping forever.

`if` evaluates its condition once and executes the first matching branch;
`elseif` and `else` do not create new variable scopes. Executable declarations
in an untaken branch remain undeclared. `clear` with a namelist
sets each closest declared user variable or named block item to undefined.
`clear` without a namelist resets all named and anonymous block form-item
variables in the current form but does not clear ordinary variables. Prompt
and event counters do not exist in this slice.

`exit@expr` and `exit@namelist` are mutually exclusive. Their simultaneous use
is classified as the standard `error.badfetch` error and maps to
`VXML_INVALID_STRUCTURE` until scoped event handling exists. An undeclared
namelist reference or failed runtime expression is `error.semantic` and maps
to `VXML_SEMANTIC_ERROR`. Neither attribute produces an
empty result. `expr` produces one unnamed scalar entry. `namelist` snapshots
each listed scalar binding in source order, including undefined. Result names
and string bytes are copied into session-owned terminal storage before staged
state is released. In the non-media profile no prompt queue exists, so exit is
immediate; prompt-drain timing is deferred with the media slice.

## Transaction protocol

One block execution is one executable-content transaction:

1. Copy committed application, document, dialog, form-item, and active
   anonymous state into staged storage.
2. Mark the selected form item and execute every `var`, `assign`, `clear`,
   conditional branch, and exit-data preparation against staged state.
3. On success, move staged managed values into committed storage and publish a
   fully owned exit snapshot when present.
4. On any evaluation, conversion, allocation, or limit failure, destroy staged
   managed values, preserve every committed byte/bound bit, transition the
   session to `FAILED`, and retain the stable error code.

Commit contains no allocation or fallible operation. Core frame, scratch, and
exit-result capacity is reserved before the first staged mutation. A CMeta
managed copy or buffer assignment may still allocate and fail while building
staged state; that destroys staging and leaves committed state unchanged.
Rollback and destruction are idempotent. Anonymous scope is discarded after a
dialog execution ends, not after each block visit, so executable declarations
and values survive a `clear`-driven revisit. Document/dialog/application/
form-item scopes remain committed.

Whole-block rollback is a deliberate CMeta safety extension. VoiceXML's
standard imperative executable-content model does not roll back earlier writes
when a later element throws. This profile chooses failure atomicity while it
has no scoped catch handlers. A future event-handling slice must either retain
this documented extension or revise the transaction boundary before catches
can observe failed executable content.

## Internal reuse boundary

The existing SCXML scope code supplies the proven storage rules: bounded slot
registration, alignment checks, semantic type equality, managed copy/move/
destroy, and independent bound bits. The existing location code supplies
checked dotted field traversal. Their neutral portions move behind an internal
CMeta runtime target used by both SCXML and VoiceXMLCMeta, with thin SCXML
wrappers where SCXML-specific diagnostics or system-variable policy remains.

The first VoiceXML expression implementation is restricted and separately
named. It may reuse QueryVM bytecode and parsing/conversion helpers, but it must
not include SCXML public/private headers or link the SCXML product. Generalizing
the entire SCXML expression runtime is deferred until parity tests can prove
that `_event`, `_name`, `_sessionid`, `_ioprocessors`, `In()`, `is_bound()`,
external evaluation, and runtime-missing path semantics remain unchanged.

## Verification gates

- API/ABI tests cover C11 and C++ inclusion, versioned prefix/tail validation,
  wrong-program initializers, ownership, and zero-handle destruction.
- Compiler tests cover every admitted element, wrong placement/order,
  unsupported media elements including `value`, exact/one-over limits, typed
  path failures, duplicates, and input-source independence.
- Runtime tests cover initialization order, all scope levels, shadowing,
  undeclared versus undefined versus scalar zero, forward references,
  declarations in untaken branches, executable `var` redeclaration, persistent
  anonymous state, block selection, `clear` revisit behavior, first-match
  conditionals, and loop bounds.
- Transaction tests inject evaluation, managed-copy, string allocation, and
  result-snapshot failures and prove that root plus every active scope rolls
  back together.
- Multi-translation-unit tests define semantically equal descriptors at
  distinct addresses and exercise both C and C++ consumers.
- Package tests prove that `VoiceXML` still consumes an XmlParser-only Salts
  fixture, while `VoiceXMLCMeta` requires only VoiceXML, CMeta, XmlParser, and
  the private QueryVM link closure. The package checks CMeta and QueryVM only
  when that component is requested.
- Final gates run fresh Release and Debug/ASan presets, install presets,
  external package consumers, `git diff --check`, and a public-header scan that
  rejects media callbacks, tickets, tokens, and wait-state APIs.
