option(TURBOSCXML_ENABLE_QUICKJS
  "Enable the bounded quickjs-sandbox SCXML data-model profile" OFF)
option(TURBOSCXML_ENABLE_CHTTP_RESOURCE
  "Enable the optional blocking CHTTP SCXML resource adapter" OFF)
option(TURBOSCXML_ENABLE_CHTTP_EVENT_IO
  "Build the optional CHTTP BasicHTTP Event I/O processor" OFF)
option(TURBOSCXML_ENABLE_VOICEXML_CMETA
  "Build and install the optional VoiceXML CMeta profile" ON)
option(TURBOSCXML_ENABLE_PLUGIN
  "Build and install the optional Salts Plugin composition bridge" ON)
option(TURBOSCXML_ENABLE_COMPONENT
  "Build the optional Salts Component provider composition bridge" OFF)
option(TURBOSCXML_ENABLE_HOST
  "Build provider-neutral ACE Host routing and SCXML Event I/O" OFF)
option(TURBOSCXML_ENABLE_CNET_EVENT_IO
  "Build optional owner-driven CNet chunk ingress into SCXML Sessions" OFF)
option(ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)

option(TURBOSCXML_ENABLE_FUZZ_TESTS
  "Build and register deterministic bounded VoiceXML fuzz tests" OFF)

# Resolve optional native SDK dependencies before the vcpkg toolchain runs.
set(VCPKG_MANIFEST_FEATURES "")
if(TURBOSCXML_ENABLE_QUICKJS)
  list(APPEND VCPKG_MANIFEST_FEATURES quickjs)
endif()
if(TURBOSCXML_ENABLE_CHTTP_RESOURCE OR TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  list(APPEND VCPKG_MANIFEST_FEATURES chttp)
endif()
