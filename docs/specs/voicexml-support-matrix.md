# VoiceXML support matrix

## Purpose

This document states the current **incubating VoiceXML support surface**. It is
not a full-conformance claim.

The machine-readable source is
`tests/voicexml/support-matrix.tsv`. CI validates that every SUPPORTED row has
an executable local witness and that every UNSUPPORTED/PARTIAL row has an
explicit negative witness.

The SCXML W3C manifest is not evidence for VoiceXML support.

## Standards references

- `VXML20` — W3C Voice Extensible Markup Language (VoiceXML) Version 2.0:
  https://www.w3.org/TR/voicexml20/
- `VXML21` — Voice Extensible Markup Language (VoiceXML) 2.1:
  https://www.w3.org/TR/voicexml21/

## Status meanings

| Status | Meaning |
| --- | --- |
| SUPPORTED | The named TurboSCXML profile has executable positive evidence. |
| UNSUPPORTED | The profile intentionally rejects this surface with executable negative evidence. |
| PARTIAL | Only an explicitly described subset is supported; the restriction has a negative witness or tracking issue. |
| N/A | The surface does not apply to that profile. |

Provider requirements are part of the support statement. A language feature
that requires prompt/collect/record/transfer/resource capability is not
equivalent to a bundled provider implementation.

## Provenance

Every current matrix row is `local`: its executable witness was authored in
this repository for the implemented TurboSCXML contract.

No VoiceXML upstream implementation-report case is currently imported into the
matrix. If an upstream-derived case is added later, its TSV provenance field
must use `upstream:<source-id>` and the repository must record:

- source URL/document;
- upstream test/assertion identifier;
- license/provenance note;
- local transformation description.

A row never becomes SUPPORTED merely because a similarly named SCXML test
passes.

## Current support summary

The matrix currently covers the delivered architecture:

- base form/block/control transfer;
- typed CMeta executable content and FIA;
- field/menu/initial and scoped Events;
- prompt/media, marks and bounded foreach;
- grammar, including 2.1 `srcexpr`;
- typed data and QuickJS no-DOM data;
- external QuickJS script;
- subdialog/record/transfer;
- recorded utterance metadata;
- urlencoded and explicit-recording multipart submit;
- resource/fetchaudio policy.

Intentional profile restrictions, such as QuickJS `data@name` DOM binding and
VoiceXML 2.1 `data` under version 2.0, are explicit negative rows.

## Adding or changing a row

1. Add or update the executable test first.
2. Add the TSV row with the exact test-case label.
3. Use `SUPPORTED` only when a positive witness exists.
4. Use `UNSUPPORTED` or `PARTIAL` only with a negative witness or tracking
   issue.
5. Mark provenance accurately.
6. Keep architectural ownership details in
   `docs/specs/voicexml-architecture-design.md`; this file is evidence, not a
   second architecture specification.

## Remaining hardening

Issue #51 remains open for:

- #276 bounded fuzz targets and minimized regressions;
- #277 security/sensitive-data policy;
- #279 supported feature-option CI matrix.

The support matrix is the baseline those slices use when deciding what must be
hardened and qualified.
