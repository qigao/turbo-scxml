# CCXML Reject Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with the smallest
incoming-call rejection operation:

```xml
<reject/>
```

The target connection is the nonempty `connection_id` carried by the event
currently being dispatched. The standard's optional `connectionid`, `reason`,
and `hints` attributes remain unsupported, as does nested content. This is not
complete CCXML 1.0 expression or connection-state conformance.

Reject is an asynchronous platform operation. Committing the provider ticket
asks the telephony platform to decline the incoming connection; it does not
terminate the CCXML session. The provider subsequently injects
`connection.disconnected`, `connection.reject.failed`, or `connection.failed`
through the existing session event boundary.

## Compiler contract

The compiler admits only an empty, attribute-free `<reject/>` action in a
transition. Attributes or nested content are rejected as
`CCXML_UNSUPPORTED_FEATURE`. The compact action row stores only the action
kind, so reject adds no retained string or allocation budget.

Programs containing the action record a `uses_reject` capability bit for safe
adapter validation during session initialization.

## Adapter contract

The v1 telephony function table gains an append-only optional operation after
`prepare_disconnect`:

```c
scxml_adapter_status (*prepare_reject)(
    void *user,
    const ccxml_reject_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_reject_request` borrows the current event's connection identifier only
for the callback. A provider that retains it must copy it before returning. A
program containing `<reject/>` requires the complete tail field and a non-NULL
callback. Older adapter prefixes remain valid for programs that do not use
reject.

The callback returns the existing move-only effect ticket. Actions prepare in
document order, commit in document order, and discard in reverse order after
any failure. Provider rejection maps to `CCXML_ADAPTER_ERROR`; an accepted but
incomplete ticket maps to `CCXML_INVALID_CONTRACT`.

## Event, state, and error contract

The default target is the current `ccxml_event.connection_id`. A NULL pointer,
zero size, or embedded NUL makes the selected transition invalid before the
reject callback is invoked. Dispatch returns `CCXML_INVALID_EVENT` and rolls
back tickets prepared earlier in the same transition. This synchronous status
is the incubation API's representation of the standard's `error.semantic`
behavior for a missing default target.

The core has no connection registry in this slice and therefore does not infer
whether the identifier names an incoming connection in the `ALERTING` state.
The provider validates authoritative state after commit and reports the
standard asynchronous success or failure event. A successful commit leaves the
session active.

## Ownership and lifecycle

- The event owner retains the connection identifier through synchronous
  dispatch; the session and provider callback only borrow it.
- The session owns copied adapter operations and prepared tickets under the
  existing close/quiescence rules.
- Provider connection objects, protocol reason mapping, and outcome-event
  storage remain outside this slice.

## Verification

The slice is complete when tests prove:

1. empty `<reject/>` compiles, while attributes and nested content fail;
2. dispatch prepares and commits the exact current connection identifier;
3. missing or malformed event connection identifiers fail before the callback;
4. mixed actions retain document-order commit and reverse-order rollback;
5. adapters without the appended callback remain valid for older programs;
6. reject programs reject missing, NULL, or truncated callback tails;
7. the full preset matrix and installed consumers remain green.
