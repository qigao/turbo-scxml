# VoiceXML Core MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an installable bounded VoiceXML core that compiles `form/block/prompt/exit` documents and executes them through a transactional prompt adapter until wait or exit.

**Architecture:** `TurboSCXML::VoiceXML` owns an immutable measured program and a single-owner synchronous FIA session. The session commits one nonblocking prompt ticket, waits for a token-matched completion, and then resumes to the next prompt or terminal exit; asynchronous serialization and CCXML integration remain separate roadmap slices.

**Tech Stack:** C11, Salts XmlParser, Salts CFlow effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/voicexml-architecture-design.md`

**Tracking:** [GitHub issue #42](https://github.com/qigao/turbo-scxml/issues/42),
under [VoiceXML roadmap #41](https://github.com/qigao/turbo-scxml/issues/41).

## Global Constraints

- Accept only the VoiceXML namespace `http://www.w3.org/2001/vxml` and root versions `2.0` or `2.1`.
- Support only root `form`, form `block`, and block `prompt`/`exit`; reject every unsupported element or attribute explicitly.
- Allow multiple forms, require at least one form and one block, enter the first form, and reject duplicate nonempty form IDs.
- Trim leading/trailing XML whitespace from decoded prompt text, retain interior UTF-8 bytes exactly, and reject empty or NUL-containing results.
- Use `salts_xml_default_limits()`, `max_forms = 64`, `max_blocks = 1024`, `max_actions = 4096`, and `max_text_bytes = 1024 * 1024` as configurable defaults.
- Use checked arithmetic for counts, decoded bytes plus trailing NUL, row storage, alignment, and the single final allocation.
- Leave every output handle zeroed after failure and destroy every temporary/owned allocation exactly once.
- Keep the core single-owner and synchronous; do not create a thread, executor, mailbox, network client, TTS engine, or CCXML dependency.
- Permit exactly one in-flight prompt. Adapter request bytes are callback-borrowed; accepted prepare transfers one complete move-only CFlow ticket.
- Commit is nonblocking and infallible. Reject zero/stale/duplicate completions without changing the program counter.
- Close adapter admission exactly once and return `VXML_BUSY` from destroy until the adapter is quiescent.
- Do not add XPath, ECMAScript approximation, CMeta expressions, QuickJS, SSML, audio, ASR, SRGS, navigation, or exit payloads in this plan.

---

### Task 1: Establish the public VoiceXML target and ABI

**Files:**
- Create: `include/voicexml/voicexml.h`
- Create: `src/voicexml_internal.h`
- Create: `src/voicexml_program.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/voicexml_program_test.c`

**Interfaces:**
- Consumes: `salts_xml_limits`, `salts_xml_location`, and `cflow_statechart_effect_ticket` from installed Salts targets.
- Produces: `vxml_limits`, `vxml_program`, `vxml_prompt_adapter_v1`, `vxml_session`, and the exact functions declared below for all later tasks.

- [ ] **Step 1: Add a public-header/default-limits test that cannot build yet**

Add `voicexml_program_test` to `tests/CMakeLists.txt`, linked with
`TurboSCXML::VoiceXML`, and create this first test:

```c
#include <voicexml/voicexml.h>
#include "tinytest.h"

static void voicexml_program_spec(void) {
    describe("VoiceXML program", {
        it("publishes positive bounded defaults", {
            const vxml_limits limits = vxml_default_limits();
            check_equal(limits.max_forms, (size_t)64u);
            check_equal(limits.max_blocks, (size_t)1024u);
            check_equal(limits.max_actions, (size_t)4096u);
            check_equal(limits.max_text_bytes, (size_t)(1024u * 1024u));
        });
    });
}

int main(void) {
    return run_spec(voicexml_program_spec);
}
```

- [ ] **Step 2: Configure to verify RED**

Run:

```powershell
cmake --fresh --preset win-release-user
```

Expected: configure or generation fails because `TurboSCXML::VoiceXML` and
`include/voicexml/voicexml.h` do not exist.

- [ ] **Step 3: Add the target and exact public ABI**

Create `turbo_voicexml` from `src/voicexml_program.c`, add alias
`TurboSCXML::VoiceXML`, require C11, expose `include`, and link
`Salts::CFlow` plus `Salts::XmlParser` publicly. Add this public surface:

