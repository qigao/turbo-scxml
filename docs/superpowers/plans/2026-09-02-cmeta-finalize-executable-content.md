# CMeta Finalize Executable Content Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Allow CMeta `<finalize>` blocks to execute the same supported SCXML executable content as other blocks, including `if/elseif/else`, `foreach`, `raise`, `send`, `cancel`, `assign`, and `log`.

**Architecture:** Keep invocation finalization inside the existing CFlow V4 host transaction. CMeta state writes continue through `cflow_statechart_host_context_edit_state`, raised events through the host internal-event journal, and sends/cancels through staged effect tickets. The compiler will stop imposing finalize-only restrictions; it will continue emitting finalize blocks as unbound `scxml_block` descriptors owned by invocation descriptors.

**Tech Stack:** C11, TurboSCXML, TurboUtils CMeta, CFlow statechart runtime, Rocida TinyTest, CMake presets, MSVC/Ninja.

---

### Task 1: Add failing finalize regression tests

**Files:**
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `tests/scxml_foreach_test.c`

- [x] Update the existing CMeta conditional test so a valid `<if>` inside `<finalize>` is expected to compile.
- [x] Add an invocation-return test proving finalize can stage an assignment, raise an internal event, and prepare/commit `send` and `cancel` effects selected by a CMeta condition.
- [x] Update the existing foreach finalize rejection test into an invocation-return test proving `foreach` plus nested `if` executes against staged CMeta state.
- [x] Build the two test targets:

```powershell
$dev = '"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_cmeta_test scxml_foreach_test'
cmd.exe /d /s /c $dev
```

- [x] Run the focused tests and confirm they fail because finalize content is rejected:

```powershell
$dev = '"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user -R "^(scxml_cmeta_test|scxml_foreach_test)$" --output-on-failure'
cmd.exe /d /s /c $dev
```

### Task 2: Remove finalize-only analyzer restrictions

**Files:**
- Modify: `src/scxml_analyze.c`

- [x] Remove the CMeta finalize rejection for conditional expressions.
- [x] Remove the CMeta finalize rejection for foreach descriptors.
- [x] Remove the finalize-only rejection for `raise`, `send`, and `cancel` at top level and inside conditional content.
- [x] Preserve finalize block counting: finalize remains an invocation-owned block and is not added to the CFlow machine executable-binding table.
- [x] Recurse literal Event collection through `invoke` and `finalize`, including nested conditions and foreach loops.
- [x] Build and rerun the focused tests until both pass:

```powershell
$dev = '"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_cmeta_test scxml_foreach_test && ctest --preset win-release-user -R "^(scxml_cmeta_test|scxml_foreach_test)$" --output-on-failure'
cmd.exe /d /s /c $dev
```

### Task 3: Verify transactional behavior and compatibility

**Files:**
- Modify if required by a failing regression: `src/scxml_runtime.c`
- Modify if required by a failing regression: `tests/scxml_cmeta_test.c`
- Modify if required by a failing regression: `tests/scxml_foreach_test.c`
- Modify: `tests/scxml_test.c`
- Modify: `docs/specs/scxml-invoke-finalize-design.md`
- Modify: `docs/specs/scxml-typed-ast-host-integration.md`

- [x] Confirm finalize assignments are visible to later finalize conditions and to transition selection for the returned event.
- [x] Confirm raised internal events and send/cancel effects use the existing host transaction paths and are not published before commit.
- [x] Confirm the existing W3C invocation-finalize tests 233 and 234 still pass.
- [x] Run the full Release suite:

```powershell
$dev = '"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure'
cmd.exe /d /s /c $dev
```

- [x] Inspect `git diff --check`, `git status --short`, and the final diff before handoff.
