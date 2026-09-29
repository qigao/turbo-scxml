# CMeta Custom Executable Action Design

## Scope

TurboSCXML supports compile-scoped foreign-namespace executable elements for
the CMeta data model. SCXML owns XML element identity, attribute-expression
binding, lifecycle and `error.execution` behavior. CMeta owns reflected
function semantics. Exact callable execution remains a separate admitted
representation.

## Canonical FunctionDesc-first contract

New code uses `scxml_compile_cmeta_v3()` with
`scxml_cmeta_custom_action_v2` rows:

- exact namespace URI and local name identify the XML element;
- `cmeta_function_desc` is the sole semantic source for parameter names,
  order, types, directions, effects and properties;
- `cmeta_function_abi_desc` supplies the exact reflected C ABI contract;
- `cmeta_callable` supplies the finite exact execution adapter.

The caller does **not** repeat a `parameter_names[]` array or parameter count.
During compilation TurboSCXML validates that FunctionDesc, ABI and callable
describe the same finite scalar shape. Only IN parameters are admitted by this
profile because XML attributes are input expressions, not output storage.

V1 custom-action rows and `scxml_compile_cmeta_v2()` remain compatibility
surfaces. They do not define the canonical semantic source for new code.

## Compile-time lowering

The V3 provider is adapted only for the duration of compilation:

```text
scxml_cmeta_custom_action_v2
        |
        +--> cmeta_function_desc validation
        +--> cmeta_function_abi_desc validation
        +--> exact callable bind/signature validation
        |
        v
FunctionDesc parameter names/types
        |
        v
XML attribute -> parameter expression programs
        |
        v
immutable Program
  - bound callable
  - compiled argument expressions
  - concrete parameter types
```

The compile-scoped compatibility rows are freed before
`scxml_compile_cmeta_v3()` returns. Runtime execution never walks the
FunctionDesc, ABI descriptor or registration table.

## CFlow projection boundary

Salts 1.8.3 exposes `cflow_function_projection_admit()` for reflected
functions mapped to specific CFlow dataflow operators. That API currently
admits proven operator shapes such as value transforms; an SCXML executable
action is not implicitly a MAP/FILTER/REDUCE operator.

TurboSCXML therefore does **not** invent a fake CFlow operator mapping merely
to claim projection use. FunctionDesc-first semantic admission and exact
callable execution are landed first. A later slice may store a CFlow execution
projection when Salts exposes an action-compatible admitted shape. CFlow
remains independent of XML and SCXML metadata.

## Ownership and lifetime

- FunctionDesc/ABI descriptors and callable code are borrowed from their
  provider through Program lifetime.
- Provider rows and XML-name strings are borrowed only during compilation.
- The Program copies the bound callable and owns compiled argument programs.
- Argument/return scratch is stack-local, bounded and never retained by
  TurboSCXML.
- A module/plugin provider must remain loaded until every Program and active
  Session using its descriptors/callable has quiesced and been destroyed.

## Validation

V3 compilation rejects before Program publication:

- invalid row size, namespace/local-name or duplicate action key;
- invalid FunctionDesc or ABI descriptor;
- ABI/function semantic mismatch;
- unbound/unsupported callable protocol;
- callable/function return or parameter type mismatch;
- unknown, OUT or INOUT parameter direction;
- duplicate/empty reflected parameter names;
- unsupported scalar shape;
- missing, extra, qualified or empty XML argument attributes.

## Verification

Tests cover:

- a successful ordinary FunctionDesc-first custom action without duplicated
  parameter metadata;
- compiled expression evaluation and normal Program execution;
- OUT-direction rejection;
- reflected/callable type mismatch rejection;
- ABI/function mismatch rejection;
- the existing V1/V2 compatibility path and full SCXML regression suite.
