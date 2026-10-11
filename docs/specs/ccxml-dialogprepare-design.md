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

`dialogid` is a required dotted NCName write location. `src` is required and
nonempty. XML entities are decoded before classification: a quoted string is
a literal without escape processing; other values use the existing typed
string-expression adapter. All other attributes
and non-whitespace child content are unsupported, including `connectionid`,
`conferenceid`, `type`, parameter lists, media direction, cache/HTTP controls,
and hints.

The compiler copies the location and decoded source URI into bounded program
storage, charges one trailing NUL per retained value, and reserves two effect
slots: provider preparation and datamodel writeback.

## Dynamic source ownership and admission

The existing dynamic-string mechanism used by `createcall` and `redirect`
also owns dialog source expressions; no new evaluator or public ABI is added.
The Program owns the decoded source text under `max_name_bytes`. Each Session
compiles one handle per dynamic action and destroys it on admission rollback
or final destruction. Literal sources still accept the legacy datamodel prefix.

Dispatch is synchronous and single-owner. Each execution evaluates the source
against the current datamodel (including staged foreach scope). The returned
view is borrowed only through the immediately following provider prepare;
the provider copies any bytes it retains. NULL, empty, or embedded-NUL results
fail with `CCXML_INVALID_CONTRACT` before provider admission. Evaluation errors
retain the existing adapter-status mapping and discard earlier effect tickets.
No queue, retry, additional thread, or new retained payload is introduced.
Provider refusal and ID writeback failure keep the existing rollback protocol.

This extends the supported typed subset of the standard's `src` expression.
Applications may migrate individual literal sources to typed paths; reverting
to literals restores compatibility with older runtimes. Arbitrary ECMAScript
and string coercion remain outside this profile.

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
ECMAScript expressions, and escaped literals remain unsupported. The
complementary bounded prepared-start profile is specified in
[`ccxml-prepared-dialogstart-design.md`](ccxml-prepared-dialogstart-design.md).

## Verification

Tests cover strict syntax and retained-byte bounds, source-independent copies,
provider request bytes, generated ID writeback and commit order, rollback,
malformed IDs/tickets, session-time location validation, append-only callback
compatibility, real CMeta lifecycle behavior, and the supported build/install
matrix.

Dynamic sources add executable witnesses in `ccxml_program_test` (entity
decoding and exact storage limits), `ccxml_session_test` (reevaluation, legacy
prefix admission, handle cleanup, invalid results and rollback), and
`ccxml_cmeta_test` (root state changes and staged foreach iteration). The
DialogManager integration also runs direct start with a dynamic source.
