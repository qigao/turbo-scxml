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
    Salts::TinyTest
    ${TEST_LIBS})
  if(TEST_INCLUDES)
    target_include_directories(${target} PRIVATE ${TEST_INCLUDES})
  endif()
  if(TEST_DEFINITIONS)
    target_compile_definitions(${target} PRIVATE ${TEST_DEFINITIONS})
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(voicexml_api_test voicexml/test_voicexml_api.c)
target_compile_features(voicexml_api_test PRIVATE c_std_11)
set_target_properties(voicexml_api_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_link_libraries(voicexml_api_test PRIVATE
  TurboSCXML::VoiceXML Salts::TinyTest)
add_test(NAME voicexml_api_test COMMAND voicexml_api_test)

add_executable(voicexml_cmeta_api_test voicexml/test_voicexml_cmeta_api.c)
target_compile_features(voicexml_cmeta_api_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_api_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_link_libraries(voicexml_cmeta_api_test PRIVATE
  TurboSCXML::VoiceXMLCMeta Salts::TinyTest)
add_test(NAME voicexml_cmeta_api_test COMMAND voicexml_cmeta_api_test)

add_executable(voicexml_program_test voicexml/test_voicexml_program.c)
target_compile_features(voicexml_program_test PRIVATE c_std_11)
set_target_properties(voicexml_program_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_include_directories(voicexml_program_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_program_test PRIVATE
  TurboSCXML::VoiceXML Salts::TinyTest)
add_test(NAME voicexml_program_test COMMAND voicexml_program_test)

add_executable(voicexml_session_test voicexml/test_voicexml_session.c)
target_compile_features(voicexml_session_test PRIVATE c_std_11)
set_target_properties(voicexml_session_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_include_directories(voicexml_session_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_session_test PRIVATE
  TurboSCXML::VoiceXML Salts::TinyTest)
add_test(NAME voicexml_session_test COMMAND voicexml_session_test)

add_executable(
  voicexml_allocator_test voicexml/test_voicexml_allocator.c)
target_compile_features(voicexml_allocator_test PRIVATE c_std_11)
set_target_properties(voicexml_allocator_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_compile_definitions(voicexml_allocator_test PRIVATE
  TURBOSCXML_VOICEXML_TESTING=1)
target_include_directories(voicexml_allocator_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_allocator_test PRIVATE
  TurboSCXML::VoiceXML Salts::TinyTest)
add_test(NAME voicexml_allocator_test COMMAND voicexml_allocator_test)

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release" AND
   NOT TURBOSCXML_ENABLE_QUICKJS AND
   NOT TURBOSCXML_ENABLE_CHTTP_RESOURCE AND
   NOT TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  find_program(TURBOSCXML_POWERSHELL_EXECUTABLE
    NAMES pwsh powershell REQUIRED)
  add_test(NAME voicexml_package_isolation_test
    COMMAND "${TURBOSCXML_POWERSHELL_EXECUTABLE}"
      -NoProfile
      -ExecutionPolicy Bypass
      -File
        "${CMAKE_CURRENT_SOURCE_DIR}/package/test_voicexml_package_isolation.ps1"
      -SourceDir "${PROJECT_SOURCE_DIR}"
      -ArtifactRoot
        "${CMAKE_BINARY_DIR}/voicexml-package-isolation"
      -InstallRoot
        "${PROJECT_SOURCE_DIR}/.superpowers/sdd/2026-09-06-voicexml-nonmedia-core-mvp/final-fix-package-installs"
      -RealSaltsRoot "$ENV{SALTS_ROOT}"
      -XmlOnlyFixture
        "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer/fixtures/xml_only_salts"
      -VcpkgPrefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
      -CMakeCommand "${CMAKE_COMMAND}")
  set_tests_properties(voicexml_package_isolation_test PROPERTIES
    LABELS "package;voicexml"
    RUN_SERIAL TRUE
    TIMEOUT 1800)
endif()

turboscxml_add_test(scxml_test scxml_test.c
  LIBS Salts::Core
  DEFINITIONS
    SCXML_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

turboscxml_add_test(
  scxml_ast_test
  scxml_ast_test.c
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_event_io_contract_test
  scxml_event_io_contract_test.c
  LIBS Salts::Core
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
  LIBS Salts::Core Salts::CSTL
  DEFINITIONS
    SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

turboscxml_add_test(
  scxml_expr_test
  scxml_expr_test.c
  LIBS Salts::CMeta
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_cmeta_test
  scxml_cmeta_test.c)

turboscxml_add_test(
  scxml_sequence_test
  scxml_sequence_test.c
  LIBS Salts::CSTL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_foreach_test
  scxml_foreach_test.c
  LIBS Salts::CSTL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_managed_foreach_test
  scxml_managed_foreach_test.c
  LIBS Salts::CSTL
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
  ccxml_send_test
  ccxml_send_test.c
  LIBS TurboSCXML::CCXML
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  ccxml_cmeta_test
  ccxml_cmeta_test.c
  LIBS TurboSCXML::CCXML Salts::CSTL
  INCLUDES ${PROJECT_SOURCE_DIR}/src)

turboscxml_add_test(
  scxml_quickjs_test
  scxml_quickjs_test.c
  LIBS Salts::CSTL
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
