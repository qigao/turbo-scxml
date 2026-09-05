# CCXML Dynamic Conference Name Design

## Scope

This slice extends the existing typed dynamic-string boundary to the optional
`confname` attribute of `<createconference>`. A quoted, nonempty conference
name retains its literal behavior. An unquoted nonempty value is compiled by
the configured datamodel adapter during session admission and evaluated
immediately before `prepare_create_conference`.

XPath and general ECMAScript remain outside this profile. The built-in CMeta
implementation supports root string paths, `_event.name`, and typed staged
foreach paths such as `item.destination`.

## Compile and admission behavior

The compiler keeps `conferenceid` as the required writable dotted location.
When `confname` is present, XML entities are decoded before the value is
classified. Matching single- or double-quoted values are retained as literals;
other nonempty decoded values are retained as dynamic expression sources.
Both forms consume the existing `max_name_bytes` budget including their
trailing NUL.

No public ABI changes are required. The action reuses `destination`,
`destination_size`, and `destination_is_dynamic`. Dynamic names set the
program's existing `uses_string_expression` capability and use the additive
`compile_string_expression`, `evaluate_string_expression`, and
`destroy_string_expression` datamodel tail. Omitted and literal names do not
require that tail beyond the datamodel write capability already required for
`conferenceid`.

## Dispatch and transaction behavior

For a dynamic name, dispatch evaluates the action's expression before calling
the provider. The returned view must be non-NULL, nonempty, and contain no
embedded NUL, as enforced by the shared evaluator. The view is borrowed only
through the immediately following `prepare_create_conference` callback.

After provider acceptance, the existing flow validates the returned conference
ID and prepares its write to `conferenceid`. Commit still publishes the ID
write before the provider reservation. Expression failure discards any earlier
transition tickets; provider failure and writeback failure retain their current
reverse-order rollback behavior. During foreach, expression evaluation uses
the staged supplemental scope for the current iteration.

## Compatibility and non-goals

- Omitted and literal `confname` forms retain their current behavior.
- The telephony and datamodel adapter layouts and ABI constants do not change.
- `reservedtalkers`, `reservedlisteners`, `hints`, and arbitrary
  `conferenceid` expressions remain unsupported.
- String concatenation, coercion, XPath, QuickJS integration, and dynamic
  conference IDs are not added.

## Verification

Tests cover decoded classification, legacy expression-tail compatibility,
per-session handle lifecycle, provider delivery, empty/NUL evaluation errors,
rollback before provider admission, returned-ID writeback ordering, and staged
CMeta foreach lookup. The full Release CTest suite must remain green.
