# VoiceXML static subdialog descriptor

This slice adds the language-side `subdialog` form item without starting a
child runtime.

## Program model

```text
<form>
  <field .../>
  <subdialog name="child" src="child.vxml#entry" cond="..."/>
  <field .../>
</form>
      |
      v
form_items[] preserves source order
      |
      +-- FIELD
      +-- SUBDIALOG -> immutable Program row
      +-- FIELD
```

A subdialog row owns:

- parent form index;
- application-root result field index/offset;
- stable CMeta STRUCT descriptor for the result object;
- immutable decoded `name` and literal `src` bytes;
- optional compiled Boolean `cond`.

The result field's root bound bit is the form-item state. No hidden Boolean
control slot is created.

## Eligibility

A static subdialog is eligible when:

1. its result STRUCT root field is undefined;
2. its optional `cond` evaluates true.

Selection sets a private `active_subdialog` and advances a separate
`subdialog_generation`. It does not call a fetcher, child provider, collect
adapter, worker, or network stack.

FIELD/INITIAL recognition continues using `collect_generation`; subdialog
selection never aliases that generation.

## Fail-closed surface

This slice accepts literal `name`, `src`, and optional `cond`.

- `src + srcexpr` is invalid;
- dynamic `srcexpr` is unsupported;
- `expr` is unsupported because the current CMeta expression value ABI has no
  STRUCT-valued result;
- child `param`, scoped Event, and `filled` processing are later slices;
- form-level `filled` with a subdialog remains unsupported until RETURN_DATA
  processing is defined.

The configured `max_subdialogs` and `max_subdialog_uri_bytes` bounds are
append-only compile-option tails. Old documents do not require them.

## Ownership

`src` and `name` are copied into immutable Program storage. The CMeta result
descriptor is borrowed from the compile-time root schema, whose lifetime is
already part of the CMeta Program contract.

Session initialization validates every subdialog row against the active root
schema before any FIA execution. A corrupted row fails fast with
`VXML_INVALID_CONTRACT`.

While the parent is suspended at a subdialog, external parent
`vxml_session_cmeta_raise()` calls fail with `VXML_INVALID_STATE`; #186 will
introduce the explicit RETURN_EVENT path.

## Next slices

- #185: owned typed parameter snapshot + child-owner request ABI;
- #186: RETURN_DATA / RETURN_EVENT / GLOBAL_EXIT completion;
- #187: DocumentStore-backed child owner and nested navigation.
