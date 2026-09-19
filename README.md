# TurboSCXML

**W3C SCXML compiled to Salts CFlow Statechart execution.**

TurboSCXML is the state-machine/statechart application layer of the Salts ecosystem. It compiles SCXML documents into CFlow Statechart programs and provides bounded, versioned host-event I/O, invocation, and CMeta data-model integration.

The core design rule is simple:

```text
SCXML syntax / semantics
        ↓
TurboSCXML compiler + session
        ↓
Salts CFlow Statechart
        ↓
explicit bounded execution
```

TurboSCXML does not create a second state-machine runtime beside CFlow.

**Tags:** C11 · SCXML · state-machine · statechart · W3C · CFlow · CMeta · CCXML · VoiceXML · event-driven

## Built on Salts

TurboSCXML reuses the Salts execution and type foundations:

- **CFlow** for Machine/Statechart semantics and execution.
- **CMeta** for typed data-model integration.
- **CSerde / CBind** for serialization/binding primitives used by the implementation.
- **Core** for common systems support.
- **TinyTest** for repository tests.

Parser/query and HTTP ownership follow the current ecosystem boundary:

- **SaltsUtils** owns QueryVM and XML/parser components while preserving their existing `Salts::*` target names.
- **CHTTP** owns HTTP client/server infrastructure through the standalone `CHttp::*` package targets.

TurboSCXML resolves those packages explicitly from their installed roots and does not fall back to the former core-Salts ownership model.

## Ecosystem role

```text
Salts
  ├── CMeta
  └── CFlow Statechart
        ↓
    TurboSCXML
        ↓
 SCXML / CCXML / VoiceXML applications
```

TurboSCXML belongs in the **framework/application layer**, not in the Salts kernel.

The dependency direction must remain one-way:

```text
TurboSCXML
  -> Salts
  -> SaltsUtils-owned parser/query targets
  -> standalone CHTTP only when an HTTP adapter is enabled
```

CFlow must remain independent of XML and SCXML.

## Repository ownership

TurboSCXML owns:

- SCXML parsing/compilation semantics;
- SCXML session lifecycle;
- host-event/invocation adapter contracts;
- CMeta data-model adaptation;
- CCXML support;
- VoiceXML profiles;
- W3C conformance fixtures/corpus;
- optional HTTP resource/Event I/O adapters.

TurboSCXML does not own:

- the generic CFlow Statechart runtime;
- CMeta;
- generic parser/query infrastructure;
- generic HTTP infrastructure;
- product authentication/persistence/deployment policy.

## Public components

| CMake target | Responsibility |
| --- | --- |
| `TurboSCXML::SCXML` | SCXML compiler/session/runtime facade over CFlow Statechart |
| `TurboSCXML::CCXML` | CCXML layer |
| `TurboSCXML::VoiceXML` | bounded VoiceXML core profile |
| `TurboSCXML::VoiceXMLCMeta` | optional CMeta-backed VoiceXML data-model profile |
| `TurboSCXML::CHttpResource` | optional host-authorized HTTP resource adapter |
| `TurboSCXML::CHttpEventIO` | optional BasicHTTP Event I/O processor |

The public C API is available through:

```c
#include <scxml/scxml.h>
```

Functions and types use the `scxml_*` naming convention.

## SCXML execution model

TurboSCXML compiles source documents into immutable program state and creates explicit session state for execution.

The model is intentionally bounded and caller-driven:

- program ownership is explicit;
- session ownership is explicit;
- host events are explicit inputs;
- no hidden worker/event-loop is created by the core;
- statechart execution is delegated to CFlow semantics;
- adapters remain versioned boundaries rather than implicit global services.

## Build

Requirements:

- CMake 3.20+
- Ninja
- a C11-capable compiler
- matching installed Salts SDK
- matching installed SaltsUtils SDK
- standalone CHTTP only when an optional CHTTP adapter is enabled
- Visual Studio 2022 developer environment on Windows

Set:

```text
PROJECT_ROOT
VCPKG_ROOT
SALTS_ROOT
SALTS_UTILS_ROOT
HTTP_SERVICES_ROOT   # only for CHTTP-enabled profiles
```

The historical `HTTP_SERVICES_ROOT` variable names the installed CHTTP package root. It does not imply a separate legacy runtime boundary.

### Windows Release

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
cmake --build --preset install-win-release-user
```

### Linux Release

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
cmake --build --preset install-linux-release-user
```

The repository's versioned `CMakeUserPresets.json` controls the install profile.

## CMake consumption

```cmake
find_package(TurboSCXML CONFIG REQUIRED
  PATHS "${TURBOSCXML_ROOT_PATH}"
  NO_DEFAULT_PATH)

target_link_libraries(app PRIVATE TurboSCXML::SCXML)
```

The package is intended to fail fast when its configured dependency profile is missing. It should not silently search a different profile or revive legacy package ownership.

## Optional QuickJS profile

`TURBOSCXML_ENABLE_QUICKJS` enables the bounded QuickJS-backed SCXML data-model profile.

The feature is optional and does not redefine the core CFlow Statechart ownership boundary.

## Optional CHTTP adapters

Two optional adapters exist:

- `TurboSCXML::CHttpResource` — host-authorized synchronous resource access.
- `TurboSCXML::CHttpEventIO` — bounded BasicHTTP Event I/O.

They are adapter layers only. HTTP ownership belongs to the standalone CHTTP package.

`TurboSCXML::CHttpResource` publicly depends on `CHttp::Client`. `TurboSCXML::CHttpEventIO` publicly depends on both `CHttp::Client` and `CHttp::Server`. The old `Salts::CHTTP` ownership assumption is not used.

## VoiceXML core profile

TurboSCXML also exports:

```text
<voicexml/voicexml.h>
TurboSCXML::VoiceXML
```

The current VoiceXML core is an intentionally bounded, non-media profile. It does not claim full VoiceXML 2.0/2.1 conformance.

Its accepted document subset is explicit and fail-fast. Programs and sessions have single-owner handles, no implicit thread creation, and deterministic lifecycle transitions.

A successful compile owns an immutable representation independent of the source buffer. A session borrows its program, so the program must outlive the session.

## VoiceXML CMeta profile

When `TURBOSCXML_ENABLE_VOICEXML_CMETA` is enabled, the package also exports:

```text
<voicexml/cmeta.h>
TurboSCXML::VoiceXMLCMeta
```

This profile integrates VoiceXML data-model behavior with CMeta while preserving the same explicit program/session ownership rules.

## Design principles

- **One state-machine foundation.** SCXML lowers to CFlow Statechart; it does not create a parallel execution kernel.
- **Explicit ownership.** Programs, sessions, adapters, and results have concrete owners/lifetimes.
- **Bounded execution.** Limits and host-driven progress remain visible.
- **Fail-fast syntax/semantics.** Unsupported or invalid structures are rejected rather than approximated.
- **No hidden I/O runtime.** Core execution is caller-driven; transport adapters are explicit.
- **Acyclic package boundaries.** Salts does not depend on TurboSCXML.
- **Separate domain ownership.** XML/query and HTTP infrastructure remain owned by their ecosystem packages.

---

**Salts CFlow provides the Statechart machine. TurboSCXML provides W3C SCXML semantics on top of it.**
