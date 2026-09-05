# CCXML Typed CMeta Foreach Plan

**Goal:** Add a bounded CCXML `<foreach>` profile that reuses the SCXML CMeta
sequence and supplemental-scope runtime while preserving transition-wide
effect atomicity.

## Runtime protocol

- Data unit: one CMeta sequence element copied into an owned snapshot, then
  copied into a typed supplemental-scope slot.
- Ownership: the application owns the root sequence; the CCXML session owns
  compiled foreach programs, snapshots, scratch values, and committed/staged/
  checkpoint scope views.
- Lifetime: a snapshot lives from `FOREACH` through its matching
  `ENDFOREACH`; a staged item lives until the aggregate transition ticket is
  committed or discarded.
- Topology: one serialized CCXML dispatch is producer and consumer; nested
  loops are rejected in this slice.
- Capacity: `max_foreach_iterations` bounds sequence length and expanded
  effect tickets; `max_foreach_storage_bytes` bounds the three scope views
  plus the largest admitted snapshot and managed-item scratch value.
- Failure: admission fails before attachment; runtime failure destroys the
  live value and snapshot, reverse-discards tickets, restores the block
  checkpoint, and clears the staged transaction.
- Shutdown: session destruction occurs only after the CFlow instance and
  adapters are quiescent, then clears scope values before freeing storage.

## Tasks

- [x] Lower `foreach` to bounded `FOREACH`/body/`ENDFOREACH` rows.
- [x] Reject extra attributes and nested loops.
- [x] Multiply body effect admission by the configured iteration bound using
      checked arithmetic.
- [x] Require the built-in CMeta adapter for foreach programs without changing
      the public datamodel-adapter ABI.
- [x] Compile CMeta sequences against typed supplemental slots during session
      admission.
- [x] Allocate bounded committed, staged, and checkpoint scope views.
- [x] Snapshot sequences and execute each body iteration before aggregate
      effect commit.
- [x] Roll back scope and provider tickets on iteration or prepare failure.
- [x] Compile loop-body conditions against the supplemental schema and evaluate
      them against the current staged typed item.
- [x] Bind optional `index` as a staged supplemental `size_t` and include it in
      the same checkpoint/commit/rollback protocol.
- [x] Reject item/index names that would resolve to application-root locations.
- [x] Reject managed-copy elements whose lifecycle allocations cannot be
      charged to the hard storage budget, and reject opaque elements without a
      usable semantic data descriptor.
- [x] Test flattened control rows, configured capacity, typed commit, and
      failure rollback.
- [x] Run the complete Release verification matrix and inspect the final diff.

## Current restrictions

Supported CCXML leaf actions and bounded conditional blocks are admitted in the
body. Elements require trivial-copy/trivial-destroy traits plus a built-in,
root-reachable, or explicitly registered semantic data descriptor. The optional
size-versioned CMeta registry supports structured `item.member` conditions
without an artificial root field. Managed/opaque elements, nested foreach,
XPath, and general ECMAScript are outside this profile.
