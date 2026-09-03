# CCXML Literal Send Delay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded literal CCXML `<send delay>` support through the shared SCXML Event I/O delay contract.

**Architecture:** Extend the shared CSS-time parser, decode and validate the CCXML literal during compilation, retain its bounded bytes, and store the exact millisecond value on the send action row. Mark only programs with nonzero delays as requiring delayed-send capability, then forward the precomputed value through the existing transactional `scxml_send_request` path.

**Tech Stack:** C11, Salts XML, Salts CFlow statecharts, shared SCXML Event I/O, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/specs/ccxml-send-delay-design.md`

## Global Constraints

- Admit only quoted string-literal CCXML delay expressions in this slice.
- Parse decoded CSS time values into exact `uint64_t` milliseconds with no dispatch-time allocation.
- Require `SCXML_EVENT_IO_CAP_DELAYED_SEND` only when a program contains a nonzero delay.
- Preserve SEND-only adapter compatibility for omitted and zero delays.
- Keep `sendid`, `namelist`, inline content, dynamic expressions, and `<cancel>` unsupported.
- Preserve the existing CFlow prepare/commit/reverse-discard and adapter lifecycle contracts.

---

### Task 1: Specify compiler admission with failing tests

**Files:**
- Create: `docs/specs/ccxml-send-delay-design.md`
- Modify: `tests/ccxml_send_test.c`

**Interfaces:**
- Consumes: CCXML `send/@delay` string-literal expressions.
- Produces: deterministic compile status for exact millisecond values.

- [x] **Step 1: Add valid literal tests**

Add compiler cases for `'250ms'`, `'1s'`, `'1.5s'`, `'.5s'`, `'+1.5s'`, entity-encoded expression quotes, and `'0s'`; each must compile as `CCXML_OK`.

- [x] **Step 2: Add invalid literal tests**

Add cases for `delay='dynamic'`, `''`, `'-1s'`, `'1'`, `'1m'`, `'1.0001s'`, `'.s'`, `'+'`, and overflowing seconds; require nonliteral values to return `CCXML_UNSUPPORTED_FEATURE` and malformed literal values to return `CCXML_INVALID_STRUCTURE`.

- [x] **Step 3: Run the focused test and confirm RED**

Run `cmake --build --preset win-dev-user --target ccxml_send_test` and `ctest --preset win-dev-user -R ccxml_send_test --output-on-failure`. Existing rejection of every `delay` attribute must make the valid cases fail.

### Task 2: Implement shared parsing and bounded compilation

**Files:**
- Modify: `src/scxml_analyze.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`
- Test: `tests/ccxml_send_test.c`

**Interfaces:**
- Consumes: `bool scxml_time_parse_ms(salts_xml_string_view, uint64_t *)`.
- Produces: `ccxml_action_row.delay_ms` and `ccxml_program_impl.uses_delayed_send`.

- [x] **Step 1: Extend the shared CSS-time parser**

Allow one leading `+` and a leading decimal point for seconds while retaining overflow checks, integral-millisecond rules, and the three-digit fractional precision limit.

- [x] **Step 2: Admit and validate `delay` in the CCXML first pass**

Recognize one unqualified `delay` attribute, decode it with the existing send-literal helper, parse its inner bytes through `scxml_time_parse_ms`, reject malformed values, and include the decoded literal plus NUL in checked `max_name_bytes` accounting.

- [x] **Step 3: Materialize the compiled action row**

Copy the decoded delay literal into program storage, parse it into `delay_ms`, and set `uses_delayed_send` only when the parsed value is nonzero.

- [x] **Step 4: Run focused tests and confirm GREEN**

Rebuild and run `ccxml_send_test`; every compiler case must pass under the Dev/ASan preset.

### Task 3: Enforce capabilities and forward the delay

**Files:**
- Modify: `src/ccxml_session.c`
- Modify: `tests/ccxml_send_test.c`

**Interfaces:**
- Consumes: `ccxml_program_impl.uses_delayed_send` and `ccxml_action_row.delay_ms`.
- Produces: exact `scxml_send_request.delay_ms` with program-sensitive adapter admission.

- [x] **Step 1: Add runtime and capability tests**

Test exact forwarding of 250 and 1500 milliseconds, rejection of a delayed program by a SEND-only adapter, acceptance by SEND|DELAYED_SEND, and acceptance of omitted/zero-delay programs by SEND-only adapters.

- [x] **Step 2: Verify runtime tests are RED**

Run the focused test before implementation; delayed compilation or capability checks must fail.

- [x] **Step 3: Implement delayed capability admission and request forwarding**

After general send adapter validation, require `SCXML_EVENT_IO_CAP_DELAYED_SEND` when `uses_delayed_send` is true, and initialize `scxml_send_request.delay_ms` from the action row.

- [x] **Step 4: Run focused tests and confirm GREEN**

Rebuild and run `ccxml_send_test` with zero failures.

### Task 4: Document, verify, and publish the increment

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-send-design.md`
- Modify: `docs/superpowers/plans/2026-09-03-ccxml-send-delay.md`

**Interfaces:**
- Produces: accurate supported/deferred feature documentation and a reviewed branch update.

- [x] **Step 1: Update feature documentation**

Move literal `delay` into the supported send profile, describe capability negotiation, and leave dynamic delay, `sendid`, `namelist`, inline content, and cancel explicitly deferred.

- [x] **Step 2: Run source-tree verification**

Fresh-configure, build, and test `win-dev-user`, `win-release-user`, `win-release-quickjs-user`, and `win-release-chttp-user`; require all registered tests to pass.

- [x] **Step 3: Run static and review gates**

Run `git diff --check`, inspect the complete branch diff, and request an independent code review. Resolve all critical, important, and minor findings, then rerun affected tests.

- [x] **Step 4: Commit and update the existing pull request**

Commit as `feat(ccxml): support literal send delay`, push `feat/ccxml-send`, and verify pull request 36 remains open with the new head. Do not merge without an explicit user request.
