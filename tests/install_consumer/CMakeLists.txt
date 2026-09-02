cmake_minimum_required(VERSION 3.20)

project(TurboSCXMLInstallConsumer LANGUAGES C CXX)

option(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE
  "Require and exercise the installed CHttpResource component" OFF)
option(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO
  "Require and exercise the installed CHttpEventIO component" OFF)
option(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS
  "Require the installed quickjs-sandbox profile" OFF)

if((TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE OR
    TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO) AND
   TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS)
  message(FATAL_ERROR
    "The install consumer validates one optional profile at a time")
endif()

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
set(TURBOSCXML_INSTALL_COMPONENTS SCXML CCXML)
if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE)
  list(APPEND TURBOSCXML_INSTALL_COMPONENTS CHttpResource)
endif()
if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO)
  list(APPEND TURBOSCXML_INSTALL_COMPONENTS CHttpEventIO)
endif()
if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS)
  list(APPEND TURBOSCXML_INSTALL_COMPONENTS QuickJS)
endif()
find_package(TurboSCXML CONFIG REQUIRED
  COMPONENTS ${TURBOSCXML_INSTALL_COMPONENTS}
  PATHS "${TURBOSCXML_ROOT_PATH}"
  NO_DEFAULT_PATH)

add_executable(turboscxml_install_consumer main.c)
target_compile_features(turboscxml_install_consumer PRIVATE c_std_11)
target_link_libraries(
  turboscxml_install_consumer PRIVATE TurboSCXML::SCXML)

if(NOT TARGET TurboSCXML::CCXML)
  message(FATAL_ERROR "Expected installed TurboSCXML::CCXML")
endif()
add_executable(turboscxml_ccxml_install_consumer ccxml_main.c)
target_compile_features(
  turboscxml_ccxml_install_consumer PRIVATE c_std_11)
target_link_libraries(
  turboscxml_ccxml_install_consumer PRIVATE TurboSCXML::CCXML)

add_executable(turboscxml_ccxml_install_consumer_cpp ccxml_main.cpp)
target_compile_features(
  turboscxml_ccxml_install_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(
  turboscxml_ccxml_install_consumer_cpp PRIVATE TurboSCXML::CCXML)
if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS)
  target_compile_definitions(
    turboscxml_install_consumer PRIVATE
    TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS=1)
endif()

if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE)
  if(NOT TARGET TurboSCXML::CHttpResource)
    message(FATAL_ERROR "Expected installed TurboSCXML::CHttpResource")
  endif()
  add_executable(turboscxml_chttp_install_consumer chttp_main.c)
  target_compile_features(turboscxml_chttp_install_consumer PRIVATE c_std_11)
  target_link_libraries(
    turboscxml_chttp_install_consumer PRIVATE TurboSCXML::CHttpResource)

  add_executable(turboscxml_chttp_install_consumer_cpp chttp_main.cpp)
  target_compile_features(
    turboscxml_chttp_install_consumer_cpp PRIVATE cxx_std_17)
  target_link_libraries(
    turboscxml_chttp_install_consumer_cpp PRIVATE
    TurboSCXML::CHttpResource)
endif()

if(TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO)
  if(NOT TARGET TurboSCXML::CHttpEventIO)
    message(FATAL_ERROR "Expected installed TurboSCXML::CHttpEventIO")
  endif()
  add_executable(turboscxml_chttp_event_io_consumer event_io_main.c)
  target_compile_features(
    turboscxml_chttp_event_io_consumer PRIVATE c_std_11)
  target_link_libraries(
    turboscxml_chttp_event_io_consumer PRIVATE
    TurboSCXML::CHttpEventIO)

  add_executable(
    turboscxml_chttp_event_io_consumer_cpp event_io_main.cpp)
  target_compile_features(
    turboscxml_chttp_event_io_consumer_cpp PRIVATE cxx_std_17)
  target_link_libraries(
    turboscxml_chttp_event_io_consumer_cpp PRIVATE
    TurboSCXML::CHttpEventIO)
endif()

if(NOT TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS AND
   NOT TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE AND
   NOT TURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO AND
   (TARGET TurboSCXML::CHttpResource OR
    TARGET TurboSCXML::CHttpEventIO OR
    EXISTS "${TURBOSCXML_ROOT_PATH}/include/scxml/chttp_resource.h" OR
    EXISTS "${TURBOSCXML_ROOT_PATH}/include/scxml/chttp_event_io.h"))
  message(FATAL_ERROR
    "Core-only install unexpectedly exposes a CHTTP component")
endif()
