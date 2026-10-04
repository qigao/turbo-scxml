# Supported VoiceXML build profiles

TurboSCXML qualifies a small set of public VoiceXML build profiles rather than
the Cartesian product of every CMake option.

The Linux Release Native SDK job is the canonical profile-matrix gate.

| Profile | VoiceXML CMeta | QuickJS | CHTTP adapters | Plugin | vcpkg manifest feature |
| --- | --- | --- | --- | --- | --- |
| core | OFF | OFF | OFF | OFF | none |
| cmeta | ON | OFF | OFF | OFF | none |
| quickjs | OFF | ON | OFF | OFF | quickjs |
| chttp | OFF | OFF | ON | OFF | none |
| full | ON | ON | ON | ON | quickjs |
| plugin-disabled package | ON | OFF | OFF | OFF | none/available root |

## Why these profiles

### core

Proves the base VoiceXML/DocumentStore/SubmitResource/DialogManager package can
be built and consumed without VoiceXMLCMeta, QuickJS, CHTTP adapters or Plugin.

Its install consumer requests only the `VoiceXML` component and disables qjs
package discovery.

### cmeta

Proves `VoiceXMLCMeta` owns its typed/DataBind dependencies without requiring
QuickJS or CHTTP.

Its install consumer requests only the VoiceXML CMeta component and disables
qjs package discovery.

### quickjs

Proves `VoiceXMLQuickJS` can be built without the public VoiceXMLCMeta target
or CHTTP adapters. It intentionally restores the manifest's `quickjs` feature
and exercises the installed VoiceXMLQuickJS consumer.

### chttp

Proves the optional CHTTP adapters consume the standalone installed CHTTP SDK
without requiring QuickJS or the VoiceXMLCMeta product. This profile uses the
same no-QuickJS vcpkg root as core/CMeta.

### full

The existing five-platform release configuration. It enables CMeta, QuickJS,
CHTTP adapters and Plugin and runs the complete local test suite plus installed
consumers.

### plugin-disabled package

The existing Linux package-isolation gate. It proves Plugin remains optional
and package discovery stays fail-fast without a compatibility fallback.

## Dependency proof

The Linux job creates two vcpkg roots:

- `vcpkg_installed-noquick`: manifest install with **no feature**, used by
  core/CMeta/CHTTP;
- `vcpkg_installed`: manifest install with `--x-feature=quickjs`, used by
  QuickJS/full.

Because the root manifest has no unconditional transport dependencies, the
no-QuickJS root proves these profiles do not need quickjs-ng or duplicated
CHTTP transport packages.

## CI cost policy

Only Linux Release repeats the reduced profile builds. Cross-platform and
sanitizer jobs keep the full profile:

- Windows full;
- Linux full;
- Linux ASan+UBSan full;
- macOS full;
- Android full.

This qualifies the public option boundaries without multiplying expensive
cross-platform/sanitizer work.
