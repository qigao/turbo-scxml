# CCXML Redirect Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with a default-target
redirect operation whose destination is a string-literal expression:

```xml
<redirect dest="'tel:+12025550123'"/>
```

The target connection is the nonempty `connection_id` carried by the event
currently being dispatched. The standard's optional `connectionid`, `reason`,
and `hints` attributes remain unsupported, as does nested content. Arbitrary
ECMAScript expressions and escaped destination literals remain unsupported.
The later
[dynamic redirect destination slice](ccxml-dynamic-redirect-design.md) also
accepts unquoted typed CMeta string paths, including staged foreach
`item.member` paths. That additive design controls wherever it extends the
literal-only compiler and datamodel contracts below; XPath and general
ECMAScript remain outside the profile.

Redirect is an asynchronous platform operation. Committing the provider ticket
asks the telephony platform to redirect the call; it does not terminate the
CCXML session. The provider subsequently injects `connection.redirected`,
`connection.redirect.failed`, `connection.failed`, and any required
`conference.unjoined` event through the existing session event boundary.

## Compiler contract

`dest` is required and is the only admitted unqualified attribute. Its value
must contain exactly one nonempty single-quoted or double-quoted string
literal. Leading or trailing bytes outside the quotes, a backslash escape, or
an unescaped matching quote inside the literal is rejected as
`CCXML_UNSUPPORTED_FEATURE`. Empty literals are rejected as
`CCXML_INVALID_STRUCTURE`.

The compiler strips the expression quotes and copies the destination bytes
into program-owned storage. The bytes share the existing `max_name_bytes`
retained-string budget. Nested content is rejected as
`CCXML_UNSUPPORTED_FEATURE`. Programs containing redirect record a
`uses_redirect` capability bit.

## Adapter contract

The v1 telephony function table gains an append-only operation after
`prepare_reject`:

```c
scxml_adapter_status (*prepare_redirect)(
    void *user,
    const ccxml_redirect_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_redirect_request` borrows the current event connection identifier and
the compiled destination only for the callback. A provider retaining either
value must copy it before returning. A program containing `<redirect>` requires
the complete tail field and a non-NULL callback. Older adapter prefixes remain
valid for programs that do not use redirect.

The callback returns the existing move-only effect ticket. Actions prepare in
document order, commit in document order, and discard in reverse order after
any failure. Provider rejection maps to `CCXML_ADAPTER_ERROR`; an accepted but
incomplete ticket maps to `CCXML_INVALID_CONTRACT`.

## Event, state, and error contract

The default target is the current `ccxml_event.connection_id`. A NULL pointer,
zero size, or embedded NUL makes the selected transition invalid before the
redirect callback. Dispatch returns `CCXML_INVALID_EVENT` and rolls back
tickets prepared earlier in the same transition. This synchronous status is
the incubation API representation of `error.semantic` for a missing default.

The core has no connection or bridge registry in this slice. The provider
therefore validates whether the target is in `ALERTING` or `CONNECTED`, tears
down an existing bridge when required, and emits the resulting asynchronous
events. A successful commit leaves the session active.

## Ownership and lifecycle

- The program owns decoded destination bytes until program destruction.
- The event owner retains the connection identifier through synchronous
  dispatch; the session and callback only borrow it.
- The session owns copied adapter operations and effect tickets under the
  existing close/quiescence contract.
- Provider connection objects, bridges, protocol reasons, and event storage
  remain outside this slice.

## Verification

The slice is complete when tests prove:

1. a quoted destination compiles and survives source overwrite;
2. missing, empty, escaped, extra-attribute, and nested forms fail, while
   dynamic sources defer typed validation to session admission;
3. dispatch passes exact connection and destination bytes and commits once;
4. missing or malformed event connection IDs roll back earlier tickets;
5. mixed actions retain document-order commit and reverse-order rollback;
6. older adapter prefixes remain valid for programs without redirect;
7. redirect programs reject absent, NULL, or truncated callback tails;
8. all presets and installed consumers remain green.
