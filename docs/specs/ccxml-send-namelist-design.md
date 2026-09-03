# CCXML Send Namelist Design

## Scope

This increment adds bounded `namelist` support to the existing literal CCXML
`<send>` profile. It does not add general ECMAScript evaluation or inline XML
content.

The [W3C CCXML `<send>` contract](https://www.w3.org/TR/ccxml/#Send) says that
`namelist` is a whitespace-separated list of variable names whose values are
evaluated when `<send>` executes. Names retain their qualification, and object
values sent through the `ccxml` processor map to nested event properties.

## Accepted profile

- `namelist` may be empty or contain at most `SCXML_PAYLOAD_MAX_ENTRIES`
  whitespace-separated dotted NCName locations.
- XML entities are decoded before tokenization and validation.
- Entry order and duplicate names are preserved.
- Each retained token is charged, including its terminator, to
  `ccxml_limits.max_name_bytes`.
- A non-empty namelist requires `SCXML_EVENT_IO_CAP_PAYLOAD` and the datamodel
  payload-read tail operations during session initialization.

General ECMAScript variable references, computed property access, inline XML
content, dynamic target/name/targettype/delay expressions, and `hints` remain
outside this bounded profile.

## Shared payload boundary

The core emits the existing `scxml_payload_view`; CCXML does not define a
second payload ABI. Each namelist token becomes one `scxml_payload_entry` whose
name is the exact decoded token:

- Boolean, signed integer, unsigned integer, floating-point, enum, and string
  values use `SCXML_CONTENT_SCALAR`.
- Structured or other schema-backed values use `SCXML_CONTENT_CMETA` and
  borrow the resolved `cmeta_data_desc` plus object.

The datamodel adapter gains a struct-size-protected tail consisting of
`validate_payload_location` and `read_payload`. Validation runs before the
session is published. A read is side-effect free and returns a callback-scoped
`scxml_content_view` valid through the immediately following Event I/O
`prepare_send` call. The Event I/O provider must copy anything it retains.

This representation preserves enough information for a `ccxml` processor to
construct nested event properties. BasicHTTP can continue encoding scalar
entries as form fields; CCXML leaves direct submission of ECMAScript objects
undefined for BasicHTTP, so a processor may reject `SCXML_CONTENT_CMETA`.

## Compact program and session storage

The compiler stores all namelist descriptors in one flat `ccxml_payload_row`
array. Each send action records `payload_first` and `payload_count`; the program
records the largest per-send count. The session allocates that many
`scxml_payload_entry` scratch rows once at initialization. Dispatch performs no
payload-array allocation.

At execution, the session reads each location in source order into scratch,
validates the returned view, and invokes Event I/O prepare with
`SCXML_PAYLOAD_NAMED`. Empty namelists remain `SCXML_PAYLOAD_NONE` and do not
negotiate payload capability.

## Transactions and lifetime

Payload reads observe the currently committed external datamodel state. They
do not create effect tickets. Event I/O prepare still creates the send ticket;
optional `sendid` assignment still creates a second datamodel ticket, reordered
so assignment commits before send publication. Any payload-read or prepare
failure discards all tickets already staged by the transition.

Because datamodel writes are committed only after the transition succeeds, a
namelist does not observe an assignment staged earlier in the same transition.
Programs that need the new value send from a later transition.

## Validation

Tests cover decoded tokenization, empty lists, malformed locations, the
per-send entry bound, retained-byte accounting, capability negotiation,
ordered scalar payload materialization, read failure rollback, and built-in
CMeta scalar/object views.