```c
#ifndef TURBO_VOICEXML_H
#define TURBO_VOICEXML_H

#include <cflow/statechart_instance.h>
#include <xml_parser/xml_parser.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIAGNOSTIC_CAPACITY 256u
#define VXML_PROMPT_ADAPTER_ABI_V1 1u

typedef enum vxml_status {
    VXML_OK = 0,
    VXML_INVALID_ARGUMENT,
    VXML_XML_ERROR,
    VXML_ALLOCATION_FAILED,
    VXML_LIMIT_EXCEEDED,
    VXML_INVALID_NAMESPACE,
    VXML_INVALID_VERSION,
    VXML_INVALID_STRUCTURE,
    VXML_UNSUPPORTED_FEATURE,
    VXML_ADAPTER_ERROR,
    VXML_INVALID_CONTRACT,
    VXML_INVALID_STATE,
    VXML_CLOSED,
    VXML_BUSY
} vxml_status;

typedef struct vxml_limits {
    salts_xml_limits xml;
    size_t max_forms;
    size_t max_blocks;
    size_t max_actions;
    size_t max_text_bytes;
} vxml_limits;

typedef struct vxml_diagnostic {
    vxml_status status;
    salts_xml_location location;
    char message[VXML_DIAGNOSTIC_CAPACITY];
} vxml_diagnostic;

typedef struct vxml_program { void *impl; } vxml_program;

vxml_limits vxml_default_limits(void);
vxml_status vxml_compile(
    vxml_program *out, const char *input, size_t input_size,
    const vxml_limits *limits, vxml_diagnostic *diagnostic);
void vxml_program_destroy(vxml_program *program);

typedef enum vxml_adapter_status {
    VXML_ADAPTER_ACCEPTED = 0,
    VXML_ADAPTER_ERROR_EXECUTION,
    VXML_ADAPTER_INVALID_CONTRACT
} vxml_adapter_status;

typedef enum vxml_prompt_completion {
    VXML_PROMPT_COMPLETED = 0,
    VXML_PROMPT_FAILED
} vxml_prompt_completion;

typedef struct vxml_prompt_request {
    uint64_t token;
    const char *text;
    size_t text_size;
} vxml_prompt_request;

typedef struct vxml_prompt_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_adapter_status (*prepare_prompt)(
        void *user, const vxml_prompt_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} vxml_prompt_adapter_v1;

typedef enum vxml_session_state {
    VXML_SESSION_READY = 0,
    VXML_SESSION_RUNNING,
    VXML_SESSION_WAITING_PROMPT,
    VXML_SESSION_EXITED,
    VXML_SESSION_FAILED,
    VXML_SESSION_CLOSED
} vxml_session_state;

typedef struct vxml_session_config {
    const vxml_program *program;
    const vxml_prompt_adapter_v1 *prompt;
    void *prompt_user;
} vxml_session_config;

typedef struct vxml_session { void *impl; } vxml_session;

vxml_status vxml_session_init(
    vxml_session *session, const vxml_session_config *config);
vxml_status vxml_session_start(vxml_session *session);
vxml_status vxml_session_complete_prompt(
    vxml_session *session, uint64_t token,
    vxml_prompt_completion completion);
vxml_session_state vxml_session_get_state(const vxml_session *session);
const char *vxml_session_error(const vxml_session *session);
void vxml_session_close(vxml_session *session);
vxml_status vxml_session_destroy(vxml_session *session);

#ifdef __cplusplus
}
#endif
#endif
```

Create private action/form/block row declarations in
`src/voicexml_internal.h`. Implement only `vxml_default_limits` and a
zero-handle-safe `vxml_program_destroy` in this task; later tasks fill compile
and session functions.

- [ ] **Step 4: Build and verify GREEN**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target voicexml_program_test
ctest --preset win-release-user -R voicexml_program_test --output-on-failure
```

Expected: the new test passes and all public declarations compile as C11.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/voicexml/voicexml.h \
  src/voicexml_internal.h src/voicexml_program.c \
  tests/voicexml_program_test.c
git commit -m "feat(voicexml): establish core public boundary"
```

### Task 2: Compile the bounded form/block/prompt/exit profile

**Files:**
- Modify: `src/voicexml_internal.h`
- Modify: `src/voicexml_program.c`
- Modify: `tests/voicexml_program_test.c`

**Interfaces:**
- Consumes: `vxml_compile`, `vxml_limits`, Salts XML node/location APIs, and the immutable row declarations from Task 1.
- Produces: a `vxml_program_impl` with `forms`, `blocks`, `actions`, and one NUL-terminated text arena; later runtime code reads it without XML ownership.

- [ ] **Step 1: Add successful compilation and source-independence tests**

Add tests that compile this document, overwrite its input buffer, inspect the
private rows through `voicexml_internal.h`, and destroy the program:

