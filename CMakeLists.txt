cmake_minimum_required(VERSION 3.20)

project(TurboSCXML VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT DEFINED ENV{TURBOUTILS_ROOT}
   OR "$ENV{TURBOUTILS_ROOT}" STREQUAL "")
  message(FATAL_ERROR
    "TURBOUTILS_ROOT must name the installed TurboUtils profile")
endif()
file(TO_CMAKE_PATH "$ENV{TURBOUTILS_ROOT}" TURBOUTILS_ROOT_PATH)
if(NOT IS_DIRECTORY "${TURBOUTILS_ROOT_PATH}")
  message(FATAL_ERROR
    "TURBOUTILS_ROOT is not a directory: $ENV{TURBOUTILS_ROOT}")
endif()

unset(TurboUtils_DIR CACHE)
unset(TurboUtils_DIR)
find_package(TurboUtils CONFIG REQUIRED
  PATHS "${TURBOUTILS_ROOT_PATH}"
  NO_DEFAULT_PATH)

foreach(required_target IN ITEMS
    TurboUtils::CFlow
    TurboUtils::CMeta
    TurboUtils::XmlParser
    TurboUtils::Core
    TurboUtils::QueryVM)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TurboUtils SDK does not provide required target: ${required_target}")
  endif()
endforeach()

add_library(turbo_scxml STATIC
  src/scxml_analyze.c
  src/scxml_runtime.c
  src/scxml_emit.c
  src/scxml_program.c
  src/scxml_session.c
  src/scxml_expr.c
  src/scxml_assign.c
  src/scxml_location.c
  src/scxml_sequence.c
  src/scxml_foreach.c)
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
    TurboUtils::CFlow
    TurboUtils::CMeta
    TurboUtils::XmlParser
  PRIVATE
    TurboUtils::Core
    TurboUtils::QueryVM)

if(BUILD_TESTING)
  get_target_property(
    CFLOW_PUBLIC_LINKS TurboUtils::CFlow INTERFACE_LINK_LIBRARIES)
  if(CFLOW_PUBLIC_LINKS MATCHES "XmlParser|cxml|CSerde|SCXML")
    message(FATAL_ERROR
      "TurboUtils::CFlow must remain independent of XML, CSerde, and SCXML")
  endif()

  get_target_property(
    TURBOSCXML_PUBLIC_LINKS turbo_scxml INTERFACE_LINK_LIBRARIES)
  set(TURBOSCXML_EXPECTED_LINKS
    "TurboUtils::CFlow;TurboUtils::CMeta;TurboUtils::XmlParser;$<LINK_ONLY:TurboUtils::Core>;$<LINK_ONLY:TurboUtils::QueryVM>")
  if(NOT TURBOSCXML_PUBLIC_LINKS STREQUAL TURBOSCXML_EXPECTED_LINKS)
    message(FATAL_ERROR
      "TurboSCXML::SCXML dependency contract changed")
  endif()

  if(NOT TARGET TurboUtils::TinyTest)
    message(FATAL_ERROR
      "BUILD_TESTING requires TurboUtils::TinyTest in the selected SDK")
  endif()
  add_subdirectory(tests)
endif()

install(TARGETS turbo_scxml
  EXPORT TurboSCXMLTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  DIRECTORY include/scxml
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")

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
