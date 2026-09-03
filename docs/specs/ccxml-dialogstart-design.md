# CCXML Direct Dialog Start Design

## Status and scope

This slice adds one bounded direct-start path for a VoiceXML-capable dialog
provider:

```xml
<dialogstart dialogid="dialog.id"
             src="'https://voice.example/menu.vxml'"
             connectionid="event$.connectionid"/>
```

The W3C element can also start prepared dialogs and accept conference,
parameter, media-direction, MIME, cache, HTTP, and hint options. This
incubation slice deliberately admits only a direct source URI joined to the
current event connection. `dialogid` is optional in CCXML 1.0 but required by
this narrower profile so the immediately created Dialog Object remains
addressable before its asynchronous result event.

Standard reference:
[W3C CCXML 1.0, `dialogstart`](https://www.w3.org/TR/ccxml/#dialogstart).

## Compiler contract

`dialogid` is a required dotted NCName write location. `src` is a required,
nonempty quoted string literal with no escape processing. `connectionid` is
required and must be exactly `event$.connectionid`; the source text is not
retained because the runtime reads the current event field directly.

All other attributes and non-whitespace child content are unsupported. The
compiler copies the location and decoded source URI into bounded program
storage, charges one trailing NUL per retained value, and reserves two effect
slots: provider start and datamodel writeback.

## Provider contract

The v1 telephony table gains append-only `prepare_dialog_start`. Its borrowed
request contains the source URI, the default media type
`application/voicexml+xml`, and the current event connection ID. The callback
must copy bytes it retains.

On acceptance the provider returns a nonempty borrowed dialog ID and one
move-only effect ticket. The ID remains valid until the ticket is committed or
discarded. Prepare creates/reserves the Dialog Object without publishing the
start operation; commit starts it asynchronously and is nonblocking and
infallible.

The provider owns URI policy and retrieval, dialog-manager selection, VoiceXML
execution, connection bridging, resource limits, and asynchronous
`dialog.started`, `error.dialog.notstarted`, and eventual `dialog.exit`
delivery.

## Datamodel and transaction contract

Session initialization requires the existing write-capable datamodel prefix
and validates `dialogid` once. Dispatch rejects a missing, empty, or
NUL-containing current event connection ID before provider prepare.

The core prepares the provider reservation first and then stages the returned
dialog ID through `prepare_assign_string`. It swaps the two tickets so the ID
write commits before provider publication. Any refusal, malformed ID, or
malformed ticket discards prepared effects in reverse order. Neither live
datamodel state nor provider-visible dialog start changes before commit.

## Deferred standard surface

`prepareddialogid`, `conferenceid`, `type`, `namelist`, `parameters`,
`mediadirection`, `maxage`, `maxstale`, `enctype`, `method`, `hints`, arbitrary
ECMAScript expressions, and escaped literals remain unsupported. Dialog
prepare and terminate are separate follow-on slices.

## Verification

Tests cover strict syntax and retained-byte bounds, source-independent copies,
current-event ID forwarding, provider ID writeback and commit order, rollback,
invalid events/IDs/tickets, session-time location validation, append-only
callback compatibility, CMeta end-to-end writeback, and the supported
build/install matrix.
