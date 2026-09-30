# VoiceXML terminal control contract

This slice distinguishes terminal control from terminal payload.

```text
<exit expr|namelist>  -> VXML_CMETA_TERMINAL_EXIT
<return namelist>     -> VXML_CMETA_TERMINAL_RETURN
<return event>        -> VXML_CMETA_TERMINAL_RETURN_EVENT
<disconnect/>         -> VXML_CMETA_TERMINAL_DISCONNECT
FIA exhaustion        -> VXML_CMETA_TERMINAL_NONE
```

The Session still enters `VXML_SESSION_EXITED` for all terminal controls.
Call `vxml_session_cmeta_terminal_kind()` to distinguish why. Historical
`vxml_session_cmeta_exit_kind/count/at` remain compatible and continue to
expose the bounded terminal value snapshot. A return namelist reuses that
owned snapshot; no provider-owned bytes survive publication.

A literal return Event is copied into immutable Program storage and can be
borrowed with `vxml_session_cmeta_terminal_event()`. The runtime does not
dispatch it locally: #164 owns subdialog parent/child routing.

The bounded CMeta profile deliberately rejects dynamic `eventexpr`,
`messageexpr`, and message plumbing in this slice rather than coercing them.
`disconnect` is a terminal control signal; the owning dialog/telephony layer
is responsible for the actual connection operation and final-processing
integration.
