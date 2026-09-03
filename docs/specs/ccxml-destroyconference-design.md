# CCXML Destroy Conference and Datamodel Read Design

## Status and scope

This slice extends the bounded CCXML incubation profile with conference
destruction and an explicit string-value read boundary:

```xml
<destroyconference conferenceid="conference.id"/>
<destroyconference conferenceid="'conference-42'"/>
```

`conferenceid` is a required expression. This slice admits either a nonempty
quoted string literal or a dotted NCName datamodel location. `hints`, escaped
literals, and arbitrary ECMAScript expressions remain unsupported.

## Compiler contract

The compiler copies either the decoded literal value or the location into
bounded program storage, including one trailing NUL in `max_name_bytes`
accounting. The action consumes one provider effect slot. Empty identifiers,
invalid dotted locations, unsupported expressions, attributes, or child
content are rejected during admission.

## Datamodel contract

The v1 datamodel table gains append-only `validate_readable_string_location`
and `read_string` operations. Programs containing only literal destroy
expressions do not require a datamodel. A location expression requires both
new operations: validation runs once during session initialization and read
runs synchronously during dispatch.

An accepted read returns a borrowed, nonempty byte view with no embedded NUL.
It remains valid through the immediately following telephony prepare call;
the provider must copy bytes it retains. The core never treats the location or
expression source text as a conference identifier.

The existing writeback prefix remains valid for create-conference-only
programs after the table grows. The CMeta implementation resolves readable
string fields through the configured bounded root and returns a view obtained
through CMeta buffer operations without allocating or mutating state.

## Provider contract

The v1 telephony table gains append-only `prepare_destroy_conference`. Its
request carries only the evaluated conference identifier. Accepted prepare
transfers one move-only effect ticket; commit publishes the already-reserved
detach/destroy operation and discard rolls it back.

The provider owns conference lookup, session attachment tracking, the rule
that the global conference is destroyed only after its last attachment is
removed, and asynchronous `conference.destroyed` or
`error.conference.destroy` delivery.

## Transaction and lifetime

Datamodel reading creates no effect ticket and occurs in document order just
before provider prepare. Any read refusal, malformed view, provider refusal,
or malformed provider ticket discards all earlier prepared effects in reverse
order. All retained program strings live until program destruction; adapter
tables are copied and adapter users remain borrowed through session
destruction.

## Verification

Tests cover both expression forms, strict syntax and storage bounds,
source-independent copies, datamodel capability and location validation,
exact evaluated provider requests, read/provider failures, reverse rollback,
malformed views/tickets, append-only callback-prefix compatibility, CMeta
nested string reads, and the supported build/install matrix.
