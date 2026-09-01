# TurboSCXML Single Adapter ABI Design

## Context

TurboSCXML has no released downstream consumers. Its Event I/O and invocation
boundaries nevertheless retain three cumulative public adapter tables, three
session initialization families, two payload representations, and runtime ABI
dispatch. The compatibility machinery obscures the current content-aware
contract and makes every new SCXML capability cross historical branches.

## Decision

TurboSCXML exposes one exact current adapter contract:

- `scxml_event_io_adapter` and `scxml_invoke_adapter` are the only operation
  tables;
- `scxml_session_config` directly carries both adapter pointers and their
  borrowed user pointers;
- `scxml_session_init()` and `scxml_session_init_cmeta()` are the only session
  initialization functions;
- `scxml_session_try_send()` admits a bare Event, while
  `scxml_session_try_send_with_metadata()` admits the current owned metadata
  envelope;
- send, invoke, payload, and metadata types have no historical suffix; and
- adapter version and structure size must exactly match the current contract.

The current content-aware V3 representation becomes the sole representation.
There are no typedef aliases, legacy entry points, partial-prefix acceptance,
or runtime fallback branches.

## Architecture and ownership

The adapter remains a thin C strategy/bridge boundary. A session copies the
operation table during initialization and borrows the user pointer until
successful destruction. Prepare callbacks borrow their request views only for
the callback, and an accepted prepare transfers exactly one move-only
prepare/commit/discard ticket to the session. Session state and adapter state
remain separate facts: Statechart owns configuration and queues, the SCXML
session owns invocation/send registries, and the host owns transport resources.

Initialization validates capabilities, callback presence, exact ABI, and exact
table size before ownership attaches. Failure closes every attached adapter
exactly once. Runtime code calls the single copied table directly.

## Public behavior and compatibility

This is an intentional source and ABI break. No data migration or compatibility
shim exists because there are no downstream consumers. Existing SCXML document
semantics, capacity limits, error mapping, ticket ordering, shutdown, and
quiescence behavior remain unchanged.

The removal does not add SCXML syntax or conformance. A dynamic send target can
resolve externally, so initialization now requires Event I/O and the matching
payload/content capability up front; this replaces the old runtime-generation
fallback with deterministic fail-fast admission. Otherwise the change only
establishes one host boundary without historical dispatch.

## Alternatives

Keeping V3 names and deleting only V1/V2 was rejected because it preserves a
false migration narrative in the only supported API. Retaining aliases was
rejected because aliases would still become accidental downstream contracts.
Removing ABI and size fields entirely was rejected because host operation
tables cross a public C boundary and must fail fast on a mismatched shape.

## Verification

- A RED TinyTest consumer uses the suffix-free API and exact-shape rejection.
- Focused Event I/O, core session, CMeta, and W3C tests exercise the migrated
  real adapters.
- Release and Debug CTest suites must pass; sanitizer validation is required
  when a repository sanitizer preset exists.
- Public headers, production sources, tests, and current documentation must
  contain no legacy adapter types, constants, init entry points, or runtime ABI
  dispatch.
