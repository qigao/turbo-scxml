# SCXML system-variable startup binding design

## Context

TurboSCXML owns the SCXML data-model projection and its W3C-derived corpus.
For an owning CMeta session, `scxml_session_impl.system_values` is the single
fact source for the protected SCXML variables. Session construction assigns:

- `_sessionid` from the generated session UUID;
- `_name` from the root `scxml/@name`, using a non-`NULL` empty string when the
  optional attribute is absent;
- `_ioprocessors.scxml.location` from the session's generated SCXML Event I/O
  address.

The W3C 321, 323, and 325 assertions ask whether `_sessionid`, `_name`, and
`_ioprocessors` are bound during initial eventless processing. CMeta already
exposes their values, but its finite expression grammar can only ask whether
`_event` is bound.

## Decision

Extend the existing `isBound(...)` primary expression to accept exactly four
unquoted system-variable arguments:

```text
isBound(_event)
isBound(_sessionid)
isBound(_name)
isBound(_ioprocessors)
```

Each form lowers to a Boolean operand. Evaluation reads only the corresponding
string-view pointer in the call-scoped `scxml_expr_system_values` object:

- `_event` uses `event_name.data`;
- `_sessionid` uses `session_id.data`;
- `_name` uses `name.data`;
- `_ioprocessors` uses `scxml_location.data`, the witness that the required
  SCXML Event I/O processor entry exists.

A `NULL` system context remains `SCXML_EXPR_EVALUATION_ERROR`. A valid context
with a `NULL` witness returns `false`; a non-`NULL` witness returns `true`, even
when its value has zero bytes. The query does not retain the borrowed view.

## Boundaries and compatibility

This is additive expression syntax implemented inside TurboSCXML. It adds no
public function, structure member, ABI tag, dependency, allocation, queue
operation, or mutable state. Existing expression evaluation and output
preservation on failure remain unchanged.

System-variable assignment semantics were completed by the follow-up
[write-protection decision](scxml-system-variable-write-protection-design.md).
Recognized `_sessionid`, `_name`, `_event[.<path>]`, and
`_ioprocessors[.<path>]` assignment attempts now compile as executable content,
fail before evaluation or mutation, and enqueue `error.execution` through the
existing runtime boundary. Unknown underscore roots and malformed locations
remain admission errors.

## W3C-derived transformations

The local CMeta fixtures preserve these mandatory startup assertions:

- test 321 takes the pass branch only when `isBound(_sessionid)` is true;
- test 323 takes the pass branch only when `isBound(_name)` is true and `_name`
  equals the retained root `name="machineName"` value;
- test 325 takes the pass branch only when `isBound(_ioprocessors)` is true.

Each fixture runs during initialization, terminates in `pass` or `fail`, and
sends exactly one `result.pass` or `result.fail` observation through the
existing bounded strict adapter. The transformation replaces only the W3C
generator's `conf:` predicates and terminal elements.

## Errors, ownership, and verification

The session owns the UUID, document-name copy, and generated SCXML location for
its lifetime. Expressions borrow those immutable bytes for one call. Unknown
or field-qualified `isBound` arguments fail compilation; a missing entire
system context fails evaluation instead of being treated as an unbound
variable.

Verification covers focused expression RED/GREEN tests, malformed syntax,
the three transformed W3C fixtures, manifest inventory validation, the full
TurboSCXML Release CTest suite, and `git diff --check`. Rollback removes the
three operand kinds and fixtures and returns the three manifest rows to
`UNSUPPORTED`; no stored data or external format migration is involved.
