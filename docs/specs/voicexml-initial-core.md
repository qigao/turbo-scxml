# VoiceXML mixed-initiative initial core

This slice implements the bounded Form Interpretation Algorithm core for
`<initial>` without adding an application-root value for the control item.

## Program model

A mixed form owns a private source-order form-item table:

```text
form.items[] = FIELD(index) | INITIAL(index)
```

Field rows remain the existing typed application-root bindings. Initial rows
own only a form-scope Boolean control slot plus optional `expr` and `cond`.
The form owns one immutable external SRGS grammar `{type, src}`.

## Selection

SELECT walks the private item table in document order.

For a FIELD, existing eligibility applies: the root value must be undefined and
the optional field condition must evaluate true.

For an INITIAL, its control slot must be undefined. With an explicit `cond`,
that condition is evaluated by the bounded CMeta expression engine. Without a
condition, the item is eligible only while no input field in the form is
filled.

No hidden application-root field is synthesized for an initial item.

## Provider boundary

Initial recognition uses a separate caller-owned
`vxml_cmeta_initial_collect_request_v1`; field/menu request layouts are
unchanged. The existing collect adapter is extended only with an append-only
`prepare_initial` tail.

Admission requires both:

- `VXML_CMETA_COLLECT_CAP_SRGS_XML`
- `VXML_CMETA_COLLECT_CAP_INITIAL_MULTI`

Capability/tail mismatches fail before the provider callback.

## Completion and PROCESS

An initial generation accepts only V2 multi-slot semantic completion. Every
slot must resolve to a real field in the same form. Unknown, duplicate, or
out-of-form slots are incompatible and do not mutate Session state.

The owner-thread progress point performs one transaction:

```text
copy all semantic slots into staged root
    -> mark every initial control in the form filled
    -> run completed field-level filled handlers
    -> run eligible form-level filled handlers
    -> commit
    -> FIA SELECT
```

Any failure rolls the whole transaction back. V1 scalar completion is
incompatible with an active INITIAL generation.

`<clear/>` clears initial controls together with fields so mixed-initiative
entry can be revisited deterministically.

## Deferred

Initial-local prompts, retry counters, and initial-local
`catch/help/noinput/nomatch` scope are deliberately deferred to #178. They
reuse the existing prompt-media and scoped-Event ownership model rather than
adding another recognizer or worker.
