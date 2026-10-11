add_executable(voicexml_base_fuzz_smoke
  voicexml_base_fuzz_smoke.c voicexml_fuzz_common.c)
target_link_libraries(voicexml_base_fuzz_smoke PRIVATE TurboSCXML::VoiceXML)
target_compile_definitions(voicexml_base_fuzz_smoke PRIVATE
  VOICEXML_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/base")

add_executable(voicexml_resource_fuzz_smoke
  voicexml_resource_fuzz_smoke.c voicexml_fuzz_common.c)
target_link_libraries(voicexml_resource_fuzz_smoke PRIVATE
  TurboSCXML::VoiceXMLDocumentStore TurboSCXML::VoiceXMLSubmitResource)
target_compile_definitions(voicexml_resource_fuzz_smoke PRIVATE
  VOICEXML_RESOURCE_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/resource")

if(TURBOSCXML_ENABLE_VOICEXML_CMETA)
  add_executable(voicexml_cmeta_fuzz_smoke
    voicexml_cmeta_fuzz_smoke.c voicexml_fuzz_common.c)
  target_link_libraries(voicexml_cmeta_fuzz_smoke PRIVATE TurboSCXML::VoiceXMLCMeta)
  target_compile_definitions(voicexml_cmeta_fuzz_smoke PRIVATE
    VOICEXML_CMETA_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/cmeta")
endif()

if(TURBOSCXML_ENABLE_QUICKJS)
  add_executable(voicexml_quickjs_fuzz_smoke
    voicexml_quickjs_fuzz_smoke.c voicexml_fuzz_common.c)
  target_link_libraries(voicexml_quickjs_fuzz_smoke PRIVATE TurboSCXML::VoiceXMLQuickJS)
  target_compile_definitions(voicexml_quickjs_fuzz_smoke PRIVATE
    VOICEXML_QUICKJS_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/quickjs")
endif()

foreach(target IN ITEMS voicexml_base_fuzz_smoke voicexml_resource_fuzz_smoke
    voicexml_cmeta_fuzz_smoke voicexml_quickjs_fuzz_smoke)
  if(TARGET ${target})
    target_compile_features(${target} PRIVATE c_std_11)
    set_target_properties(${target} PROPERTIES
      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "tests/fuzz")
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
      LABELS "voicexml-fuzz" TIMEOUT 30 ENVIRONMENT "VOICEXML_FUZZ_TRACE=1")
  endif()
endforeach()
