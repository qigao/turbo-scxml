# CCXML Create Conference and Datamodel Writeback Design

## Status and scope

This slice extends the bounded CCXML incubation profile with conference
creation and an explicit left-value writeback boundary:

```xml
<createconference conferenceid="conference_id" confname="'support'"/>
```

`conferenceid` is a required dotted NCName location, not an identifier value
and not a telephony-provider argument. The original slice admits an optional
nonempty quoted `confname` literal. The additive dynamic-name slice also admits
an unquoted typed string expression without changing the provider or datamodel
ABI; see `ccxml-dynamic-conference-name-design.md`. `reservedtalkers`,
`reservedlisteners`, `hints`, escaped literals, and arbitrary ECMAScript
expressions remain unsupported.

## Compiler contract

The compiler copies the location and optional decoded conference name or
expression source into bounded program storage. Each copied range includes one trailing NUL in
`max_name_bytes` accounting. A create-conference action consumes two effect
slots at runtime: one provider reservation and one datamodel writeback.

A dynamic name is compiled once per session and evaluated immediately before
provider prepare. Its borrowed result must be nonempty and contain no embedded
NUL. During foreach, the built-in CMeta adapter resolves the current staged
iteration scope. Quoted literal and omitted names retain the original adapter
prefix compatibility.

The retained `conferenceid` syntax is one or more NCName segments separated by
dots. Empty segments, leading/trailing dots, quotes, brackets, and invalid
UTF-8 are rejected during admission. Schema resolution remains a datamodel
adapter responsibility and is performed once during session initialization.

## Provider contract

The v1 telephony table gains an append-only `prepare_create_conference`
operation. The request carries only the optional conference name. On accepted
prepare the provider returns both a move-only effect ticket and a borrowed,
nonempty conference identifier. The identifier remains valid until that
ticket is committed or discarded; the core passes it to the datamodel prepare
boundary for copying before resolving the provider ticket.

The provider owns the global conference registry, same-name lookup/attachment,
resource limits, implicit detach on session termination, and asynchronous
`conference.created` or `error.conference.create` delivery. Commit publishes
the already-reserved operation and is nonblocking and infallible.

## Datamodel contract

`ccxml_datamodel_adapter_v1` has two synchronous operations:

- `validate_string_location` resolves a writable owned-string location during
  session initialization.
- `prepare_assign_string` copies a value into private staging and returns one
  move-only ticket without mutating live state.

The core ships a CMeta-backed implementation. It borrows one validated root
schema and mutable root object, resolves dotted paths through
`cmeta_data_struct_shape`, requires `CMETA_DATA_STRING` with owned buffer
operations and move/destroy traits, builds replacement storage during prepare,
and replaces the live value without allocation during commit.

The generic boundary deliberately does not expose CMeta types to a telephony
provider and leaves room for another expression/datamodel implementation.

## Transaction and lifetime

For each create-conference action, the core prepares the telephony ticket
first and the datamodel ticket second. It commits the staged ID writeback before
publishing the provider operation, so an immediately delivered result event
cannot observe the old value. Any refusal or malformed result discards the
provider reservation before the staged write and then earlier actions in
reverse order. Action ordering relative to surrounding executable content is
preserved.

Program strings live until program destruction. Session adapter tables are
copied; their user pointers remain borrowed through session destruction. The
CMeta adapter borrows its schema and mutable state until its own destruction
and must not be destroyed concurrently with a session call.

## Verification

Tests cover strict syntax and storage bounds, source-independent copies,
session-time location validation, exact provider requests and returned-ID
writeback, commit ordering, reverse rollback, malformed provider/datamodel
tickets, callback-tail ABI checks, CMeta nested owned-string assignment, and
the supported build/install matrix.
