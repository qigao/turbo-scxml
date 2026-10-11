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
- **CMeta** for typed data-model and provider-interface reflection.
- **CSerde** for the canonical token stream used at resource boundaries.
- **DataBind** from SaltsUtils for bounded CMeta-native data binding.
- **Core** for common systems support.
- **TinyTest** for repository tests.

Parser/query and HTTP ownership follow the current ecosystem boundary:

- **SaltsUtils** owns QueryVM, XML/parser components, and DataBind; installed consumers use `Salts::DataBind`.
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
  -> SaltsUtils-owned parser/query/DataBind targets
  -> standalone CHTTP only when an HTTP adapter is enabled
```

CFlow must remain independent of XML and SCXML.

### XML document vs typed-data boundary

Language documents and external typed data intentionally use separate paths:

```text
SCXML / CCXML / VoiceXML bytes
  -> Salts XmlParser
  -> TurboSCXML W3C semantic compiler

<data src> resource bytes
  -> explicit DataBind format provider
  -> CSerde
  -> precompiled DataBindNativePlan
  -> CMeta-native session state
```

An XML-formatted external data resource uses the DataBind XML provider; it is
not reparsed as an SCXML document. TurboSCXML does not maintain a second XML
parser/DOM or a second native data-binding model.

See [SCXML XML parsing and DataBind boundary](docs/specs/scxml-xml-databind-boundary.md).

### Provider reflection and Plugin composition

Stateful Event I/O, Invoke and resource providers keep the existing bounded
adapter ABI used by sessions, while canonical provider identity is expressed as
CMeta Interface `{self,vtable}` contracts. Existing static adapters can be
projected into those interfaces, and Interface implementations can be bridged
back into the same session-facing adapters.

The optional `TurboSCXML::Plugin` component admits matching Salts Plugin
Interface exports while retaining a DSO lease for the complete provider-wrapper
lifetime. It does not add a second provider registry or execution model.

See [CMeta provider interfaces](docs/specs/scxml-cmeta-provider-interfaces.md).

The non-media CCXML/VoiceXML lifecycle bridge is documented in [VoiceXML dialog manager](docs/specs/voicexml-dialog-manager.md).

Multi-document URI resolution and the bounded immutable cache are documented in [VoiceXML document store](docs/specs/voicexml-document-store.md).

The optional CHTTP document projection is documented in [VoiceXML CHTTP document resource](docs/specs/voicexml-chttp-resource.md).

## Repository ownership

TurboSCXML owns:

- SCXML language semantic compilation over Salts XmlParser syntax views;
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
| `TurboSCXML::SCXML` | SCXML compiler/session/runtime facade over CFlow Statechart, including CMeta provider bridges |
| `TurboSCXML::Plugin` | optional lease-safe Salts Plugin Function/Interface composition bridge |
| `TurboSCXML::CCXML` | CCXML layer |
| `TurboSCXML::VoiceXML` | bounded VoiceXML core profile |
| `TurboSCXML::VoiceXMLDialogManager` | bounded CCXML dialogprepare/start/terminate bridge over the VoiceXML core |
| `TurboSCXML::VoiceXMLDocumentStore` | bounded URI resolution + immutable VoiceXML document/program cache |
| `TurboSCXML::VoiceXMLCHttpResource` | optional CHTTP-backed VoiceXML document provider bridge |
| `TurboSCXML::VoiceXMLCMeta` | optional CMeta-backed VoiceXML data-model profile |
| `TurboSCXML::CHttpResource` | optional host-authorized HTTP resource adapter |
| `TurboSCXML::CHttpEventIO` | optional BasicHTTP Event I/O processor |

The public C API is available through:

```c
#include <scxml/scxml.h>
#include <scxml/provider.h> /* optional CMeta provider-interface surface */
#include <voicexml/dialog_manager.h> /* optional CCXML/VoiceXML dialog lifecycle bridge */
#include <voicexml/document_store.h> /* bounded multi-document URI/cache owner */
#include <voicexml/chttp_resource.h> /* optional CHTTP document provider */
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
- installed `Salts.Native` SDK
- installed `SaltsUtils.Native` SDK
- standalone CHTTP only when an optional CHTTP adapter is enabled
- Visual Studio 2022 developer environment on Windows

