# CCXML Normal Dialog Termination Design

## Status and scope

This slice adds one bounded normal-termination path:

```xml
<dialogterminate dialogid="dialog.id"/>
```

`dialogid` may instead be a nonempty quoted literal. A dotted NCName location
lets the element consume the identifier written by the supported direct
`dialogstart` profile. The standard `immediate` default is retained: this
profile always requests normal, non-immediate termination.

Standard reference:
[W3C CCXML 1.0, `dialogterminate`](https://www.w3.org/TR/ccxml/#dialogterminate).

## Compiler contract

`dialogid` is required and accepts either a nonempty quoted string literal
without escape processing or a dotted NCName readable string location. The
element must contain no non-whitespace children. `immediate`, `hints`, all
other attributes, arbitrary ECMAScript expressions, and escaped literals are
unsupported.

The compiler copies either the decoded literal or location into bounded
program storage, charges its trailing NUL, and reserves one effect slot.

## Provider contract

The v1 telephony table gains append-only `prepare_dialog_terminate`. Its
borrowed request contains the resolved dialog ID and `immediate == false`.
The callback must copy bytes it retains and, on acceptance, transfer one
move-only effect ticket. Prepare reserves the operation without changing
provider-visible dialog state; commit publishes a nonblocking, infallible
normal termination request.

The provider owns dialog lookup and authoritative state, normal shutdown,
return-value collection, bridge teardown, `conference.unjoined` delivery, and
the single eventual `dialog.exit` event. Terminating a dialog does not
terminate the CCXML session.

## Datamodel and transaction contract

Literal IDs require no datamodel. A location requires the existing readable
string adapter tail. Session initialization validates every compiled location;
dispatch reads it before provider prepare. Empty, NUL-containing, or otherwise
malformed borrowed values are provider contract errors and no termination is
prepared.

Provider refusal or a malformed ticket discards all earlier effects in the
transition in reverse order. Successful termination participates in ordinary
document-order commit.

## Deferred standard surface

Explicit `immediate` expressions, including `false`, and `hints` remain
unsupported. Immediate termination needs a separate profile because it
changes return-value and event timing semantics.

## Verification

Tests cover literal/location compilation, strict rejection, byte limits,
source-independent copies, datamodel capability and value validation,
provider request bytes and normal mode, rollback, malformed tickets,
append-only ABI compatibility, and a two-dispatch CMeta start/terminate
lifecycle.
