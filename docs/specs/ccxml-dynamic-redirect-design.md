# CCXML Dynamic Redirect Destination Design

## Scope

This slice extends the existing typed dynamic-string boundary from
`<createcall dest="...">` to `<redirect dest="...">`. Quoted, nonempty
redirect destinations retain their literal behavior. Every other nonempty
decoded value is compiled by the configured datamodel adapter when the
session is admitted and evaluated immediately before `prepare_redirect`.

XPath and general ECMAScript remain outside this profile. The built-in
implementation uses the existing CMeta expression compiler and supports
root string paths, `_event.name`, and typed staged foreach paths such as
`item.destination`.

## Compile and admission behavior

The XML compiler decodes entities before classifying `redirect@dest`.
Matching single- or double-quoted values remain literals; unquoted nonempty
values become dynamic expression sources. The retained bytes continue to use
the existing `max_name_bytes` budget, and the action's existing
`destination_is_dynamic` bit selects expression handling.

No public ABI changes are required. Dynamic redirects use the additive
`compile_string_expression`, `evaluate_string_expression`, and
`destroy_string_expression` tail already present in
`ccxml_datamodel_adapter_v1`. A literal redirect continues to work without
that tail. Each dynamic action owns one compiled handle per session, and all
normal admission rollback and exact-once destruction rules apply.

## Dispatch and transaction behavior

Dispatch validates the current Event connection ID before evaluating the
destination. This preserves the redirect contract and avoids invoking the
datamodel when the mandatory current connection is absent or malformed.
The resulting destination must be non-NULL, nonempty, and contain no embedded
NUL, as enforced by the shared string-expression evaluator.

The evaluated view is borrowed only through the immediately following
`prepare_redirect` callback. Evaluation failure discards earlier provider
tickets and rolls back staged foreach state. Provider rejection follows the
existing reverse-order ticket rollback. Successful preparation participates
in the existing document-order commit.

When foreach is present, admission compiles against the global supplemental
scope. Evaluation reads the staged scope during a transaction and committed
scope otherwise, so each redirect observes the current typed iteration.

## Compatibility and non-goals

- Literal redirect programs remain compatible with legacy datamodel prefixes.
- Telephony adapter layout and redirect request layout do not change.
- Explicit `connectionid`, `reason`, `hints`, and other redirect options remain
  unsupported.
- String concatenation, coercion, XPath, and QuickJS integration are not added.
- Other CCXML string-valued attributes are separate incremental slices.

## Verification

Tests cover decoded dynamic classification, literal compatibility, adapter-tail
requirements, handle lifecycle, evaluation ordering, invalid result rejection,
rollback, provider delivery, and typed staged foreach evaluation. The full
Release CTest suite must remain green.
