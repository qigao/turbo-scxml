# CMeta Custom Actions Implementation Plan

1. Add the V2 compile provider and custom-action registration declarations to
   the public header without changing V1 layout or behavior.
2. Add compile-provider validation tests before implementing the new entry
   point.
3. Thread the validated registry through `scxml_build`, add owned action IR and
   accounting fields, and preserve the bound callable in `scxml_program_impl`.
4. Teach executable-content analysis to route foreign namespaces to the custom
   action analyzer while retaining strict SCXML namespace checks elsewhere.
5. Validate empty element shape and exact registered attributes; compile each
   attribute value against the callable parameter descriptor.
6. Emit a `SCXML_STEP_CUSTOM_ACTION` descriptor and destroy all argument
   expression programs on failure and program teardown.
7. Evaluate arguments into aligned scalar scratch, invoke the bound callable,
   discard the return value, and map dispatch failure to `error.execution`.
8. Add behavior tests for top-level executable blocks, nested `if`/`foreach`,
   `finalize`, malformed and duplicate registrations, and runtime rejection.
9. Update public docs and run focused plus full preset verification.
