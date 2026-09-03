# CCXML Prepared Dialog Start Design

## Status and scope

This slice connects a previously detached VoiceXML dialog to the current
connection:

```xml
<dialogstart prepareddialogid="dialog.prepared"
             connectionid="event$.connectionid"/>
```

The admitted form complements the target-free `<dialogprepare>` profile. It
does not create or overwrite a dialog ID; it reads the prepared ID from the
datamodel and asks the provider to attach and start that existing dialog on the
connection carried by the current event.

Standard reference:
[W3C CCXML 1.0, `dialogstart`](https://www.w3.org/TR/ccxml/#dialogstart).

## Compiler contract

`prepareddialogid` is a required dotted NCName readable string location.
`connectionid` is required and must be exactly `event$.connectionid`. All other
attributes and non-whitespace child content are unsupported, including
`dialogid`, `src`, `conferenceid`, `type`, parameter lists, media direction,
cache/HTTP controls, and hints.

The compiler copies the prepared-ID location into bounded program storage,
charges its trailing NUL, marks the program as datamodel-readable, and reserves
one provider effect slot.

## Runtime and provider contract

Session initialization requires the existing readable datamodel prefix,
validates the prepared-ID location once, and requires the append-only
`prepare_prepared_dialog_start` telephony callback.

Dispatch validates the current event connection before reading the datamodel.
The read must return a nonempty, NUL-free borrowed prepared-dialog ID. The core
then calls the provider with borrowed prepared-dialog and connection views and
retains one move-only effect ticket. Refusal or a malformed value/ticket rolls
back every effect already prepared by the transition; commit publishes the
start operation without failure or allocation.

The provider owns dialog lookup, prepared-state validation, media attachment,
VoiceXML execution, and asynchronous `dialog.started`,
`error.dialog.notstarted`, and eventual `dialog.exit` delivery.

## Deferred standard surface

Literal or arbitrary-expression `prepareddialogid`, conference targets,
`type`, `namelist`, `parameters`, `mediadirection`, `maxage`, `maxstale`,
`enctype`, `method`, `hints`, and inline content remain unsupported. Starting a
prepared dialog does not change the already supported direct-source
`<dialogstart>` profile.

## Verification

Tests cover strict syntax and retained-byte bounds, source-independent copies,
session-time readable-location validation, exact provider request bytes,
connection validation ordering, datamodel/provider refusal and malformed
contracts, rollback, append-only callback compatibility, and a real CMeta
prepare-start-terminate lifecycle.
