# VoiceXML static menu collect boundary

This slice implements the bounded event-target half of VoiceXML `menu` /
`choice` without creating a second recognizer runtime and without inventing
an application-root field for the anonymous menu item.

## Program model

```text
document-level <menu id=...>
        |
        v
shared vxml_form_row entry
        |
        +-- CMeta form row: menu = menu index
        |
        +-- immutable public choice-view array
        |      { dtmf, speech(empty in this slice) }
        |
        +-- private target side table
               { EVENT, Program-owned literal event }
```

The public choice array is intentionally separated from the private target
side table. Array element ABI cannot be tail-extended safely because providers
traverse the array using `sizeof(vxml_cmeta_menu_choice_v1)`.

## Collect ABI

The historical `vxml_cmeta_collect_request_v1` is unchanged byte-for-byte.
This matters because it is also a caller-owned output object; growing it would
let a new library overwrite an old caller's smaller allocation.

Menus instead use `vxml_cmeta_menu_collect_request_v1` and an append-only
`prepare_menu` callback at the end of `vxml_cmeta_collect_adapter_v1`.
Historical adapters remain valid through the old `cancel` prefix.

A menu request requires `VXML_CMETA_COLLECT_CAP_MENU_CHOICE`. Providers that
do not advertise the capability are rejected before the menu tail callback is
inspected or invoked.

```text
field prepare / menu prepare / commit / cancel
                     existing provider lifecycle
                 |
                 v
        generation-scoped mailbox
                 |
        +--------+--------+
        |                 |
     FIELD              MENU
 typed values       choice ordinal
```

The same committed generation cannot accept both completion forms.

## Static DTMF profile

This slice accepts literal `choice@event` only.

- without `menu@dtmf="true"`, every choice supplies a non-empty DTMF
  sequence;
- with `menu@dtmf="true"`, the first nine choices without explicit DTMF
  receive 1..9 in order; later implicit choices remain valid with no DTMF
  assignment;
- explicit DTMF under auto mode is restricted to `0`, `*`, or `#`;
- optional whitespace in explicit DTMF is removed before validation
  (`"1 2 #"` and `"12#"` are equivalent);
- duplicate normalized DTMF sequences reject;
- lowercase A-D normalize to uppercase outside auto mode;
- choice speech content, generated speech grammar, dynamic targets, messages,
  and navigation are deferred rather than approximated.

## Completion and Event routing

`vxml_session_cmeta_menu_try_complete()` copies only
`{generation, choice_index}` into the shared mailbox. It retains no provider
bytes.

The single-owner `vxml_session_cmeta_collect_run_ready()` validates the
generation and ordinal, disarms that generation, then raises the immutable
literal Event through the existing scoped Event dispatcher. If the Event is
handled and the Session remains RUNNING, the menu is re-armed with a fresh
generation. Stale callbacks from the prior generation therefore cannot mutate
the new menu attempt.

Literal `choice@next` is tracked separately by #170 so navigation storage and
DocumentStore ownership remain aligned with the already-delivered #48
boundary.
