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

The historical `vxml_cmeta_collect_request_v1` prefix is unchanged. Its
append-only tail identifies FIELD versus MENU and exposes a Program-owned menu
choice array.

A menu request requires `VXML_CMETA_COLLECT_CAP_MENU_CHOICE`. Providers that
do not advertise the capability are rejected before `prepare()`.

```text
collect prepare / commit / cancel       existing provider lifecycle
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
- with `menu@dtmf="true"`, choices without explicit DTMF receive 1..9 in
  order;
- explicit DTMF under auto mode is restricted to `0`, `*`, or `#`;
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