```c
char source[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><prompt>Hello &amp; goodbye</prompt>"
    "<exit/></block></form></vxml>";
vxml_program program = {0};
vxml_diagnostic diagnostic = {0};

check_equal(
    vxml_compile(&program, source, strlen(source), NULL, &diagnostic),
    VXML_OK);
memset(source, 'x', sizeof(source) - 1u);
check_not_null(program.impl);
check_equal(((const vxml_program_impl *)program.impl)->form_count, (size_t)1u);
check_equal(((const vxml_program_impl *)program.impl)->block_count, (size_t)1u);
check_equal(((const vxml_program_impl *)program.impl)->action_count, (size_t)2u);
check_equal(
    ((const vxml_program_impl *)program.impl)->actions[0].kind,
    VXML_ACTION_PROMPT);
check_equal(
    ((const vxml_program_impl *)program.impl)->actions[0].text,
    "Hello & goodbye");
vxml_program_destroy(&program);
check_null(program.impl);
```

Add a second success case for `version='2.0'`, two forms, unique IDs, and
leading/trailing prompt whitespace trimming.

- [ ] **Step 2: Add strict rejection and exact-limit tests**

Cover each status and source location with separate `it` cases:

- wrong/missing namespace -> `VXML_INVALID_NAMESPACE`;
- missing or unsupported version -> `VXML_INVALID_VERSION`;
- no form, form without block, empty block -> `VXML_INVALID_STRUCTURE`;
- duplicate form ID or invalid NCName -> `VXML_INVALID_STRUCTURE`;
- `menu`, `field`, nested prompt markup, prompt attributes, block attributes,
  or `exit@namelist` -> `VXML_UNSUPPORTED_FEATURE`;
- empty trimmed prompt, non-whitespace exit child, malformed XML, and embedded
  NUL input -> the matching structure/XML error;
- exactly 64 forms succeeds while 65 returns `VXML_LIMIT_EXCEEDED`;
- exact decoded prompt bytes plus NUL succeed while one extra decoded byte
  returns `VXML_LIMIT_EXCEEDED`;
- every zero configured limit returns `VXML_INVALID_ARGUMENT` and leaves
  `program.impl == NULL`.

- [ ] **Step 3: Run the focused test to verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target voicexml_program_test
ctest --preset win-release-user -R voicexml_program_test --output-on-failure
```

Expected: compile cases fail because `vxml_compile` has no implementation.

- [ ] **Step 4: Implement measure-then-copy compilation**

Use these private representations:

```c
typedef enum vxml_action_kind {
    VXML_ACTION_PROMPT = 1,
    VXML_ACTION_EXIT = 2
} vxml_action_kind;

typedef struct vxml_action_row {
    vxml_action_kind kind;
    const char *text;
    size_t text_size;
    salts_xml_location location;
} vxml_action_row;

typedef struct vxml_block_row {
    size_t action_begin;
    size_t action_count;
} vxml_block_row;

typedef struct vxml_form_row {
    const char *id;
    size_t id_size;
    size_t block_begin;
    size_t block_count;
} vxml_form_row;

typedef struct vxml_program_impl {
    void *storage;
    vxml_form_row *forms;
    size_t form_count;
    vxml_block_row *blocks;
    size_t block_count;
    vxml_action_row *actions;
    size_t action_count;
    char *strings;
    size_t string_bytes;
} vxml_program_impl;
```

Parse with `salts_xml_parse`, validate structure while measuring decoded
strings and rows, compute aligned offsets with checked addition/multiplication,
allocate one zeroed block, then walk the same nodes to copy rows and strings.
Use byte-and-size equality for duplicate IDs. On any mismatch between measure
and copy, destroy the candidate and return `VXML_INVALID_CONTRACT`.

- [ ] **Step 5: Rebuild and verify GREEN**

Run the focused build and CTest command from Step 3. Expected: every compiler,
limit, ownership, and diagnostic case passes.

- [ ] **Step 6: Commit**

```bash
git add src/voicexml_internal.h src/voicexml_program.c \
  tests/voicexml_program_test.c
git commit -m "feat(voicexml): compile the core document profile"
```

### Task 3: Start a session and publish one transactional prompt

**Files:**
- Create: `src/voicexml_session.c`
- Create: `tests/voicexml_session_test.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: immutable `vxml_program_impl` actions and the copied `vxml_prompt_adapter_v1` table.
- Produces: `vxml_session_init`, `vxml_session_start`, `vxml_session_get_state`, `vxml_session_error`, and a live nonzero prompt token retained by session state.

- [ ] **Step 1: Add a prompt provider probe and failing start test**

