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
    Rocida::TinyTest
    ${TEST_LIBS})
  if(TEST_INCLUDES)
    target_include_directories(${target} PRIVATE ${TEST_INCLUDES})
  endif()
  if(TEST_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${TEST_DEFINITIONS})
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

turboscxml_add_test(scxml_test scxml_test.c
  LIBS Rocida::Core
  DEFINITIONS
    SCXML_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

turboscxml_add_test(
  scxml_ast_test
  scxml_ast_test.c
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_event_io_contract_test
  scxml_event_io_contract_test.c
  LIBS Rocida::Core
  DEFINITIONS
    SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

turboscxml_add_test(
  scxml_invoke_child_materialization_test
  scxml_invoke_child_materialization_test.c
  DEFINITIONS
    SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

turboscxml_add_test(
  scxml_w3c_conformance_test
  scxml_w3c_conformance_test.c
  LIBS Rocida::Core Rocida::STL
  DEFINITIONS
    SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

turboscxml_add_test(
  scxml_expr_test
  scxml_expr_test.c
  LIBS Rocida::CMeta
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_cmeta_test
  scxml_cmeta_test.c)

turboscxml_add_test(
  scxml_sequence_test
  scxml_sequence_test.c
  LIBS Rocida::STL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_foreach_test
  scxml_foreach_test.c
  LIBS Rocida::STL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_managed_foreach_test
  scxml_managed_foreach_test.c
  LIBS Rocida::STL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  ccxml_program_test
  ccxml_program_test.c
  LIBS TurboSCXML::CCXML
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  ccxml_session_test
  ccxml_session_test.c
  LIBS TurboSCXML::CCXML
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_quickjs_test
  scxml_quickjs_test.c
  LIBS Rocida::STL
  INCLUDES ${PROJECT_SOURCE_DIR}/src
  DEFINITIONS
    SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
  turboscxml_add_test(
    scxml_chttp_resource_test
    scxml_chttp_resource_test.c
    LIBS TurboSCXML::CHttpResource
    INCLUDES ${PROJECT_SOURCE_DIR}/src)
endif()

if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  target_link_libraries(scxml_w3c_conformance_test PRIVATE
    TurboSCXML::CHttpEventIO)
  target_compile_definitions(scxml_w3c_conformance_test PRIVATE
    TURBOSCXML_TEST_CHTTP_EVENT_IO=1)
  turboscxml_add_test(
    scxml_chttp_codec_test
    scxml_chttp_codec_test.c
    LIBS TurboSCXML::CHttpEventIO
    INCLUDES ${PROJECT_SOURCE_DIR}/src)
  turboscxml_add_test(
    scxml_chttp_lifecycle_test
    scxml_chttp_lifecycle_test.c
    LIBS TurboSCXML::CHttpEventIO
    INCLUDES ${PROJECT_SOURCE_DIR}/src)
  turboscxml_add_test(
    scxml_chttp_egress_test
    scxml_chttp_egress_test.c
    LIBS TurboSCXML::CHttpEventIO
    INCLUDES ${PROJECT_SOURCE_DIR}/src)
  turboscxml_add_test(
    scxml_chttp_ingress_test
    scxml_chttp_ingress_test.c
    LIBS TurboSCXML::CHttpEventIO
    INCLUDES ${PROJECT_SOURCE_DIR}/src)
endif()
