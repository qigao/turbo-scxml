cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED REPO_ROOT)
  message(FATAL_ERROR "REPO_ROOT is required")
endif()

file(GLOB voice_sources "${REPO_ROOT}/src/voicexml*.c")
list(LENGTH voice_sources source_count)
if(source_count LESS 1)
  message(FATAL_ERROR "no VoiceXML source files found")
endif()

set(forbidden_names
  "printf"
  "fprintf"
  "vprintf"
  "vfprintf"
  "fwrite"
  "puts"
  "perror")

foreach(path IN LISTS voice_sources)
  file(READ "${path}" source)
  foreach(name IN LISTS forbidden_names)
    # Match a standalone C identifier call, but not snprintf/vsnprintf.
    string(REGEX MATCH
      "(^|[^A-Za-z0-9_])${name}[ \t\r\n]*\\("
      forbidden_match
      "${source}")
    if(forbidden_match)
      file(RELATIVE_PATH relative "${REPO_ROOT}" "${path}")
      message(FATAL_ERROR
        "VoiceXML direct output sink is forbidden: ${relative}: ${name}()")
    endif()
  endforeach()

  string(REGEX MATCH "(^|[^A-Za-z0-9_])SALTS_LOG[A-Za-z0-9_]*" salts_macro "${source}")
  string(REGEX MATCH "(^|[^A-Za-z0-9_])salts_log[A-Za-z0-9_]*" salts_fn "${source}")
  if(salts_macro OR salts_fn)
    file(RELATIVE_PATH relative "${REPO_ROOT}" "${path}")
    message(FATAL_ERROR
      "VoiceXML default SALTS logging is forbidden: ${relative}")
  endif()
endforeach()

message(STATUS "Validated no-output security contract across ${source_count} VoiceXML source files")