The test provider copies prompt bytes during prepare, stores the token, and
returns a ticket whose callbacks increment exact counters. Compile a
`prompt/exit` document, initialize a session, and assert:

```c
check_equal(vxml_session_init(&session, &config), VXML_OK);
check_equal(vxml_session_get_state(&session), VXML_SESSION_READY);
check_equal(vxml_session_start(&session), VXML_OK);
check_equal(vxml_session_get_state(&session), VXML_SESSION_WAITING_PROMPT);
check_equal(provider.prepare_count, (size_t)1u);
check_equal(provider.commit_count, (size_t)1u);
check_equal(provider.discard_count, (size_t)0u);
check_equal(provider.text, "Hello");
check_true(provider.token != UINT64_C(0));
```

Also assert that a second `start` returns `VXML_INVALID_STATE` without another
provider call.

- [ ] **Step 2: Add adapter admission and ticket-contract tests**

Add cases for NULL config/program/adapter, wrong ABI, a struct truncated before
`is_quiescent`, NULL operations, provider rejection, accepted zero ticket,
accepted ticket missing one callback, and a program destroyed before init.
Provider rejection must leave no ticket and change the session to
`VXML_SESSION_FAILED`; malformed accepted tickets return
`VXML_INVALID_CONTRACT` and discard a complete transferred ticket when that is
possible.

- [ ] **Step 3: Run the session test to verify RED**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target voicexml_session_test
ctest --preset win-release-user -R voicexml_session_test --output-on-failure
```

Expected: link fails for the unimplemented session functions.

- [ ] **Step 4: Implement initialization and run-to-wait**

Create a private session with copied operations, borrowed program/user, action
cursor, state, next token, waiting token, close flag, and fixed error buffer.
Implement this action loop:

```c
while (impl->action_index < impl->entry_action_end) {
    const vxml_action_row *action =
        &program->actions[impl->action_index];
    if (action->kind == VXML_ACTION_EXIT) {
        impl->state = VXML_SESSION_EXITED;
        return VXML_OK;
    }
    if (action->kind == VXML_ACTION_PROMPT)
        return prepare_and_commit_prompt(impl, action);
    return fail_session(impl, VXML_INVALID_CONTRACT,
                        "unknown VoiceXML action");
}
impl->state = VXML_SESSION_EXITED;
return VXML_OK;
```

Increment and wrap-check the token before provider prepare. Validate adapter
status and the complete ticket. Commit exactly once only after validation,
retain no provider ticket, then publish `WAITING_PROMPT`. Keep the cursor on
the prompt until its successful completion in Task 4.

- [ ] **Step 5: Rebuild and verify GREEN**

Run the focused commands from Step 3. Expected: all initialization, provider,
ticket, and run-to-wait cases pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt src/voicexml_session.c \
  tests/voicexml_session_test.c
git commit -m "feat(voicexml): publish transactional prompts"
```

### Task 4: Resume prompts and enforce terminal lifecycle

**Files:**
- Modify: `src/voicexml_session.c`
- Modify: `tests/voicexml_session_test.c`

**Interfaces:**
- Consumes: the waiting token/cursor produced by Task 3 and adapter close/quiescence operations.
- Produces: `vxml_session_complete_prompt`, explicit/implied exit, exact-once close, busy destruction, and deterministic stale-completion behavior.

- [ ] **Step 1: Add failing completion and document-order tests**

Compile two prompts followed by exit. Start, complete the first stored token,
assert that the second prompt is prepared with a larger token, complete it,
and assert `VXML_SESSION_EXITED`. Verify exact text order and two prepare/two
commit/zero discard callbacks. Add a document without explicit exit and prove
entry-form exhaustion also exits.

- [ ] **Step 2: Add failure, stale-token, and shutdown tests**

Cover:

- completion token zero, wrong token, and duplicate old token return
  `VXML_INVALID_STATE` without moving the cursor;
- `VXML_PROMPT_FAILED` changes to `VXML_SESSION_FAILED` and stores a stable
  nonempty error;
- completion after `EXITED`, `FAILED`, or `CLOSED` is rejected;
- close from `READY` and `WAITING_PROMPT` calls adapter close once;
- repeated close remains exact-once;
- destroy before quiescence returns `VXML_BUSY` and preserves `session.impl`;
- destroy after quiescence succeeds, clears the handle, and never destroys the
  borrowed program or adapter user;
- one session can be destroyed while another session shares the immutable
  program and adapter table.

