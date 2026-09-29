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

# VoiceXML-only isolation deliberately exposes no Salts targets. Its only
# public dependency is Salts::XmlParser, which belongs to the SaltsUtils
# fixture selected through SALTS_UTILS_ROOT.
set(Salts_FOUND TRUE)
