# VoiceXML bounded fuzz smoke

These targets are deterministic hardening gates, not a claim of exhaustive
fuzzing.

## Targets

- `voicexml_base_fuzz_smoke`: base VoiceXML compile plus one bounded Session
  initialization/start for successfully compiled mutations.
- `voicexml_cmeta_fuzz_smoke`: CMeta compile plus one bounded CMeta Session
  initialization/start for successfully compiled mutations.

Each checked-in seed is locally authored and is not copied from an upstream
conformance suite.

## CI bounds

- maximum input: 4096 bytes;
- iterations: 32 mutations per seed;
- deterministic xorshift mutation schedule;
- no network, CHTTP, provider, filesystem writes, or background workers;
- CMeta runtime is bounded by 128 execution steps;
- smoke runs only in the existing ASan+UBSan job.

The harness asserts compiler publication invariants: success must publish a
Program and failure must leave the Program empty. Session initialization has
the same success/handle invariant.

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
