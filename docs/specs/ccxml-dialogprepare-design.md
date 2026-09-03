# CCXML Detached Dialog Preparation Design

## Status and scope

This slice adds one bounded, target-free preparation path for a
VoiceXML-capable dialog provider:

```xml
<dialogprepare dialogid="dialog.prepared"
               src="'https://voice.example/menu.vxml'"/>
```

The W3C element prepares a dialog handler asynchronously and may associate it
with a connection or conference. This incubation profile deliberately omits a
media target. A later prepared `<dialogstart>` can therefore attach the
prepared dialog to the then-current connection without conflicting with a
target chosen during preparation. `dialogid` is optional in CCXML 1.0 but is
required here so the immediately created Dialog Object remains addressable.

Standard reference:
[W3C CCXML 1.0, `dialogprepare`](https://www.w3.org/TR/ccxml/#dialogprepare).

## Compiler contract

`dialogid` is a required dotted NCName write location. `src` is a required,
nonempty quoted string literal with no escape processing. All other attributes
and non-whitespace child content are unsupported, including `connectionid`,
`conferenceid`, `type`, parameter lists, media direction, cache/HTTP controls,
and hints.

The compiler copies the location and decoded source URI into bounded program
storage, charges one trailing NUL per retained value, and reserves two effect
slots: provider preparation and datamodel writeback.

## Provider contract

The v1 telephony table gains append-only `prepare_dialog_prepare`. Its borrowed
request contains the source URI and the default media type
`application/voicexml+xml`; it contains no connection or conference target.
The callback must copy bytes it retains.

On acceptance the provider returns a nonempty borrowed prepared-dialog ID and
one move-only effect ticket. The ID remains valid until the ticket is committed
or discarded. Prepare creates or reserves the Dialog Object without publishing
the asynchronous preparation operation; commit publishes it and is nonblocking
and infallible.

The provider owns URI policy and retrieval, dialog-manager selection, VoiceXML
setup, resource limits, and asynchronous `dialog.prepared`,
`error.dialog.notprepared`, or termination-related event delivery.

## Datamodel and transaction contract

Session initialization requires the existing write-capable datamodel prefix
and validates `dialogid` once. Dispatch prepares the provider reservation and
then stages its returned ID through `prepare_assign_string`. It swaps the two
tickets so the ID write commits before provider publication. Any refusal,
malformed ID, or malformed ticket discards prepared effects in reverse order.
Neither live datamodel state nor provider-visible preparation changes before
commit.

## Deferred standard surface

`connectionid`, `conferenceid`, `type`, `namelist`, `parameters`,
`mediadirection`, `maxage`, `maxstale`, `enctype`, `method`, `hints`, arbitrary
ECMAScript expressions, and escaped literals remain unsupported. Starting a
prepared dialog is a separate follow-on slice.

## Verification

Tests cover strict syntax and retained-byte bounds, source-independent copies,
provider request bytes, generated ID writeback and commit order, rollback,
malformed IDs/tickets, session-time location validation, append-only callback
compatibility, real CMeta lifecycle behavior, and the supported build/install
matrix.
