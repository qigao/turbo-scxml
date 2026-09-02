cmake_minimum_required(VERSION 3.20)

project(TurboSCXML VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TURBOSCXML_ENABLE_QUICKJS
  "Enable the bounded quickjs-sandbox SCXML data-model profile" OFF)
option(TURBOSCXML_ENABLE_CHTTP_RESOURCE
  "Enable the optional blocking CHTTP SCXML resource adapter" OFF)
option(TURBOSCXML_ENABLE_CHTTP_EVENT_IO
  "Build the optional CHTTP BasicHTTP Event I/O processor" OFF)
option(ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)

if(ENABLE_SANITIZER_ADDRESS)
  message(STATUS "AddressSanitizer: ON")
  if(MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    add_compile_options(
      -fsanitize=address
      -fno-omit-frame-pointer
      -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address)
  endif()
endif()

if(NOT DEFINED ENV{ROCIDA_ROOT}
   OR "$ENV{ROCIDA_ROOT}" STREQUAL "")
  message(FATAL_ERROR
    "ROCIDA_ROOT must name the installed Rocida profile")
endif()
file(TO_CMAKE_PATH "$ENV{ROCIDA_ROOT}" ROCIDA_ROOT_PATH)
if(NOT IS_DIRECTORY "${ROCIDA_ROOT_PATH}")
  message(FATAL_ERROR
    "ROCIDA_ROOT is not a directory: $ENV{ROCIDA_ROOT}")
endif()

unset(Rocida_DIR CACHE)
unset(Rocida_DIR)
find_package(Rocida 1.1 CONFIG REQUIRED
  PATHS "${ROCIDA_ROOT_PATH}"
  NO_DEFAULT_PATH)

foreach(required_target IN ITEMS
    Rocida::CFlow
    Rocida::CMeta
    Rocida::CSerde
    Rocida::CBind
    Rocida::XmlParser
    Rocida::Core
    Rocida::QueryVM)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "Rocida SDK does not provide required target: ${required_target}")
  endif()
endforeach()

add_library(turbo_scxml STATIC
  src/scxml_ast.c
  src/scxml_analyze.c
  src/scxml_runtime.c
  src/scxml_emit.c
  src/scxml_program.c
  src/scxml_session.c
  src/scxml_expr.c
  src/scxml_assign.c
  src/scxml_location.c
  src/scxml_sequence.c
  src/scxml_scope.c
  src/scxml_foreach.c
  src/scxml_quickjs.c)
add_library(TurboSCXML::SCXML ALIAS turbo_scxml)

