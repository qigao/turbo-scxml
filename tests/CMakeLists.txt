function(turboscxml_add_test target source)
  set(multi_value_args DEFINITIONS INCLUDES LIBS)
  cmake_parse_arguments(TEST "" "" "${multi_value_args}" ${ARGN})

  add_executable(${target} ${source})
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_link_libraries(${target} PRIVATE
    TurboSCXML::SCXML
    TurboUtils::TinyTest
    ${TEST_LIBS})
  if(TEST_INCLUDES)
    target_include_directories(${target} PRIVATE ${TEST_INCLUDES})
  endif()
  if(TEST_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${TEST_DEFINITIONS})
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

turboscxml_add_test(cflow_scxml_test cflow_scxml_test.c
  LIBS TurboUtils::Core
  DEFINITIONS
    CFLOW_SCXML_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

turboscxml_add_test(
  cflow_scxml_event_io_contract_test
  cflow_scxml_event_io_contract_test.c
  LIBS TurboUtils::Core)

turboscxml_add_test(
  cflow_scxml_w3c_conformance_test
  cflow_scxml_w3c_conformance_test.c
  LIBS TurboUtils::Core
  DEFINITIONS
    CFLOW_SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

turboscxml_add_test(
  cflow_scxml_cmeta_expr_test
  cflow_scxml_cmeta_expr_test.c
  LIBS TurboUtils::CMeta
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  cflow_scxml_cmeta_test
  cflow_scxml_cmeta_test.c)

turboscxml_add_test(
  cflow_scxml_cmeta_sequence_test
  cflow_scxml_cmeta_sequence_test.c
  LIBS TurboUtils::STL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  cflow_scxml_cmeta_foreach_test
  cflow_scxml_cmeta_foreach_test.c
  LIBS TurboUtils::STL)

turboscxml_add_test(
  cflow_scxml_cmeta_managed_foreach_test
  cflow_scxml_cmeta_managed_foreach_test.c
  LIBS TurboUtils::STL)
