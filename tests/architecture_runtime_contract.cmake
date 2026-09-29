if(NOT DEFINED TURBOSCXML_SOURCE_DIR)
  message(FATAL_ERROR "TURBOSCXML_SOURCE_DIR is required")
endif()

file(READ "${TURBOSCXML_SOURCE_DIR}/src/scxml_runtime.c"
  TURBOSCXML_RUNTIME_SOURCE)
file(READ "${TURBOSCXML_SOURCE_DIR}/src/scxml_program.c"
  TURBOSCXML_PROGRAM_SOURCE)

foreach(TURBOSCXML_FORBIDDEN_RUNTIME_LOOKUP IN ITEMS
    "cmeta_callable_signature("
    "cmeta_function_param("
    "cflow_function_action_projection_admit(")
  string(FIND
    "${TURBOSCXML_RUNTIME_SOURCE}"
    "${TURBOSCXML_FORBIDDEN_RUNTIME_LOOKUP}"
    TURBOSCXML_FORBIDDEN_RUNTIME_LOOKUP_POSITION)
  if(NOT TURBOSCXML_FORBIDDEN_RUNTIME_LOOKUP_POSITION EQUAL -1)
    message(FATAL_ERROR
      "SCXML runtime regained control-plane reflection/admission lookup: "
      "${TURBOSCXML_FORBIDDEN_RUNTIME_LOOKUP}")
  endif()
endforeach()

string(FIND
  "${TURBOSCXML_RUNTIME_SOURCE}"
  "cmeta_callable_invoke("
  TURBOSCXML_RUNTIME_INVOKE_POSITION)
if(TURBOSCXML_RUNTIME_INVOKE_POSITION EQUAL -1)
  message(FATAL_ERROR
    "SCXML runtime no longer invokes the pre-admitted CMeta callable")
endif()

string(FIND
  "${TURBOSCXML_PROGRAM_SOURCE}"
  "cflow_function_action_projection_admit("
  TURBOSCXML_COMPILE_ADMISSION_POSITION)
if(TURBOSCXML_COMPILE_ADMISSION_POSITION EQUAL -1)
  message(FATAL_ERROR
    "SCXML compile path no longer uses CFlow Function action admission")
endif()
