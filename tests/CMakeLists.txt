add_executable(device-input-test device-input-test.cc)
target_include_directories(device-input-test PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME device-input COMMAND device-input-test)

# Both editions exercise the same device logic with simulated Linux endpoints.
if(PROJECTEUR_WITH_KDE)
  set(device_test_platform "${PROJECT_SOURCE_DIR}/src/projecteur-logging.cc")
  set(device_test_libraries Qt6::Core Qt6::Gui Intl::Intl)
else()
  set(device_test_platform
    "${PROJECT_SOURCE_DIR}/src/native/platform.cc"
    "${PROJECT_SOURCE_DIR}/src/native/files.cc"
    "${PROJECT_SOURCE_DIR}/src/native/binary.cc")
  set(device_test_libraries PkgConfig::GIO nlohmann_json::nlohmann_json Intl::Intl)
endif()

add_executable(input-mapper-recovery-test
  input-mapper-recovery-test.cc
  "${PROJECT_SOURCE_DIR}/src/deviceinput.cc"
  "${PROJECT_SOURCE_DIR}/src/deviceinput.h"
  ${device_test_platform})
add_test(NAME input-mapper-recovery COMMAND input-mapper-recovery-test)

add_executable(hidpp-pointer-control-test
  hidpp-pointer-control-test.cc
  "${PROJECT_SOURCE_DIR}/src/device-hidpp.cc"
  "${PROJECT_SOURCE_DIR}/src/device-hidpp.h"
  "${PROJECT_SOURCE_DIR}/src/device.cc"
  "${PROJECT_SOURCE_DIR}/src/device.h"
  "${PROJECT_SOURCE_DIR}/src/hidpp.cc"
  "${PROJECT_SOURCE_DIR}/src/hidpp.h"
  "${PROJECT_SOURCE_DIR}/src/deviceinput.cc"
  "${PROJECT_SOURCE_DIR}/src/deviceinput.h"
  "${PROJECT_SOURCE_DIR}/src/virtualdevice.cc"
  ${device_test_platform})
add_test(NAME hidpp-pointer-control COMMAND hidpp-pointer-control-test)
set_tests_properties(hidpp-pointer-control PROPERTIES TIMEOUT 30)

foreach(test_target IN ITEMS input-mapper-recovery-test hidpp-pointer-control-test)
  target_include_directories(${test_target} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}")
  target_link_libraries(${test_target} PRIVATE ${device_test_libraries})
  if(PROJECTEUR_WITH_GNOME)
    target_compile_definitions(${test_target} PRIVATE PROJECTEUR_NATIVE_GNOME=1)
  endif()
endforeach()

if(PROJECTEUR_WITH_GNOME)
  find_program(NODE_EXECUTABLE node REQUIRED)
  add_test(NAME gnome-geometry COMMAND "${NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/gnome/tests/geometry.test.mjs")
  add_test(NAME gnome-panel COMMAND "${NODE_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/gnome/tests/panel.test.mjs")
  find_program(GJS_EXECUTABLE gjs REQUIRED)
  find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
  if(TARGET pofiles)
    foreach(language IN ITEMS de en)
      if(language STREQUAL "de")
        set(locale de_DE.UTF-8)
      else()
        set(locale en_US.UTF-8)
      endif()
      add_test(NAME "gnome-translations-${language}"
        COMMAND "${CMAKE_COMMAND}" -E env "LC_ALL=${locale}" "LANGUAGE="
          "${GJS_EXECUTABLE}" -m "${PROJECT_SOURCE_DIR}/gnome/tests/translations.test.js"
          "${PROJECT_BINARY_DIR}/gnome-preferences" "${language}")
    endforeach()
    add_test(NAME gnome-translations-fallback
      COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=en_US.UTF-8 LANGUAGE=fr
        "${GJS_EXECUTABLE}" -m "${PROJECT_SOURCE_DIR}/gnome/tests/translations.test.js"
        "${PROJECT_BINARY_DIR}/gnome-preferences" fr)
  endif()

  # Replace hardware enumeration and the executable entry point. The test
  # drives the same service and pointer-mode signal used by the GNOME RPM.
  get_target_property(app_sources projecteur SOURCES)
  set(mode_test_sources)
  foreach(source IN LISTS app_sources)
    if(source MATCHES "(^|/)(main|devicescan)\\.cc$")
      continue()
    endif()
    if(IS_ABSOLUTE "${source}" OR source MATCHES "^\\$<")
      list(APPEND mode_test_sources "${source}")
    else()
      list(APPEND mode_test_sources "${PROJECT_SOURCE_DIR}/${source}")
    endif()
  endforeach()
  add_executable(app-mode-cycle-test app-mode-cycle-test.cc ${mode_test_sources})
  target_include_directories(app-mode-cycle-test PRIVATE $<TARGET_PROPERTY:projecteur,INCLUDE_DIRECTORIES>)
  target_compile_definitions(app-mode-cycle-test PRIVATE $<TARGET_PROPERTY:projecteur,COMPILE_DEFINITIONS>)
  target_link_libraries(app-mode-cycle-test PRIVATE $<TARGET_PROPERTY:projecteur,LINK_LIBRARIES>)
  add_dependencies(app-mode-cycle-test projecteur)
  add_test(NAME app-mode-cycle COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" --
    "$<TARGET_FILE:app-mode-cycle-test>")
  set_tests_properties(app-mode-cycle PROPERTIES TIMEOUT 30)

  add_test(NAME gnome-bridge COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" --
    "${GJS_EXECUTABLE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/gnome-bridge-test.js"
    "$<TARGET_FILE:projecteur>")
  set_tests_properties(gnome-bridge PROPERTIES TIMEOUT 30)
  set_tests_properties(gnome-bridge PROPERTIES ENVIRONMENT "PROJECTEUR_TEST_HEADLESS=1")
endif()
