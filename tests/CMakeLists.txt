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

if(TURBOSCXML_ENABLE_VOICEXML_CMETA)
add_executable(voicexml_cmeta_api_test voicexml/test_voicexml_cmeta_api.c)
target_compile_features(voicexml_cmeta_api_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_api_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_link_libraries(voicexml_cmeta_api_test PRIVATE
  TurboSCXML::VoiceXMLCMeta Salts::TinyTest)
add_test(NAME voicexml_cmeta_api_test COMMAND voicexml_cmeta_api_test)

add_executable(
  voicexml_cmeta_scope_test voicexml/test_voicexml_cmeta_scope.c)
target_compile_features(voicexml_cmeta_scope_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_scope_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_include_directories(voicexml_cmeta_scope_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_cmeta_scope_test PRIVATE
  TurboSCXML::_CMetaRuntime Salts::TinyTest)
add_test(NAME voicexml_cmeta_scope_test COMMAND voicexml_cmeta_scope_test)

add_executable(
  voicexml_cmeta_expr_test voicexml/test_voicexml_cmeta_expr.c)
target_compile_features(voicexml_cmeta_expr_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_expr_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_compile_definitions(voicexml_cmeta_expr_test PRIVATE
  TURBOSCXML_VOICEXML_TESTING=1)
target_include_directories(voicexml_cmeta_expr_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_cmeta_expr_test PRIVATE
  TurboSCXML::VoiceXMLCMeta Salts::TinyTest)
add_test(NAME voicexml_cmeta_expr_test COMMAND voicexml_cmeta_expr_test)

add_executable(
  voicexml_cmeta_program_test voicexml/test_voicexml_cmeta_program.c)
target_compile_features(voicexml_cmeta_program_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_program_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_compile_definitions(voicexml_cmeta_program_test PRIVATE
  TURBOSCXML_VOICEXML_TESTING=1)
target_include_directories(voicexml_cmeta_program_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_cmeta_program_test PRIVATE
  TurboSCXML::VoiceXMLCMeta Salts::TinyTest)
add_test(NAME voicexml_cmeta_program_test COMMAND voicexml_cmeta_program_test)

add_executable(
  voicexml_cmeta_session_test voicexml/test_voicexml_cmeta_session.c)
target_compile_features(voicexml_cmeta_session_test PRIVATE c_std_11)
set_target_properties(voicexml_cmeta_session_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_compile_definitions(voicexml_cmeta_session_test PRIVATE
  TURBOSCXML_VOICEXML_TESTING=1)
target_include_directories(voicexml_cmeta_session_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_cmeta_session_test PRIVATE
  TurboSCXML::VoiceXMLCMeta Salts::TinyTest)
add_test(NAME voicexml_cmeta_session_test COMMAND voicexml_cmeta_session_test)

add_executable(
  voicexml_subdialog_owner_test voicexml_subdialog_owner_test.c)
target_compile_features(voicexml_subdialog_owner_test PRIVATE c_std_11)
set_target_properties(voicexml_subdialog_owner_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_link_libraries(voicexml_subdialog_owner_test PRIVATE
  TurboSCXML::VoiceXMLSubdialogOwner
  Salts::TinyTest)
add_test(NAME voicexml_subdialog_owner_test
  COMMAND voicexml_subdialog_owner_test)
endif()

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

turboscxml_add_test(
  voicexml_document_store_test
  voicexml_document_store_test.c
  LIBS TurboSCXML::VoiceXMLDocumentStore)

turboscxml_add_test(
  voicexml_script_resource_test
  voicexml_script_resource_test.c
  LIBS TurboSCXML::VoiceXMLScriptResource)

turboscxml_add_test(
  voicexml_submit_resource_test
  voicexml_submit_resource_test.c
  LIBS TurboSCXML::VoiceXMLSubmitResource)

add_executable(voicexml_dialog_manager_test
  voicexml_dialog_manager_test.c)
target_compile_features(voicexml_dialog_manager_test PRIVATE c_std_11)
set_target_properties(voicexml_dialog_manager_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_include_directories(voicexml_dialog_manager_test PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(voicexml_dialog_manager_test PRIVATE
  TurboSCXML::VoiceXMLDialogManager
  Salts::TinyTest)
add_test(NAME voicexml_dialog_manager_test
  COMMAND voicexml_dialog_manager_test)

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
      -RealSaltsUtilsRoot "$ENV{SALTS_UTILS_ROOT}"
      -RealCHttpRoot "$ENV{CHTTP_ROOT}"
      -XmlOnlyFixture
        "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer/fixtures/xml_only_salts"
      -XmlOnlySaltsUtilsFixture
        "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer/fixtures/xml_only_salts_utils"
      -CMetaFixture
        "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer/fixtures/cmeta_salts"
      -CMetaSaltsUtilsFixture
        "${CMAKE_CURRENT_SOURCE_DIR}/install_consumer/fixtures/cmeta_salts_utils"
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

if(TURBOSCXML_ENABLE_COMPONENT)
  # Runtime FunctionAbi validation complements, not replaces, C11/C++17
  # native function-pointer constness checks. Only compile static libraries:
  # installed SDK linking is separately exercised by the normal CTests.
  include(CheckCSourceCompiles)
  include(CheckCXXSourceCompiles)
  set(_scxml_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  set(_scxml_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(_scxml_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  # Main target_compile_features(c_std_11) is not inherited by isolated
  # check_*_source_compiles projects. MSVC defaults to its pre-C11 C dialect
  # without an explicit flag, so this compiler-only proof must select the
  # exact production C11 and C++17 language modes itself. No ABI fallback.
  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS
      "${_scxml_saved_required_flags} /std:c11 /experimental:c11atomics")
  endif()
  set(_scxml_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  # try_compile runs in an isolated project: project-local ALIAS targets
  # are not visible there. Compile from the exact installed SDK headers,
  # without linking any placeholder library or bypassing an ABI check.
  set(CMAKE_REQUIRED_LIBRARIES "")
  set(CMAKE_REQUIRED_INCLUDES
    "${PROJECT_SOURCE_DIR}/include"
    "${SALTS_ROOT_PATH}/include"
    "${SALTS_UTILS_ROOT_PATH}/include")
  # The public SCXML umbrella includes generated DataBind/XML Parser headers
  # with target-specific, installed subdirectory include roots. An isolated
  # try_compile does NOT inherit main-graph target include paths, so construct
  # its compile-only include set from each actual imported SDK target.
  foreach(_sdk_target IN ITEMS
      Salts::CMeta Salts::CFlow Salts::CSerde
      Salts::DataBind Salts::XmlParser Salts::Core)
    get_target_property(_sdk_public_includes
      ${_sdk_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_sdk_public_includes)
      foreach(_sdk_include IN LISTS _sdk_public_includes)
        if(NOT _sdk_include MATCHES "^\\$<")
          list(APPEND CMAKE_REQUIRED_INCLUDES "${_sdk_include}")
        endif()
      endforeach()
    endif()
  endforeach()
  list(REMOVE_DUPLICATES CMAKE_REQUIRED_INCLUDES)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(_scxml_intercept_good [=[
#include <cmeta/ace_interceptor.h>
/* Same public named C tags, but no need to pull the entire SCXML/IDL include
   graph into this strictly native function-pointer type-system probe. */
typedef struct scxml_send_request scxml_send_request;
typedef struct cflow_statechart_effect_ticket cflow_statechart_effect_ticket;
CMETA_INTERCEPTOR_TYPE(scxml_compile_intercept, scxml_send_request,
                       cflow_statechart_effect_ticket);
cmeta_status exact(void *, const scxml_send_request *,
                   cflow_statechart_effect_ticket *);
CMETA_STATIC_ASSERT(
    CMETA_TYPE_MATCHES(&exact, scxml_compile_intercept_target_fn),
    "exact native callback signature must compile");
int main(void) { return 0; }
]=])
  set(_scxml_intercept_bad [=[
#include <cmeta/ace_interceptor.h>
/* Same public named C tags, but no need to pull the entire SCXML/IDL include
   graph into this strictly native function-pointer type-system probe. */
typedef struct scxml_send_request scxml_send_request;
typedef struct cflow_statechart_effect_ticket cflow_statechart_effect_ticket;
CMETA_INTERCEPTOR_TYPE(scxml_compile_intercept, scxml_send_request,
                       cflow_statechart_effect_ticket);
cmeta_status mutable_request(void *, scxml_send_request *,
                             cflow_statechart_effect_ticket *);
CMETA_STATIC_ASSERT(
    CMETA_TYPE_MATCHES(&mutable_request, scxml_compile_intercept_target_fn),
    "mutable request is not a borrowed const request");
int main(void) { return 0; }
]=])
  foreach(_check IN ITEMS
      SCXML_INTERCEPT_EXACT_C11 SCXML_INTERCEPT_MUTABLE_C11
      SCXML_INTERCEPT_EXACT_CPP17 SCXML_INTERCEPT_MUTABLE_CPP17)
    unset("${_check}" CACHE)
  endforeach()
  check_c_source_compiles(
      "${_scxml_intercept_good}" SCXML_INTERCEPT_EXACT_C11)
  check_c_source_compiles(
      "${_scxml_intercept_bad}" SCXML_INTERCEPT_MUTABLE_C11)
  if(MSVC)
    # Salts::CMeta exports /Zc:preprocessor for MSVC C++; isolated
    # try_compile deliberately has no link target and must carry the exact
    # same public compiler option, otherwise Enum(...) PP macros are parsed
    # by MSVC's traditional (nonconforming) preprocessor.
    set(CMAKE_REQUIRED_FLAGS
      "${_scxml_saved_required_flags} /std:c++17 /Zc:preprocessor")
  endif()
  check_cxx_source_compiles(
      "${_scxml_intercept_good}" SCXML_INTERCEPT_EXACT_CPP17)
  check_cxx_source_compiles(
      "${_scxml_intercept_bad}" SCXML_INTERCEPT_MUTABLE_CPP17)
  if(NOT SCXML_INTERCEPT_EXACT_C11 OR SCXML_INTERCEPT_MUTABLE_C11 OR
     NOT SCXML_INTERCEPT_EXACT_CPP17 OR SCXML_INTERCEPT_MUTABLE_CPP17)
    # Keep the full compiler's own message attached to each individual
    # try_compile probe. A generic regex over CMakeConfigureLog.yaml can
    # truncate Windows path diagnostics at semicolons or mix unrelated probes.
    set(_scxml_probe_log "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeConfigureLog.yaml")
    if(EXISTS "${_scxml_probe_log}")
      file(READ "${_scxml_probe_log}" _scxml_probe_text)
      foreach(_scxml_probe IN ITEMS
          SCXML_INTERCEPT_EXACT_C11 SCXML_INTERCEPT_EXACT_CPP17)
        string(FIND "${_scxml_probe_text}" "${_scxml_probe}" _scxml_probe_at)
        if(_scxml_probe_at GREATER_EQUAL 0)
          string(LENGTH "${_scxml_probe_text}" _scxml_log_size)
          math(EXPR _scxml_excerpt_len
            "${_scxml_log_size} - ${_scxml_probe_at}")
          if(_scxml_excerpt_len GREATER 5000)
            set(_scxml_excerpt_len 5000)
          endif()
          string(SUBSTRING "${_scxml_probe_text}"
            ${_scxml_probe_at} ${_scxml_excerpt_len} _scxml_probe_excerpt)
          message(WARNING
            "SCXML typed Interceptor positive ${_scxml_probe} diagnostic:\n${_scxml_probe_excerpt}")
        endif()
      endforeach()
    endif()
    message(FATAL_ERROR
      "SCXML ACE typed Interceptor C11/C++17 native ABI gate failed")
  endif()
  set(CMAKE_REQUIRED_LIBRARIES "${_scxml_saved_required_libraries}")
  set(CMAKE_REQUIRED_INCLUDES "${_scxml_saved_required_includes}")
  set(CMAKE_REQUIRED_FLAGS "${_scxml_saved_required_flags}")
  if(_scxml_saved_try_type)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_scxml_saved_try_type}")
  else()
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  endif()

  foreach(generation IN ITEMS 1 2)
    add_library(scxml_component_dso_${generation} MODULE
      scxml_component_dso_fixture.c)
    target_link_libraries(scxml_component_dso_${generation} PRIVATE
      TurboSCXML::SCXML Salts::ComponentPluginABI Salts::PluginABI)
    target_compile_features(scxml_component_dso_${generation} PRIVATE c_std_11)
    target_compile_definitions(scxml_component_dso_${generation} PRIVATE
      SCXML_COMPONENT_DSO_MARKER=${generation}00
      SCXML_COMPONENT_DSO_ID="test.scxml.component.${generation}")
    set_target_properties(scxml_component_dso_${generation} PROPERTIES
      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
      FOLDER "tests/fixtures")
  endforeach()
  turboscxml_add_test(scxml_component_dso_test scxml_component_dso_test.c
    LIBS TurboSCXML::Component
    DEFINITIONS
      SCXML_COMPONENT_DSO_ONE="$<TARGET_FILE:scxml_component_dso_1>"
      SCXML_COMPONENT_DSO_TWO="$<TARGET_FILE:scxml_component_dso_2>")
  add_dependencies(scxml_component_dso_test
    scxml_component_dso_1 scxml_component_dso_2)
  set_tests_properties(scxml_component_dso_test PROPERTIES TIMEOUT 60)

  # Explicitly opt in to the microbenchmark via its CTest name. It is built
  # with the normal full CMake graph but not repeated in the sanitizer/TSan
  # or cross-RID conformance regex: timing is not a functional gate.
  add_executable(
    scxml_component_interceptor_benchmark
    scxml_component_interceptor_benchmark.c)
  target_compile_features(scxml_component_interceptor_benchmark PRIVATE c_std_11)
  set_target_properties(scxml_component_interceptor_benchmark PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_link_libraries(scxml_component_interceptor_benchmark PRIVATE
    TurboSCXML::Component Salts::Platform)
  target_compile_definitions(scxml_component_interceptor_benchmark PRIVATE
    SCXML_COMPONENT_DSO_ONE="$<TARGET_FILE:scxml_component_dso_1>")
  add_dependencies(scxml_component_interceptor_benchmark
    scxml_component_dso_1)
  add_test(NAME scxml_component_interceptor_benchmark
    COMMAND scxml_component_interceptor_benchmark)
  set_tests_properties(scxml_component_interceptor_benchmark PROPERTIES
    LABELS "microbenchmark" TIMEOUT 60)

  turboscxml_add_test(
    scxml_component_test
    scxml_component_test.c
    LIBS TurboSCXML::Component)
  turboscxml_add_test(
    voicexml_component_dialog_test
    voicexml_component_dialog_test.c
    LIBS TurboSCXML::Component TurboSCXML::VoiceXMLDialogManager)
  set_tests_properties(voicexml_component_dialog_test PROPERTIES TIMEOUT 60)
endif()

if(TURBOSCXML_ENABLE_HOST)
  turboscxml_add_test(
    scxml_host_router_test
    scxml_host_router_test.c
    LIBS TurboSCXML::Host Salts::Core)
  set_tests_properties(scxml_host_router_test PROPERTIES TIMEOUT 30)
  turboscxml_add_test(
    scxml_host_event_io_test
    scxml_host_event_io_test.c
    LIBS TurboSCXML::Host Salts::Core)
  set_tests_properties(scxml_host_event_io_test PROPERTIES TIMEOUT 30)
  turboscxml_add_test(
    scxml_host_delay_test
    scxml_host_delay_test.c
    LIBS TurboSCXML::Host Salts::Core)
  set_tests_properties(scxml_host_delay_test PROPERTIES TIMEOUT 30)
endif()

if(TURBOSCXML_ENABLE_CNET_EVENT_IO)
  turboscxml_add_test(
    scxml_cnet_ingress_test
    scxml_cnet_ingress_test.c
    LIBS TurboSCXML::CNetEventIO Salts::Core)
  set_tests_properties(scxml_cnet_ingress_test PROPERTIES TIMEOUT 40)
  turboscxml_add_test(
    scxml_cnet_egress_test
    scxml_cnet_egress_test.c
    LIBS TurboSCXML::CNetEventIO Salts::Core)
  set_tests_properties(scxml_cnet_egress_test PROPERTIES TIMEOUT 40)
  turboscxml_add_test(
    scxml_cnet_frame_ingress_test
    scxml_cnet_frame_ingress_test.c
    LIBS TurboSCXML::CNetEventIO Salts::Core)
  set_tests_properties(scxml_cnet_frame_ingress_test PROPERTIES TIMEOUT 30)
  turboscxml_add_test(
    scxml_cnet_domain_fence_test
    scxml_cnet_domain_fence_test.c
    LIBS TurboSCXML::CNetEventIO Salts::Core)
  set_tests_properties(scxml_cnet_domain_fence_test PROPERTIES TIMEOUT 40)
  turboscxml_add_test(
    scxml_cnet_request_generation_test
    scxml_cnet_request_generation_test.c
    LIBS TurboSCXML::CNetEventIO Salts::NativeIO Salts::Core)
  set_tests_properties(scxml_cnet_request_generation_test
    PROPERTIES TIMEOUT 40)
  if(WIN32)
    target_link_libraries(scxml_cnet_request_generation_test PRIVATE ws2_32)
  endif()
endif()

if(TURBOSCXML_ENABLE_COMPONENT AND TURBOSCXML_ENABLE_CNET_EVENT_IO)
  foreach(generation IN ITEMS 1 2)
    add_library(scxml_invoke_component_dso_${generation} MODULE
      scxml_component_invoke_dso_fixture.c)
    target_link_libraries(scxml_invoke_component_dso_${generation} PRIVATE
      TurboSCXML::SCXML Salts::ComponentPluginABI Salts::PluginABI Salts::CNet)
    target_compile_features(scxml_invoke_component_dso_${generation} PRIVATE c_std_11)
    target_compile_definitions(scxml_invoke_component_dso_${generation} PRIVATE
      SCXML_COMPONENT_DSO_MARKER=${generation}00
      SCXML_INVOKE_DSO_ID="test.scxml.invoke.component.${generation}")
    set_target_properties(scxml_invoke_component_dso_${generation} PROPERTIES
      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
      FOLDER "tests/fixtures")
  endforeach()
  turboscxml_add_test(
    scxml_component_cnet_invoke_test
    scxml_component_cnet_invoke_test.c
    LIBS TurboSCXML::Component TurboSCXML::CNetEventIO Salts::NativeIO Salts::Core
    DEFINITIONS
      SCXML_INVOKE_DSO_ONE="$<TARGET_FILE:scxml_invoke_component_dso_1>"
      SCXML_INVOKE_DSO_TWO="$<TARGET_FILE:scxml_invoke_component_dso_2>")
  add_dependencies(scxml_component_cnet_invoke_test
    scxml_invoke_component_dso_1 scxml_invoke_component_dso_2)
  if(TURBOSCXML_ENABLE_HOST)
    target_link_libraries(scxml_component_cnet_invoke_test PRIVATE
      TurboSCXML::Host)
    target_compile_definitions(scxml_component_cnet_invoke_test PRIVATE
      SCXML_TEST_COMPONENT_HOST_DELAY=1)
  endif()
  if(WIN32)
    target_link_libraries(scxml_component_cnet_invoke_test PRIVATE ws2_32)
  endif()
  set_tests_properties(scxml_component_cnet_invoke_test PROPERTIES TIMEOUT 60)
endif()

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
  scxml_provider_test
  scxml_provider_test.c)

add_test(
  NAME scxml_architecture_runtime_contract
  COMMAND "${CMAKE_COMMAND}"
    -DTURBOSCXML_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture_runtime_contract.cmake")

if(TURBOSCXML_ENABLE_PLUGIN)
  add_library(scxml_plugin_fixture MODULE
    scxml_plugin_fixture.c)
  target_compile_features(scxml_plugin_fixture PRIVATE c_std_11)
  set_target_properties(scxml_plugin_fixture PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_link_libraries(scxml_plugin_fixture PRIVATE
    TurboSCXML::SCXML
    Salts::PluginABI)

  add_executable(scxml_plugin_test
    scxml_plugin_test.c)
  target_compile_features(scxml_plugin_test PRIVATE c_std_11)
  set_target_properties(scxml_plugin_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_link_libraries(scxml_plugin_test PRIVATE
    TurboSCXML::Plugin
    Salts::TinyTest)
  add_dependencies(scxml_plugin_test scxml_plugin_fixture)
  add_test(
    NAME scxml_plugin_test
    COMMAND scxml_plugin_test)
  set_tests_properties(scxml_plugin_test PROPERTIES
    ENVIRONMENT
      "SCXML_PLUGIN_FIXTURE_PATH=$<TARGET_FILE:scxml_plugin_fixture>")
endif()

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
  LIBS TurboSCXML::CCXML TurboSCXML::VoiceXMLDialogManager
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

if(TURBOSCXML_ENABLE_QUICKJS)
  add_executable(
    voicexml_quickjs_test
    voicexml_quickjs_test.c)
  target_compile_features(voicexml_quickjs_test PRIVATE c_std_11)
  set_target_properties(voicexml_quickjs_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_include_directories(voicexml_quickjs_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(voicexml_quickjs_test PRIVATE
    TurboSCXML::VoiceXMLQuickJS
    TurboSCXML::VoiceXMLScriptResource
    Salts::TinyTest)
  add_test(NAME voicexml_quickjs_test COMMAND voicexml_quickjs_test)
endif()
set_tests_properties(scxml_quickjs_test PROPERTIES
  TIMEOUT 30)

if(TURBOSCXML_ENABLE_QUICKJS)
  add_executable(
    quickjs_sandbox_test
    quickjs_sandbox_test.c)
  target_compile_features(quickjs_sandbox_test PRIVATE c_std_11)
  set_target_properties(quickjs_sandbox_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_include_directories(quickjs_sandbox_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(quickjs_sandbox_test PRIVATE
    TurboSCXML::_QuickJSSandbox
    Salts::TinyTest)
  add_test(NAME quickjs_sandbox_test COMMAND quickjs_sandbox_test)

  add_executable(
    quickjs_cmeta_bridge_test
    quickjs_cmeta_bridge_test.c)
  target_compile_features(quickjs_cmeta_bridge_test PRIVATE c_std_11)
  set_target_properties(quickjs_cmeta_bridge_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  target_include_directories(quickjs_cmeta_bridge_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(quickjs_cmeta_bridge_test PRIVATE
    TurboSCXML::_QuickJSCMetaBridge
    TurboSCXML::_QuickJSSandbox
    Salts::TinyTest)
  add_test(NAME quickjs_cmeta_bridge_test COMMAND quickjs_cmeta_bridge_test)
endif()

if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
  turboscxml_add_test(
    scxml_chttp_resource_test
    scxml_chttp_resource_test.c
    LIBS TurboSCXML::CHttpResource CHttp::Server
    INCLUDES ${PROJECT_SOURCE_DIR}/src)

  turboscxml_add_test(
    voicexml_chttp_resource_test
    voicexml_chttp_resource_test.c
    LIBS TurboSCXML::VoiceXMLCHttpResource
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