- [ ] **Step 3: Run the focused test to verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target voicexml_session_test
ctest --preset win-release-user -R voicexml_session_test --output-on-failure
```

Expected: completion and lifecycle assertions fail because Task 3 implements
only start-to-wait.

- [ ] **Step 4: Implement completion, exit, close, and destruction**

On a matching completed token, clear the waiting token, increment the action
cursor once, change to `RUNNING`, and re-enter the action loop. On failed
completion, clear the token and terminate `FAILED` without advancing. Close
must set admission closed before calling the adapter and must never invoke a
provider callback while holding an internal lock; the core itself remains
single-owner and needs no mutex. Destruction follows:

```c
vxml_session_close(session);
if (!impl->prompt.is_quiescent(impl->prompt_user)) return VXML_BUSY;
memset(impl, 0, sizeof(*impl));
free(impl);
session->impl = NULL;
return VXML_OK;
```

Preserve the first runtime error and return it from `vxml_session_error` until
successful destruction.

- [ ] **Step 5: Rebuild and verify GREEN**

Run the focused commands from Step 3. Expected: all prompt sequencing,
stale-completion, terminal-state, and shutdown cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/voicexml_session.c tests/voicexml_session_test.c
git commit -m "feat(voicexml): complete the core session lifecycle"
```

### Task 5: Export, document, and verify the MVP package

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `README.md`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Create: `tests/install_consumer/voicexml_main.c`
- Create: `tests/install_consumer/voicexml_main.cpp`
- Modify: `docs/specs/voicexml-architecture-design.md`

**Interfaces:**
- Consumes: the completed `TurboSCXML::VoiceXML` target and public header.
- Produces: installed `TurboSCXML COMPONENTS VoiceXML` discovery plus C and C++ consumer evidence.

- [ ] **Step 1: Add failing install-consumer checks**

Extend the install consumer to require `TurboSCXML::VoiceXML`. The C consumer
calls `vxml_default_limits`, compiles the canonical document, destroys it, and
returns nonzero on any failure. The C++ consumer includes the header inside a
normal C++ translation unit, zero-initializes `vxml_program`, compiles the same
document, and destroys it.

- [ ] **Step 2: Run install verification to verify RED**

Run:

```powershell
cmake --build --preset install-win-release-user
$env:TURBOSCXML_ROOT = Join-Path $env:PROJECT_ROOT `
    'external/pkgs/turboscxml/release'
$env:SALTS_ROOT = Join-Path $env:PROJECT_ROOT `
    'external/pkgs/salts/release'
$consumerBuild = 'build/install-consumer-release'
cmake --fresh -S tests/install_consumer -B $consumerBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$PWD/vcpkg_installed/x64-windows"
cmake --build $consumerBuild
```

Expected: the installed package does not contain the target/header and the
consumer fails to configure or build.

- [ ] **Step 3: Export the target and document the supported profile**

Install `turbo_voicexml` into `TurboSCXMLTargets`, install
`include/voicexml/voicexml.h`, set `TurboSCXML_VoiceXML_FOUND TRUE`, and add a
build-time exact dependency-contract assertion for
`Salts::CFlow;Salts::XmlParser`. Update README with the canonical document,
single-owner completion protocol, adapter ownership, explicit unsupported
surface, and links to the architecture spec and roadmap issue.

- [ ] **Step 4: Run focused, full, and installed-package verification**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user
$env:TURBOSCXML_ROOT = Join-Path $env:PROJECT_ROOT `
    'external/pkgs/turboscxml/release'
$env:SALTS_ROOT = Join-Path $env:PROJECT_ROOT `
    'external/pkgs/salts/release'
$consumerBuild = 'build/install-consumer-release'
cmake --fresh -S tests/install_consumer -B $consumerBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$PWD/vcpkg_installed/x64-windows"
cmake --build $consumerBuild
& "$consumerBuild/turboscxml_voicexml_install_consumer.exe"
& "$consumerBuild/turboscxml_voicexml_install_consumer_cpp.exe"
git diff --check main...HEAD
```

Expected: every test passes, the external C and C++ consumers link the
installed target, and diff check prints no output.

- [ ] **Step 5: Request review and commit**

Request a read-only review of `main...HEAD`. Fix every Critical or Important
finding and rerun Step 4. Then commit:

```bash
git add CMakeLists.txt cmake/TurboSCXMLConfig.cmake.in README.md \
  tests/install_consumer/CMakeLists.txt \
  tests/install_consumer/voicexml_main.c \
  tests/install_consumer/voicexml_main.cpp \
  docs/specs/voicexml-architecture-design.md
git commit -m "docs(voicexml): publish the core MVP profile"
```
