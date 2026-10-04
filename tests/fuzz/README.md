# VoiceXML bounded fuzz smoke

These targets are deterministic hardening gates, not a claim of exhaustive
fuzzing.

## Targets

- `voicexml_base_fuzz_smoke`: arbitrary mutations exercise base VoiceXML
  compile plus Session init/destroy publication invariants; runtime `start`
  is exercised only on fixed known-safe seeds because the literal profile has
  no execution-step quota.
- `voicexml_cmeta_fuzz_smoke`: CMeta compile plus one bounded CMeta Session
  initialization/start for successfully compiled mutations.

Each checked-in seed is locally authored and is not copied from an upstream
conformance suite.

## CI bounds

- maximum input: 4096 bytes;
- iterations: 16 mutations per seed;
- deterministic xorshift mutation schedule;
- no network, CHTTP, provider, filesystem writes, or background workers;
- CMeta runtime is bounded by 128 execution steps;
- smoke runs only in the existing ASan+UBSan job.

The harness asserts compiler publication invariants: success must publish a
Program and failure must leave the Program empty. Session initialization has
the same success/handle invariant. Arbitrary literal mutations are never
allowed to enter unbounded control-flow execution; fixed runtime seeds cover
literal start, while CMeta mutation runtime remains bounded by its explicit
128-step quota.

## Regression workflow

When sanitizer fuzz smoke finds a crash, timeout, or invariant violation:

1. reproduce with the seed name and iteration printed by the harness;
2. minimize the mutated bytes manually or with a local reducer;
3. add the minimized case as a normal focused unit test whenever the failure is
   semantic/structural;
4. add a small corpus seed only when it represents a distinct parser/compiler
   mutation family;
5. keep the original hard bounds unchanged unless the support contract itself
   changes.

Do not turn CI into an unbounded fuzz service.

## Cross-repository regressions

If a minimized fuzz failure belongs to a consumed package rather than
TurboSCXML itself:

1. fix and regress it in the owning repository;
2. qualify the owner fix under its sanitizer gate;
3. publish the normal latest package release;
4. re-run TurboSCXML against that released package.

Do not add a sanitizer suppression, consumer-side ownership workaround,
fallback implementation, or dependency version pin.

The first example is the malformed XML lexer error-token leak found by this
smoke gate in Native SDK CI run `37181219319`. The ownership bug belonged to
SaltsUtils XML parser/cxml and was fixed/regressed there before TurboSCXML
qualification resumed.


A later PR #285 sanitizer run exposed another malformed-XML ownership
regression while exercising the same public parser contract. Because
`salts_xml_parse()` guarantees that failure leaves an empty output, the fix
remained in the SaltsUtils XML parser owner rather than adding a consumer-side
destroy workaround. SaltsUtils #477/#480 fixed and qualified the exact
VoiceXML mutations, then release 4.1.19 published that fix. The local
`partial-tree-unclosed.vxml` seeds keep the cross-repository regression
visible while TurboSCXML continues to consume the latest released SDK.
