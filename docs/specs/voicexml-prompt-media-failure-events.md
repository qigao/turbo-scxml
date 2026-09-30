# VoiceXML prompt-media failure Events

Prompt-media providers remain backend-neutral adapters. TurboSCXML owns only
bounded admission, generation-safe completion ingress, and scoped VoiceXML
Event delivery.

## Mapping

| Media condition | VoiceXML Event |
| --- | --- |
| queue/resource capacity unavailable | `error.noresource` |
| unsupported media / codec / type | `error.unsupported.format` |
| terminal media/resource fetch failure | `error.badfetch` |

A stale completion is not a current VoiceXML failure. It returns the existing
`STALE` ingress result and raises no Event.

Audio fallback remains a provider-side recovery contract. A terminal failure
completion is reported only when the provider could not recover through the
compiled alternate-content range.

## Admission

The existing provider operation signatures are unchanged.

- capability mismatch before callback maps to
  `error.unsupported.format`;
- provider `VXML_UNSUPPORTED_FEATURE` maps to
  `error.unsupported.format`;
- provider `VXML_LIMIT_EXCEEDED` maps to `error.noresource`;
- invalid arguments/contracts remain programming errors and are not
  reclassified.

The prepare ticket is never published on failure. A provider that returned a
discardable partial ticket has it discarded before Event dispatch.

When the Event is caught, the prepare API keeps returning the original media
status, so the caller can distinguish admission failure while the VoiceXML
handler commits normally. An uncaught Event follows the existing Session
failure path.

## Completion ABI

`vxml_cmeta_prompt_media_completion_v1` appends one enum:

```c
typedef enum vxml_cmeta_prompt_media_failure {
    VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE = 0,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE
} vxml_cmeta_prompt_media_failure;
```

The V1 version number is unchanged. Ingress validates the historical prefix
through `outcome`.

For a historical FAILED completion with no failure tail, the deterministic
compatibility mapping is `NORESOURCE`.

A COMPLETED record must carry NONE when the tail is present.

## Concurrency and lifetime

`try_complete()` remains the nonblocking MPSC ingress. It copies only:

```text
generation
outcome
failure enum
```

It retains no provider pointer, message, URI, media bytes, or lease.

`run_ready()` is the single-owner boundary. It validates the generation,
settles the in-flight prompt, disarms completion ingress, then raises the
mapped scoped Event for FAILED outcomes.

Therefore a late or duplicate completion cannot raise an Event for a newer
prompt generation.
