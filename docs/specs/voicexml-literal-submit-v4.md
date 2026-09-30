# VoiceXML literal submit and DialogManager V4

This slice connects the literal VoiceXML `submit` action to the one-attempt
`VoiceXMLSubmitResource` boundary.

## Literal language profile

Accepted:

```xml
<submit next="result.vxml"
        method="get|post"
        enctype="application/x-www-form-urlencoded"/>
```

- `method` defaults to GET.
- urlencoded is the only admitted enctype in this slice.
- absent or whitespace-only `namelist` is allowed.
- dynamic `expr`, nonempty namelist and multipart fail closed.

Multipart belongs to the recorded-audio/#49 boundary; typed CMeta namelist is
a later #148 slice.

The target URI is copied into immutable Program storage. Executing the action
moves the literal Session to `VXML_SESSION_SUBMITTING` and exposes one
borrowed `vxml_submit_target_v1`.

## DialogManager V4

V4 extends the V3 store-backed ownership model with:

- one borrowed `vxml_submit_resource_adapter_v1` + user;
- one hard submit-response byte limit;
- explicit VoiceXML compile limits;
- one shared `max_navigation_hops` budget for external goto and submit.

A V4 submit transfer is:

```text
current document URI
    |
    +-- resolve submit target
    |
    +-- VoiceXMLSubmitResource execute() exactly once
    |      GET/POST + zero literal fields
    |
    +-- while response lease is live:
    |      resolve effective URI
    |      compile returned VoiceXML bytes
    |
    +-- close response lease exactly once
    |
    +-- destroy old Session / release old store borrow or Program
    +-- install compiled response Program directly
    +-- current_document_uri := effective URI (or resolved target)
    +-- start response fragment
    |
    +-- continue bounded goto/submit chain
```

The response is never re-fetched with GET. This is critical for POST: a
successful POST response is compiled directly from the leased bytes.

## Document base ownership

The dialog row owns a bounded copy of the current document URI independently of
whether the active Program comes from DocumentStore or from a submit response.

This lets a submit response perform later relative goto/submit without creating
a fake cache entry or repeating the POST.

## Failure and lifetime

- provider failure publishes no retry;
- `POSSIBLY_PROCESSED` remains a terminal one-attempt failure;
- malformed response compilation closes the provider lease once;
- effective-URI resolution failure closes the lease once;
- submit and goto both consume the same navigation-hop budget;
- V1/V2/V3 manager configs remain unchanged.

Core VoiceXML and DialogManager remain free of CHTTP.
