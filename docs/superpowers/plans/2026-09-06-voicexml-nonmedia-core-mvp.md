# VoiceXML Non-Media Core MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Add an installable bounded VoiceXML core that compiles
`vxml/form/block/exit` documents and executes the first form to deterministic
exit without introducing media APIs or callbacks.

**Architecture:** `TurboSCXML::VoiceXML` owns an immutable measured program and
a single-owner synchronous FIA foundation. The first slice validates structure,
retains form identities, visits blocks in document order, and terminates through
explicit or implied exit. Media and asynchronous adapters remain deferred.

**Tech Stack:** C11, Salts XmlParser, TinyTest, CMake presets.

**Spec:** `docs/specs/voicexml-architecture-design.md`

**Tracking:** [GitHub issue #42](https://github.com/qigao/turbo-scxml/issues/42),
under [VoiceXML roadmap #41](https://github.com/qigao/turbo-scxml/issues/41).

## Global Constraints

- Accept only namespace `http://www.w3.org/2001/vxml` and versions `2.0`/`2.1`.
- Support only root `form`, form `block`, and an optional empty `exit` child of
  a block. Reject unsupported elements and attributes explicitly.
- Allow multiple forms, require at least one form and one block, enter the first
  form, and reject duplicate nonempty form IDs.
- Use `salts_xml_default_limits()`, `max_forms = 64`, `max_blocks = 1024`,
  `max_actions = 4096`, and `max_name_bytes = 256 * 1024` as defaults.
- Use checked arithmetic for counts, retained IDs plus NUL, row storage,
  alignment, and the single final program allocation.
- Leave output handles zeroed after failure and release temporary ownership
  exactly once. Program input bytes may be released after successful compile.
- Keep the core single-owner and synchronous. Do not create a thread, executor,
  mailbox, network client, or CCXML dependency.
- This plan MUST NOT add prompt/audio/SSML/ASR/SRGS/recognition/record/transfer,
  media callbacks, media tickets, wait tokens, XPath, CMeta, or QuickJS.
- Unsupported functionality returns `VXML_UNSUPPORTED_FEATURE`; malformed
  supported structure returns `VXML_INVALID_STRUCTURE` with source location.

---

### Task 1: Establish the VoiceXML target and public ABI

**Files:**
- Create: `include/voicexml/voicexml.h`
- Create: `src/voicexml_internal.h`
- Create: `src/voicexml_program.c`
- Create: `tests/voicexml/test_voicexml_api.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Produces `TurboSCXML::VoiceXML`, opaque program/session types,
status/diagnostic/limit definitions, and compile/program lifecycle declarations.
Only `Salts::XmlParser` is public. No runtime adapter type is permitted.

- [ ] Add a TinyTest that includes `<voicexml/voicexml.h>`, checks documented
  default limits, rejects null compile arguments, and safely destroys a zero
  program. Configure and run it first; record the expected RED build failure.
- [ ] Define C11 ABI guards, `vxml_status`, `vxml_limits`, `vxml_diagnostic`,
  opaque `vxml_program` and `vxml_session`, program/session state enums, and:

```c
vxml_limits vxml_default_limits(void);
vxml_status vxml_compile(const void *bytes, size_t size,
                         const vxml_limits *limits,
                         vxml_program *out,
                         vxml_diagnostic *diagnostic);
void vxml_program_destroy(vxml_program *program);
vxml_status vxml_session_init(vxml_session *session,
                              const vxml_program *program);
vxml_status vxml_session_start(vxml_session *session);
vxml_session_state vxml_session_get_state(const vxml_session *session);
vxml_status vxml_session_error(const vxml_session *session);
vxml_status vxml_session_close(vxml_session *session);
void vxml_session_destroy(vxml_session *session);
```

- [ ] Add a buildable skeleton target and focused GREEN test. Commit.

### Task 2: Compile the bounded document profile

**Files:**
- Modify: `src/voicexml_internal.h`
- Modify: `src/voicexml_program.c`
- Create: `tests/voicexml/test_voicexml_program.c`

**Boundary:** Consumes Salts XML nodes/locations and produces an immutable
program with form rows, block rows, exit action rows, and retained form IDs.

- [ ] Add failing table-driven tests for a canonical explicit-exit document,
  empty-block implied exit, multiple forms, decoded form IDs, and source-input
  independence. Record RED.
- [ ] Add failing negative tests for wrong/missing namespace/version, duplicate
  IDs, missing forms/blocks, foreign or unsupported elements (including
  `prompt`), unsupported attributes, non-whitespace text, and nonempty `exit`.
- [ ] Add exact-limit and one-over tests for forms, blocks, actions, and retained
  ID bytes, plus zero limits and malformed XML diagnostics.
- [ ] Implement measure-then-build compilation with checked arithmetic and one
  final immutable allocation. Preserve the first relevant source location.
- [ ] Run focused tests and the Release suite; record GREEN. Commit.

### Task 3: Execute block traversal and terminal lifecycle

**Files:**
- Create: `src/voicexml_session.c`
- Create: `tests/voicexml/test_voicexml_session.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Borrows a compiled program and provides a deterministic,
single-owner state machine with no callback or asynchronous admission surface.

- [ ] Add failing tests for READY after init, explicit exit, implied exit after
  empty blocks, first-form entry, start-only-once, invalid/zero handles,
  idempotent close from every state, and destroy without touching the program.
- [ ] Implement READY -> RUNNING -> EXITED traversal. Structural impossibilities
  detected at runtime transition to FAILED with a stable error.
- [ ] Ensure operations after CLOSED and repeated start fail without mutation.
- [ ] Run focused tests and the Release suite; record GREEN. Commit.

### Task 4: Export, install, and document the non-media profile

**Files:**
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/main.c`
- Modify: `README.md`
- Modify: `CMakeLists.txt`

**Boundary:** Makes `find_package(TurboSCXML COMPONENTS VoiceXML)` resolve only
`Salts::XmlParser`, and publishes an honest support matrix that marks all media
features deferred/unsupported.

- [ ] Add the install-consumer expectation first and record RED.
- [ ] Export/install the header and target, component discovery, and dependency
  metadata. Add a C consumer that compiles and runs an exit-only document.
- [ ] Document the accepted grammar, ownership/lifecycle, non-media limitation,
  and pointers to issues #41/#42/#47.
- [ ] Run Release tests, install preset, and the external consumer; record GREEN.
  Commit.

### Task 5: Whole-branch verification

- [ ] Configure/build/test Release from a clean build tree.
- [ ] Install to a temporary prefix and run external C and C++ consumers.
- [ ] Confirm `rg` finds no media adapter/ticket/token APIs in public VoiceXML
  headers and no VoiceXML dependency from SCXML or CCXML.
- [ ] Generate a whole-branch review package and obtain a clean independent
  spec/code-quality review before branch integration.
