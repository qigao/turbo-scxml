# CCXML Create Call Slice Design

## Status and scope

This slice extends the bounded CCXML core with the smallest useful outbound
call operation. It accepts an empty `<createcall>` action whose required
`dest` attribute is an ECMAScript string-literal expression, for example:

```xml
<createcall dest="'tel:+12025550123'"/>
```

This is an incubation profile, not complete CCXML 1.0 expression or
`<createcall>` conformance. `connectionid`, `aai`, `callerid`, `hints`,
`timeout`, `joinid`, and `joindirection` remain unsupported. Arbitrary
ECMAScript expressions and escaped string literals remain unsupported.

The later
[dynamic string expression slice](ccxml-dynamic-string-expression-design.md)
also accepts unquoted typed CMeta string paths, including staged foreach
`item.member` paths. That additive design controls wherever it extends the
literal-only compiler and datamodel contracts below; XPath and general
ECMAScript remain outside the profile.

The profile follows CCXML 1.0 section 10.5.4 in treating call placement as an
asynchronous platform operation. Committing the provider ticket starts the
attempt. The provider subsequently injects `connection.progressing`,
`connection.connected`, or `connection.failed` through the existing session
event boundary. The core does not synthesize those platform outcomes.

## Compiler contract

`dest` is required and is the only admitted unqualified attribute. Its value
must contain exactly one nonempty single-quoted or double-quoted string
literal. Leading or trailing bytes outside the quotes, a backslash escape, or
an unescaped matching quote inside the literal is rejected as
`CCXML_UNSUPPORTED_FEATURE`. Empty literals are rejected as
`CCXML_INVALID_STRUCTURE`.

The compiler strips the outer expression quotes and copies the destination
bytes into program-owned storage. Event names and destination bytes share the
existing `max_name_bytes` retained-string budget. This avoids a new unbounded
allocation surface while preserving the public limits layout.

## Adapter contract

The v1 telephony function table gains an append-only optional operation:

```c
scxml_adapter_status (*prepare_create_call)(
    void *user,
    const ccxml_create_call_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
```

`ccxml_create_call_request` borrows the copied destination only for the
callback. A provider that retains it must copy it before returning. A program
containing `<createcall>` requires this tail operation at session
initialization. Programs using only the original action set accept the legacy
v1 prefix ending at `is_quiescent`.

The session copies only the number of adapter bytes announced by
`struct_size`, capped at the current structure size. It never reads beyond a
legacy prefix. `prepare_create_call` returns the same move-only effect ticket
contract as `prepare_accept`: all actions prepare in document order, commit in
document order, and discard in reverse order after any failure. Provider
rejection maps to `CCXML_ADAPTER_ERROR`; an incomplete accepted ticket maps to
`CCXML_INVALID_CONTRACT`.

## Ownership and lifecycle

- The program owns the decoded destination bytes until program destruction.
- The session borrows the program, adapter user, and provider-owned ticket
  users under the existing lifecycle contract.
- A successful commit transfers responsibility for eventual connection events
  to the provider; it does not terminate the session.
- Close and quiescence behavior is unchanged.

## Verification

The slice is complete when tests prove:

1. a standard quoted `tel:` destination compiles and survives source overwrite;
2. missing, empty, escaped, extra-attribute, and nested-content forms fail
   deterministically, while dynamic sources defer typed validation to session
   admission;
3. dispatch prepares and commits the exact copied destination;
4. mixed accept/create-call actions preserve document-order commit and
   reverse-order rollback;
5. a legacy adapter prefix remains valid for accept-only programs;
6. a create-call program rejects an adapter without the appended operation;
7. the full preset matrix and installed consumers remain green.
