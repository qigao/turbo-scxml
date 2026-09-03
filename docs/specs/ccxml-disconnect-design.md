# CCXML Disconnect Slice Design

## Status and scope

This slice extends the bounded CCXML incubation profile with the smallest
useful disconnect operation:

```xml
<disconnect/>
```

The target connection is the nonempty `connection_id` carried by the event
currently being dispatched. The standard's optional `connectionid`, `reason`,
and `hints` attributes remain unsupported, as does nested content. This is not
complete CCXML 1.0 expression or connection-management conformance.

Disconnect is an asynchronous platform operation. Committing the provider
ticket requests disconnection; it does not terminate the CCXML session. The
provider subsequently injects `connection.disconnected` or an appropriate
failure event through the existing session event boundary. This slice does not
add a connection registry: the provider remains authoritative for connection
existence and lifecycle.

## Compiler contract

The compiler admits only an empty, attribute-free `<disconnect/>` action in a
transition. Attributes or nested content are rejected as
`CCXML_UNSUPPORTED_FEATURE`. The compact action row stores only the action
kind, so disconnect adds no retained string or allocation budget.

Programs containing the action record a `uses_disconnect` capability bit for
safe adapter validation at session initialization.

## Adapter contract

The v1 telephony function table gains an append-only optional operation after
`prepare_create_call`:

```c
scxml_adapter_status (*prepare_disconnect)(
    void *user,
    const ccxml_disconnect_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_disconnect_request` borrows the current event's connection identifier
only for the callback. A provider that retains it must copy it before
returning. A program containing `<disconnect/>` requires the complete tail
field and a non-NULL callback. Older adapter prefixes remain valid for programs
that do not use disconnect.

The callback returns the existing move-only effect ticket. Actions prepare in
document order, commit in document order, and discard in reverse order after
any failure. Provider rejection maps to `CCXML_ADAPTER_ERROR`; an accepted but
incomplete ticket maps to `CCXML_INVALID_CONTRACT`.

## Event and error contract

The default target is the current `ccxml_event.connection_id`. A NULL pointer,
zero size, or an embedded NUL makes the selected transition invalid before the
disconnect callback is invoked. Dispatch returns `CCXML_INVALID_EVENT` and
rolls back tickets prepared earlier in the same transition.

This synchronous status is the incubation API's representation of the
standard's `error.semantic` behavior. Once a valid request is committed,
provider-side lookup or disconnect failure is asynchronous and must be
reported through a later event.

## Ownership and lifecycle

- The event owner retains the connection identifier through synchronous
  dispatch; the session and provider callback only borrow it.
- The session owns copied adapter operations and prepared tickets under the
  existing close/quiescence rules.
- A successful disconnect commit leaves the session active.
- Provider connection objects and outcome-event storage are outside this
  slice.

## Verification

The slice is complete when tests prove:

1. empty `<disconnect/>` compiles, while attributes and nested content fail;
2. dispatch prepares and commits the exact current connection identifier;
3. a missing or malformed event connection returns `CCXML_INVALID_EVENT`;
4. mixed actions retain document-order commit and reverse-order rollback;
5. adapters without the appended callback remain valid for older programs;
6. disconnect programs reject missing or truncated callback tails;
7. the full preset matrix and installed consumers remain green.
