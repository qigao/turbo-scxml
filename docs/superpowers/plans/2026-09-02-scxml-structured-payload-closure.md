# Structured Payload Closure Implementation Plan

1. Add focused CMeta tests that currently fail admission for internal named
   payloads and structured done-data content expressions.
2. Extend send descriptors with a compile-time root-field projection schema;
   account for its rows and stable-id storage during admission and allocation.
3. Emit field metadata for internal `namelist`/`param` payloads and destroy it
   through the existing descriptor teardown path.
4. Materialize the named payload into a reserved Event metadata object before
   publishing the tagged internal Event; release it on every pre-commit error.
5. Emit rich done-data content expressions as resolved CMeta locations rather
   than scalar expression programs.
6. Copy-construct the bounded selected object into the completion slot, publish
   its descriptor as the Event data root, and reuse the existing completion
   release path.
7. Cover both payload forms through real internal/completion Events and retain
   the existing negative lifecycle/capacity regression suite.
8. Run the focused test binary, full Release build and CTest preset.