set_target_properties(turbo_scxml PROPERTIES
  EXPORT_NAME SCXML
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
target_compile_features(turbo_scxml PUBLIC c_std_11)
target_compile_options(turbo_scxml
  PUBLIC
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:/experimental:c11atomics>)
target_include_directories(turbo_scxml
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(turbo_scxml
  PUBLIC
    Rocida::CFlow
    Rocida::CMeta
    Rocida::CSerde
    Rocida::XmlParser
  PRIVATE
    Rocida::Core
    Rocida::CBind
    Rocida::QueryVM)
target_compile_definitions(turbo_scxml
  PUBLIC TURBOSCXML_HAS_QUICKJS=$<BOOL:${TURBOSCXML_ENABLE_QUICKJS}>)
if(TURBOSCXML_ENABLE_QUICKJS)
  find_package(qjs CONFIG REQUIRED)
  target_link_libraries(turbo_scxml PRIVATE qjs)
endif()

add_library(turbo_ccxml STATIC
  src/ccxml_program.c)
add_library(TurboSCXML::CCXML ALIAS turbo_ccxml)
set_target_properties(turbo_ccxml PROPERTIES
  EXPORT_NAME CCXML
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})
target_compile_features(turbo_ccxml PUBLIC c_std_11)
target_compile_options(turbo_ccxml
  PUBLIC
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:/experimental:c11atomics>)
target_include_directories(turbo_ccxml
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(turbo_ccxml
  PUBLIC TurboSCXML::SCXML
  PRIVATE Rocida::XmlParser)

if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
  if(NOT TARGET Rocida::CHTTP)
    message(FATAL_ERROR
      "TURBOSCXML_ENABLE_CHTTP_RESOURCE requires Rocida::CHTTP")
  endif()
  add_library(turbo_scxml_chttp_resource STATIC
    src/scxml_chttp_resource.c)
  add_library(TurboSCXML::CHttpResource ALIAS turbo_scxml_chttp_resource)
  set_target_properties(turbo_scxml_chttp_resource PROPERTIES
    EXPORT_NAME CHttpResource
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  target_compile_features(turbo_scxml_chttp_resource PUBLIC c_std_11)
  target_compile_options(turbo_scxml_chttp_resource
    PUBLIC
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/experimental:c11atomics>)
  target_include_directories(turbo_scxml_chttp_resource
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(turbo_scxml_chttp_resource
    PUBLIC
      TurboSCXML::SCXML
      Rocida::CHTTP
    PRIVATE
      Rocida::Core)
endif()

if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  if(NOT TARGET Rocida::CHTTP)
    message(FATAL_ERROR
      "TURBOSCXML_ENABLE_CHTTP_EVENT_IO requires Rocida::CHTTP")
  endif()
  add_library(turbo_scxml_chttp_event_io STATIC
    src/chttp_event_io_codec.c
    src/chttp_event_io_egress.c
    src/chttp_event_io_ingress.c
    src/chttp_event_io.c)
  add_library(TurboSCXML::CHttpEventIO ALIAS turbo_scxml_chttp_event_io)
  set_target_properties(turbo_scxml_chttp_event_io PROPERTIES
    EXPORT_NAME CHttpEventIO
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  target_compile_features(turbo_scxml_chttp_event_io PUBLIC c_std_11)
  target_include_directories(turbo_scxml_chttp_event_io
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(turbo_scxml_chttp_event_io
    PUBLIC TurboSCXML::SCXML Rocida::CHTTP
    PRIVATE Rocida::Core)
endif()

if(BUILD_TESTING)
  get_target_property(
    CFLOW_PUBLIC_LINKS Rocida::CFlow INTERFACE_LINK_LIBRARIES)
  if(CFLOW_PUBLIC_LINKS MATCHES "XmlParser|cxml|CSerde|SCXML")
    message(FATAL_ERROR
      "Rocida::CFlow must remain independent of XML, CSerde, and SCXML")
  endif()

  get_target_property(
    TURBOSCXML_PUBLIC_LINKS turbo_scxml INTERFACE_LINK_LIBRARIES)
  set(TURBOSCXML_EXPECTED_LINKS
    "Rocida::CFlow;Rocida::CMeta;Rocida::CSerde;Rocida::XmlParser;$<LINK_ONLY:Rocida::Core>;$<LINK_ONLY:Rocida::CBind>;$<LINK_ONLY:Rocida::QueryVM>")
  if(TURBOSCXML_ENABLE_QUICKJS)
    string(APPEND TURBOSCXML_EXPECTED_LINKS ";$<LINK_ONLY:qjs>")
  endif()
  if(NOT TURBOSCXML_PUBLIC_LINKS STREQUAL TURBOSCXML_EXPECTED_LINKS)
    message(FATAL_ERROR
      "TurboSCXML::SCXML dependency contract changed")
  endif()

  if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
    get_target_property(
      TURBOSCXML_CHTTP_PUBLIC_LINKS
      turbo_scxml_chttp_resource INTERFACE_LINK_LIBRARIES)
    set(TURBOSCXML_CHTTP_EXPECTED_LINKS
      "TurboSCXML::SCXML;Rocida::CHTTP;$<LINK_ONLY:Rocida::Core>")
    if(NOT TURBOSCXML_CHTTP_PUBLIC_LINKS STREQUAL
       TURBOSCXML_CHTTP_EXPECTED_LINKS)
      message(FATAL_ERROR
        "TurboSCXML::CHttpResource dependency contract changed")
    endif()
  endif()

  if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
    get_target_property(
      TURBOSCXML_CHTTP_EVENT_IO_PUBLIC_LINKS
      turbo_scxml_chttp_event_io INTERFACE_LINK_LIBRARIES)
    set(TURBOSCXML_CHTTP_EVENT_IO_EXPECTED_LINKS
      "TurboSCXML::SCXML;Rocida::CHTTP;$<LINK_ONLY:Rocida::Core>")
    if(NOT TURBOSCXML_CHTTP_EVENT_IO_PUBLIC_LINKS STREQUAL
       TURBOSCXML_CHTTP_EVENT_IO_EXPECTED_LINKS)
      message(FATAL_ERROR
        "TurboSCXML::CHttpEventIO dependency contract changed")
    endif()
  endif()

  if(NOT TARGET Rocida::TinyTest)
    message(FATAL_ERROR
      "BUILD_TESTING requires Rocida::TinyTest in the selected SDK")
  endif()
  add_subdirectory(tests)
endif()

install(TARGETS turbo_scxml
  EXPORT TurboSCXMLTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
  install(TARGETS turbo_scxml_chttp_resource
    EXPORT TurboSCXMLTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  install(TARGETS turbo_scxml_chttp_event_io
    EXPORT TurboSCXMLTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(
  FILES include/scxml/scxml.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scxml)
if(TURBOSCXML_ENABLE_CHTTP_RESOURCE)
  install(
    FILES include/scxml/chttp_resource.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scxml)
endif()
if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  install(
    FILES include/scxml/chttp_event_io.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scxml)
endif()

set(TURBOSCXML_PACKAGE_HAS_CHTTP_RESOURCE
  ${TURBOSCXML_ENABLE_CHTTP_RESOURCE})
set(TURBOSCXML_PACKAGE_HAS_QUICKJS
  ${TURBOSCXML_ENABLE_QUICKJS})
set(TURBOSCXML_PACKAGE_HAS_CHTTP_EVENT_IO
  ${TURBOSCXML_ENABLE_CHTTP_EVENT_IO})
configure_package_config_file(
  cmake/TurboSCXMLConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/TurboSCXMLConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboSCXML)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/TurboSCXMLConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(EXPORT TurboSCXMLTargets
  FILE TurboSCXMLTargets.cmake
  NAMESPACE TurboSCXML::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboSCXML)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/TurboSCXMLConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/TurboSCXMLConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboSCXML)
