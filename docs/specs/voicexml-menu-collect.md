# VoiceXML static menu collect boundary

This boundary implements bounded static VoiceXML `menu` / `choice`
collection without creating a second recognizer runtime and without inventing
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
        |      { dtmf, speech(exact normalized phrase or empty) }
        |
        +-- private target side table
               { EVENT | NEXT, Program-owned literal bytes }
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

A menu request always requires `VXML_CMETA_COLLECT_CAP_MENU_CHOICE`. If any
choice carries an automatically generated exact speech phrase, the request also
requires `VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT`. Providers missing either
required capability are rejected before `prepare_menu` is invoked.

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

The static profile accepts exactly one literal `choice@event` or
`choice@next` per choice.

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
- exact choice PCDATA is normalized by trimming leading/trailing XML
  whitespace and collapsing internal XML whitespace runs to one ASCII space;
- the normalized phrase is stored in the existing stable `choice.speech`
  view and is bounded by `max_menu_choice_bytes`;
- `accept="exact"` is supported at menu or choice scope; choice scope
  overrides the menu setting;
- `accept="approximate"` fails closed because equivalent subphrase grammars
  may be language/platform dependent and require a future explicit capability;
- explicit child `<grammar>`, dynamic targets, and messages remain deferred
  rather than approximated;
- literal next targets have an independent `max_menu_target_bytes` bound;
  event-only callers using the earlier menu compile-options prefix remain
  compatible;
- a local `#fragment` must name one valid dialog NCName.

## Completion routing

`vxml_session_cmeta_menu_try_complete()` copies only
`{generation, choice_index}` into the shared mailbox. It retains no provider
bytes.

The single-owner `vxml_session_cmeta_collect_run_ready()` validates the
generation and ordinal and disarms that generation before publishing the
selected target.

For EVENT, it raises the immutable literal Event through the existing scoped
Event dispatcher. If the Event is handled and the Session remains RUNNING, the
menu is re-armed with a fresh generation.

For NEXT, it publishes the Program-owned target into the existing Session
navigation fields and enters `VXML_SESSION_NAVIGATING`. Callers then use the
historical `vxml_session_navigation_request()`; DialogManager/DocumentStore
remain responsible for relative resolution, current-document fragments,
fetching, and form selection. No menu-specific resolver or URI allocation is
introduced.

Stale callbacks from the prior generation cannot publish either an Event or a
navigation request.

## Exact generated speech

The exact-speech slice does not add a second grammar representation. Providers
receive the normalized Program-owned phrase directly in
`vxml_cmeta_menu_choice_v1.speech` and return the same choice ordinal through
the existing menu completion ABI.

Pure DTMF menus do not require the speech capability. A menu with at least one
non-empty `speech` view requires
`VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT` in addition to MENU_CHOICE.

Approximate matching is deliberately not expanded in core. VoiceXML permits
platform/language-dependent approximate grammar generation, so a future
profile must make that capability and per-choice acceptance mode explicit
without changing the stable choice-array stride.