Set:

```text
PROJECT_ROOT
VCPKG_ROOT
SALTS_ROOT
SALTS_UTILS_ROOT
CHTTP_ROOT   # only for CHTTP-enabled profiles
```

`CHTTP_ROOT` names the installed standalone `CHttp.Native` SDK when an HTTP adapter is enabled.

### Current SDK migration

Resolve the latest published native SDKs, including prereleases (`dotnet add
... package <name> --prerelease`). The dependency migration was tested locally
with [Salts 2.3.0-rc.10](https://github.com/qigao/salts/releases/tag/v2.3.0-rc.10),
[SaltsUtils 4.3.0-rc.8](https://github.com/qigao/salts-utils/releases/tag/v4.3.0-rc.8)
and [CHttp 2.1.0-rc.3](https://github.com/qigao/chttp/releases/tag/v2.1.0-rc.3).
CI pins this graph and its source manifests for reproducible qualification;
update them together when adopting the next published graph.

The current SDK exports `Salts::DataBind` as the public binding target and uses
`cmeta_*` thread, clock, UUID, logging and Plugin names. The Plugin bridge now
exposes `cmeta_plugin_*` types and its test DSO exports `cmeta_plugin_query`
using the SDK's ABI version. Rebuild hosts, plugins and consumers together;
older Plugin binaries are not interchangeable with this SDK generation.
Lease ownership and reverse-order teardown remain unchanged.

The CHTTP feature also restores the SDK's GmSSL dependency through the shared
qigao vcpkg registry. Core and QuickJS-only profiles do not request it. See
[CHTTP lifecycle migration](docs/scxml-chttp-event-io.md#生命周期) for the owner
thread and startup error boundary.

The migration follows the published SDK contracts instead of maintaining
legacy aliases or duplicate DataBind targets. To roll back, restore the prior
source revision and its matching SDK set together, then rebuild all profiles
and DSOs. Mixing old headers, libraries or cached package exports is unsupported.

### Platform qualification

The `Native SDK CI` workflow consumes the published native SDK graph pinned in
`cmake/ci/native-sdk-dependencies.csproj`, including prereleases.

| Platform | Qualification |
| --- | --- |
| Windows x64 | Release build, full CTest, install, and installed C/C++ consumers |
| Linux x64 | Release build, full CTest, install, and installed C/C++ consumers |
| Linux x64 sanitizers | Debug ASan + UBSan full CTest; LSan remains enabled except the W3C case isolated for upstream Salts #630 |
| macOS (runner architecture) | Release build, full CTest, install, and installed C/C++ consumers |
| Android arm64-v8a, API 26+ | Release cross-build, install, and installed C/C++ consumer build; target execution is not performed on the Linux host runner |

Other Android ABI presets remain available for local/experimental builds but are not CI-qualified.

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

`TURBOSCXML_ENABLE_QUICKJS` enables the bounded QuickJS-NG profiles, using the
existing vcpkg `quickjs-ng` dependency:

| Profile | Public entry |
| --- | --- |
| SCXML datamodel | `TurboSCXML::SCXML`, `<scxml/scxml.h>` |
| CCXML conditions and dynamic strings | `TurboSCXML::CCXMLQuickJS`, `<ccxml/quickjs.h>` |
| VoiceXML datamodel | `TurboSCXML::VoiceXMLQuickJS`, `<voicexml/quickjs.h>` |

CCXML imports the current CMeta state and `event$` into a fresh context for each
evaluation. Conditions use JavaScript truth conversion; dynamic destinations
and dialog sources require nonempty strings. CMeta retains transactional ID
writeback ownership; JavaScript mutations are discarded. See the
[expression adapter contract and example](docs/specs/ccxml-dynamic-string-expression-design.md#quickjs-ng-adapter)
for bounds, lifetime rules and unsupported syntax.

The existing C11 and C++17 CCXML installed consumers execute the same
[QuickJS session cases](tests/install_consumer/ccxml_quickjs_cases.h) when
`TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS` is enabled. They cover current
state/Event imports, conditions, dynamic send fields and cancel IDs, rollback,
retry, and adapter shutdown. On Windows, after installing with
`install-win-release-quickjs-user`, set `TURBOSCXML_ROOT` to that SDK and run
configure/build/CTest with `win-release-quickjs-installed-user` from
`tests/install_consumer`.

VoiceXML's installed C11/C++17 consumers run with
`win-release-voicexml-quickjs-installed-user` in the same directory against
the same SDK. They execute static and computed script URIs through a local
resource provider, verify committed-state isolation and exception rollback,
and check exact lease settlement. CTest registers these consumers separately
from the SCXML/CCXML profile.

VoiceXML typed QuickJS state accepts `cmeta_reflect_value` roots through
CMeta's canonical fieldwise copy/move/cleanup operations. Reflection storage
descriptors deliberately do not supply native storage traits, so the shared
snapshot bridge selects semantic lifecycle for reflection-format descriptors;
legacy descriptors keep their existing native owner callbacks. This avoids
duplicating field lifecycle or treating a read-only `cmeta_reflect_data` view
as an owned value. Nested views remain ineligible for snapshots. No stored
data or public ABI migration is required; consumers needing an older runtime
can retain their existing legacy descriptor and native lifecycle callbacks.
VoiceXML compilation rejects unsupported snapshot lifecycle with
`VXML_INVALID_CONTRACT` and root storage exceeding `max_snapshot_bytes` with
`VXML_LIMIT_EXCEEDED`, before publishing a Program. Allocation and state-copy
failures remain runtime errors.
SCXML additionally requires native storage traits for CFlow Statechart state
ownership. A generated reflection descriptor alone is insufficient there:
SCXML QuickJS compilation returns `SCXML_INVALID_ARGUMENT` before acquiring
script resources if the root lacks native copy/move/destroy support (or its
trivial equivalent). Compilation also checks the shared snapshot lifecycle:
native storage traits do not make a reflection VIEW, or a VALUE containing
a nested VIEW, eligible for an owning transaction snapshot.

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

Current profile support and executable witnesses are tracked in [VoiceXML support and conformance evidence matrix](docs/specs/voicexml-support-matrix.md).

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

## Component runtime candidate qualification

The `Component runtime conformance` workflow qualifies `main` and architecture
integration changes against the exact published Salts/SaltsUtils graph in
`cmake/ci/native-sdk-dependencies.csproj`. It verifies SDK source manifests on
Linux, Windows and macOS. `Component upstream source TSan` rebuilds the same
first-party sources with ThreadSanitizer on Linux.

The DSO test executes real SCXML send effects in old/new sessions, checks generation capacity and closed admission, and verifies each provider closes once before scope retirement and module unload. A session borrows its adapter bridge; destroy the session before its provider bridge and scope. Installed consumers explicitly request `Component`, which requires `Salts::ComponentPlugin`; Plugin handles use the canonical `cmeta_plugin_*` SDK types.

With the workflow's installed dependency roots and shared vcpkg/re2c environment,
run the complete configured build and CTest suites, then verify the installed
consumer independently:

```sh
cmake --preset ci-component-release-user
cmake --build --preset ci-component-release-user -j2
ctest --preset ci-component-release-user --no-tests=error --output-on-failure
cmake --build --preset install-ci-component-release-user -j2
cd tests/install_consumer
cmake --preset ci-component-installed-user
cmake --build --preset ci-component-installed-user -j2
ctest --preset ci-component-installed-user --no-tests=error --output-on-failure
```

CI selects Component and adjacent business suites from the full configured graph
and uploads consumer/dependency identities, JUnit results, and CTest logs as
`component-acceptance-linux-x64`. The root CTest command above runs the broader
suite. A green Linux Release run establishes only this profile's acceptance;
sanitizer qualification and Windows/macOS downstream runs remain separate release
gates. This workflow neither merges nor publishes a stable release.
