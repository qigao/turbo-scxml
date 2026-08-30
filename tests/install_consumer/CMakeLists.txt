cmake_minimum_required(VERSION 3.20)

project(TurboSCXMLInstallConsumer LANGUAGES C)

if(NOT DEFINED ENV{TURBOSCXML_ROOT}
   OR "$ENV{TURBOSCXML_ROOT}" STREQUAL "")
  message(FATAL_ERROR "TURBOSCXML_ROOT is required")
endif()
file(TO_CMAKE_PATH "$ENV{TURBOSCXML_ROOT}" TURBOSCXML_ROOT_PATH)
if(NOT IS_DIRECTORY "${TURBOSCXML_ROOT_PATH}")
  message(FATAL_ERROR
    "TURBOSCXML_ROOT is not a directory: $ENV{TURBOSCXML_ROOT}")
endif()

unset(TurboSCXML_DIR CACHE)
unset(TurboSCXML_DIR)
find_package(TurboSCXML CONFIG REQUIRED
  PATHS "${TURBOSCXML_ROOT_PATH}"
  NO_DEFAULT_PATH)

add_executable(turboscxml_install_consumer main.c)
target_compile_features(turboscxml_install_consumer PRIVATE c_std_11)
target_link_libraries(
  turboscxml_install_consumer PRIVATE TurboSCXML::SCXML)
