# SCXML Completion Param Failure Design

## Status

Accepted for W3C test343 implementation. This decision changes interpreter
behavior only for mixed successful and failed CMeta `<donedata><param>`
materialization. It does not change the public C API, ABI, CMake targets, or
dependency versions.

## Background

SCXML 1.0 section 5.7 requires an invalid `param/@location` or a failing
`param/@expr` to enqueue `error.execution` and ignore that param's name and
value. TurboSCXML currently copies one immutable CMeta state snapshot, applies
every completion assignment into that copy, and publishes a typed subset
schema only when all assignments succeed. One failed assignment destroys the
copy and publishes empty completion data. That behavior is safe and bounded,
but it also discards successful sibling params.

W3C test343 observes the failure Event before the completion Event and requires
the failed pair to be absent. The local regression strengthens that witness
with one successful sibling so that whole-object discard cannot satisfy it.

## Decision

Keep the program-owned `scxml_done_data_descriptor` as the immutable fact
source for candidate fields and assignments. At session initialization,
preallocate projection storage for every completion slot. During completion
materialization:

1. Copy the source CMeta object once into the reserved slot.
2. Evaluate every param in document order against the same immutable source.
3. Copy the descriptor field into the slot's projection only when its
   assignment succeeds.
4. For each failed param, enqueue one `error.execution`, omit its field, and
   continue with later params.
5. Publish the program-owned full schema when every param succeeds, a
   slot-owned projected schema when only a subset succeeds, or empty text data
   when none succeeds.

Projected schemas use the original descriptor stable ID plus a deterministic
`#runtime-subset:` bit string in document order. This keeps distinct runtime
shapes semantically distinct while keeping all identifier bytes bounded and
session-owned.

## Ownership and Data-Path Protocol

| Concern | Contract |
| --- | --- |
| Data unit | One `scxml_completion_data_slot`: owned CMeta object, projected field descriptors, projected stable-ID bytes, and slot state. |
| Fact source | Program-owned immutable `scxml_done_data_descriptor` assignments and candidate fields. Runtime projection is derived data only. |
| Owner | The owning `scxml_session_impl` allocates and frees all slot, field, and stable-ID storage. CFlow borrows the current Event view only while the slot is `BOUND`. |
| Lifetime | Projection pointers remain stable from session initialization through session destruction. Object/schema views are valid from `READY` through `BOUND` and are invalidated by completion-slot release. |
| Topology | Single owning session/executor mutates completion slots. No cross-thread producer is added. |
| Order | Param evaluation and projected fields preserve XML document order. Each failure Event is enqueued in that same order before the later completion Event. |
| Capacity | Per-slot field capacity is the largest descriptor assignment count. Per-slot stable-ID capacity is the largest checked sum of base ID bytes, `#runtime-subset:`, one marker byte per assignment, and the terminator. Total storage is checked multiplication by `completion_capacity + 1`. |
| Full behavior | Overflow or allocation failure rejects session initialization with the existing limit/allocation status. Runtime capacity mismatch is an invariant violation and fails the session. Projection metadata introduces no heap allocation or fallback during materialization; existing CMeta object-copy and field-adapter allocation semantics are unchanged. |
| Failure | A param evaluation failure leaves its destination unchanged, excludes its field, and queues `error.execution`. Failure to copy the source object, queue the error, or validate the projected schema releases the slot and is fatal. |
| Release | The slot destroys its owned object exactly once, clears Event payload state, and preserves its preallocated projection pointers for reuse. Session destruction drains every live slot before freeing backing arrays. |
| Observation | Existing session status, `error.execution`, completion Event data, and test ownership counters remain the observable boundary. No new logging is added. |

## Alternatives

### Keep atomic all-or-nothing publication

Rejected. It preserves existing ownership but violates the per-param ignore
rule when a valid sibling accompanies a failed param.

### Allocate a schema for each completion Event

Rejected. It is simple but adds unbounded hot-path allocation and makes
allocation failure part of normal completion processing.

### Add a visibility bitmap to the expression evaluator

Rejected. It would spread SCXML completion policy into the CMeta expression
system and alter the private system-values contract for every structured Event
source. A projected CMeta schema already expresses the required boundary.

## Compatibility and Migration

Successful-only completion payloads retain their existing schema and values.
All-failed payloads remain empty and continue to raise errors before
completion, preserving tests 298 and 488. Mixed payloads intentionally change
from empty to the successful typed subset. Session memory increases by a
checked, document-derived amount per configured completion slot; no deployed
configuration or serialized data requires migration.

## Rollback

Reverting the projection storage and restoring fail-whole-object behavior is a
source-only rollback. Existing program files and session configurations remain
loadable because no public format or ABI changes.

## Verification

- A focused CMeta regression must fail under whole-object discard, then pass
  only when a successful sibling remains readable and the failed field is not
  admitted by the completion schema.
- A multi-failure regression must consume one `error.execution` per failed
  param, retain successful fields after failures, and preserve document order.
- Sequential mixed completions must reuse one released projection slot across
  different subset shapes without retaining stale fields or losing storage.
- A projection-specific capacity test must reach checked multiplication after
  `completion_capacity + 1` succeeds and report the existing limit status.
- W3C-derived tests 294, 298, 343, and 488 must pass together.
- Managed copy/destroy counters must balance after session destruction.
- Strict inventory, Debug CTest, and Release CTest must remain green.

## References

- [SCXML 1.0 section 5.7 `<param>`](https://www.w3.org/TR/scxml/#param)
- [W3C Implementation Report test343](https://www.w3.org/Voice/2013/scxml-irp/343/test343.txml)
