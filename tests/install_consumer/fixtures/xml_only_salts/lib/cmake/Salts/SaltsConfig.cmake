if(NOT DEFINED ENV{SALTS_XML_ONLY_REAL_ROOT}
   OR "$ENV{SALTS_XML_ONLY_REAL_ROOT}" STREQUAL "")
  set(Salts_FOUND FALSE)
  set(Salts_NOT_FOUND_MESSAGE
    "SALTS_XML_ONLY_REAL_ROOT must name the real Salts install prefix")
  return()
endif()
file(TO_CMAKE_PATH "$ENV{SALTS_XML_ONLY_REAL_ROOT}"
  SALTS_XML_ONLY_REAL_ROOT_PATH)
if(NOT IS_DIRECTORY "${SALTS_XML_ONLY_REAL_ROOT_PATH}/include" OR
   NOT IS_DIRECTORY "${SALTS_XML_ONLY_REAL_ROOT_PATH}/lib")
  set(Salts_FOUND FALSE)
  set(Salts_NOT_FOUND_MESSAGE
    "SALTS_XML_ONLY_REAL_ROOT is not a Salts install prefix: "
    "$ENV{SALTS_XML_ONLY_REAL_ROOT}")
  return()
endif()

set(_SALTS_XML_ONLY_LIBRARIES)
foreach(_SALTS_XML_ONLY_LIBRARY IN ITEMS
    xml_parser
    query_vm
    salts
    salts_cmeta
    salts_coroutine
    salts_concurrency
    salts_platform)
  unset(_SALTS_XML_ONLY_LIBRARY_PATH CACHE)
  unset(_SALTS_XML_ONLY_LIBRARY_PATH)
  find_library(_SALTS_XML_ONLY_LIBRARY_PATH
    NAMES "${_SALTS_XML_ONLY_LIBRARY}"
    PATHS "${SALTS_XML_ONLY_REAL_ROOT_PATH}/lib"
    NO_DEFAULT_PATH)
  if(NOT _SALTS_XML_ONLY_LIBRARY_PATH)
    set(Salts_FOUND FALSE)
    set(Salts_NOT_FOUND_MESSAGE
      "The XmlParser-only fixture cannot find "
      "${_SALTS_XML_ONLY_LIBRARY} under "
      "${SALTS_XML_ONLY_REAL_ROOT_PATH}/lib")
    return()
  endif()
  list(APPEND _SALTS_XML_ONLY_LIBRARIES
    "${_SALTS_XML_ONLY_LIBRARY_PATH}")
endforeach()
if(WIN32)
  list(APPEND _SALTS_XML_ONLY_LIBRARIES bcrypt)
endif()

add_library(Salts::XmlParser INTERFACE IMPORTED)
set_target_properties(Salts::XmlParser PROPERTIES
  INTERFACE_COMPILE_FEATURES c_std_11
  INTERFACE_INCLUDE_DIRECTORIES
    "${SALTS_XML_ONLY_REAL_ROOT_PATH}/include;${SALTS_XML_ONLY_REAL_ROOT_PATH}/include/query_vm"
  INTERFACE_LINK_LIBRARIES "${_SALTS_XML_ONLY_LIBRARIES}")
set(Salts_XmlParser_FOUND TRUE)
