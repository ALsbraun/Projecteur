cmake_minimum_required(VERSION 3.20)

# Set the default build type to release
if( NOT CMAKE_BUILD_TYPE )
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECTEUR_RELEASE_VERSION
     LIMIT_COUNT 1 REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT PROJECTEUR_RELEASE_VERSION)
  message(FATAL_ERROR "VERSION must contain one semantic version such as 1.0.0")
endif()

project(Projecteur VERSION "${PROJECTEUR_RELEASE_VERSION}" LANGUAGES C CXX)
include(CTest)

add_compile_options(-Wall -Wextra -Werror)
#set(CMAKE_CXX_CLANG_TIDY clang-tidy-12)

find_package(Gettext REQUIRED)
option(PROJECTEUR_WITH_KDE "Build the Plasma overlay, screencast and applet" OFF)
option(PROJECTEUR_WITH_GNOME "Build the GTK preferences and install the GNOME Shell extension" ON)
if((PROJECTEUR_WITH_GNOME AND PROJECTEUR_WITH_KDE)
   OR (NOT PROJECTEUR_WITH_GNOME AND NOT PROJECTEUR_WITH_KDE))
  message(FATAL_ERROR "Select one desktop edition per build directory.")
endif()
if(PROJECTEUR_WITH_KDE)
  find_package(ECM 6.7 REQUIRED NO_MODULE)
  list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
  include(KDEInstallDirs6)
else()
  include(GNUInstallDirs)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(GitVersion)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Intl REQUIRED)
if(PROJECTEUR_WITH_KDE)
  set(CMAKE_AUTOMOC ON)
  find_package(Qt6 6.10 REQUIRED COMPONENTS Core DBus Gui Quick ShaderTools WaylandClient Widgets)
  find_package(WaylandScanner REQUIRED)
  find_package(KF6Config 6.7 REQUIRED)
  find_package(KF6 6.7 REQUIRED COMPONENTS CoreAddons I18n ConfigWidgets DBusAddons GlobalAccel
                                          Notifications WidgetsAddons WindowSystem XmlGui)
  find_package(KF6 6.7 REQUIRED COMPONENTS Package KirigamiPlatform)
  find_package(KPipeWire 6.7 REQUIRED)
  find_package(Plasma 6.7 REQUIRED)
  find_package(LayerShellQt 6.7 REQUIRED)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
  find_package(nlohmann_json 3.10 REQUIRED)
endif()

add_executable(projecteur
  src/enum-helper.h
  src/device.cc                src/device.h
  src/device-command-helper.cc src/device-command-helper.h
  src/device-hidpp.cc          src/device-hidpp.h
  src/device-key-lookup.cc     src/device-key-lookup.h
  src/deviceinput.cc           src/deviceinput.h
  src/devicescan.cc            src/devicescan.h
  src/hidpp.cc                 src/hidpp.h
  src/presentationtimer.cc     src/presentationtimer.h
  src/projecteur-i18n.cc       src/projecteur-i18n.h
  src/spotlight.cc             src/spotlight.h
  src/virtualdevice.cc         src/virtualdevice.h
)

if(PROJECTEUR_WITH_KDE)
  qt6_add_resources(RESOURCES qml/qml.qrc resources.qrc)
  target_sources(projecteur PRIVATE
    src/main.cc src/projecteurapp.cc src/projecteurapp.h
    src/gnomeoverlay.cc src/gnomeoverlay.h
    src/projecteurcontrol.cc src/projecteurcontrol.h
    src/projecteur-logging.cc src/settings.cc src/settings.h
    src/actiondelegate.cc src/actiondelegate.h
    src/deviceswidget.cc src/deviceswidget.h
    src/iconwidgets.cc src/iconwidgets.h
    src/imageitem.cc src/imageitem.h
    src/inputmapconfig.cc src/inputmapconfig.h
    src/inputseqedit.cc src/inputseqedit.h
    src/kwinscreencast.cc src/kwinscreencast.h
    src/linuxdesktop.cc src/linuxdesktop.h
    src/nativekeyseqedit.cc src/nativekeyseqedit.h
    src/preferencesdlg.cc src/preferencesdlg.h
    src/spotshapes.cc src/spotshapes.h
    ${RESOURCES}
  )
  qt6_add_shaders(projecteur projecteur_shaders
    PREFIX "/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml/shaders"
    FILES qml/shaders/textzoom.frag
          qml/shaders/diffusedot.frag
          qml/shaders/dottrail.frag
  )
  target_compile_definitions(projecteur PRIVATE PROJECTEUR_WITH_KDE=1)
  qt6_generate_wayland_protocol_client_sources(projecteur
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/protocols/zkde-screencast-unstable-v1.xml"
    PRIVATE_CODE
  )
endif()

set(PROJECTEUR_CONTROL_XML
  "${CMAKE_CURRENT_SOURCE_DIR}/src/org.projecteur.Projecteur.xml")
if(PROJECTEUR_WITH_KDE)
  qt_add_dbus_adaptor(PROJECTEUR_DBUS_ADAPTOR_SOURCES
  "${PROJECTEUR_CONTROL_XML}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/projecteurcontrol.h"
  ProjecteurControl
  projecteurcontroladaptor
  ProjecteurControlAdaptor
)
  target_sources(projecteur PRIVATE ${PROJECTEUR_DBUS_ADAPTOR_SOURCES})
  kconfig_target_kcfg_file(projecteur
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/projecteurconfig.kcfg"
    CLASS_NAME ProjecteurConfig
    MUTATORS
    DEFAULT_VALUE_GETTERS
    KCONFIG_CONSTRUCTOR
  )
else()
  file(READ "${PROJECTEUR_CONTROL_XML}" PROJECTEUR_CONTROL_XML_CONTENT)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECTEUR_CONTROL_XML}")
  configure_file(src/native/control-xml.h.in native-control-xml.h @ONLY)
  target_sources(projecteur PRIVATE
    src/native/platform.cc src/native/files.cc src/native/binary.cc
    src/native/settings.cc src/settingsfile.cc src/native/preferences.cc
    src/native/service.cc src/native/main.cc)
  target_compile_definitions(projecteur PRIVATE PROJECTEUR_NATIVE_GNOME=1)
  target_link_libraries(projecteur PRIVATE PkgConfig::GIO nlohmann_json::nlohmann_json)
endif()

target_include_directories(projecteur PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(projecteur PRIVATE Intl::Intl)
if(PROJECTEUR_WITH_KDE)
  target_link_libraries(projecteur PRIVATE
    Qt6::Core Qt6::DBus Qt6::Gui Qt6::Quick Qt6::WaylandClient Qt6::Widgets
    KF6::ConfigCore KF6::ConfigGui KF6::CoreAddons KF6::I18n KF6::ConfigWidgets KF6::DBusAddons KF6::GlobalAccel KF6::Notifications
    KF6::WidgetsAddons KF6::WindowSystem KF6::XmlGui
    K::KPipeWire LayerShellQt::Interface
  )
endif()

target_compile_options(projecteur
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra>
)

target_compile_definitions(projecteur PRIVATE
  CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
  PROJECTEUR_LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}")
set(PROJECTEUR_PACKAGE_VERSION "" CACHE STRING "Distribution package version for diagnostics")
if(PROJECTEUR_PACKAGE_VERSION)
  target_compile_definitions(projecteur PRIVATE PROJECTEUR_PACKAGE_VERSION="${PROJECTEUR_PACKAGE_VERSION}")
endif()

if(PROJECTEUR_WITH_KDE)
  add_subdirectory(plasma)
endif()
if(PROJECTEUR_WITH_GNOME)
  set(PROJECTEUR_PREFERENCES_DIR "${CMAKE_INSTALL_LIBEXECDIR}/projecteur")
  set(PROJECTEUR_PREFERENCES_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gnome-preferences")
  file(RELATIVE_PATH PROJECTEUR_PREFERENCES_RELPATH "${CMAKE_INSTALL_PREFIX}/bin"
       "${CMAKE_INSTALL_FULL_LIBEXECDIR}/projecteur")
  target_compile_definitions(projecteur PRIVATE PROJECTEUR_WITH_GNOME
    PROJECTEUR_PREFERENCES_RELPATH="${PROJECTEUR_PREFERENCES_RELPATH}")
  set(PROJECTEUR_BUILD_LOCALE_DIRNAME locale)
  configure_file(gnome/preferences/i18n.js.in "${PROJECTEUR_PREFERENCES_BUILD_DIR}/i18n.js" @ONLY)
  install(FILES "${PROJECTEUR_PREFERENCES_BUILD_DIR}/i18n.js" DESTINATION "${PROJECTEUR_PREFERENCES_DIR}")
  foreach(module IN ITEMS main.js devices.js)
    configure_file("gnome/preferences/${module}" "${PROJECTEUR_PREFERENCES_BUILD_DIR}/${module}" COPYONLY)
    install(FILES "${PROJECTEUR_PREFERENCES_BUILD_DIR}/${module}"
            DESTINATION "${PROJECTEUR_PREFERENCES_DIR}")
  endforeach()
  install(DIRECTORY gnome/projecteur@projecteur.github.io
          DESTINATION share/gnome-shell/extensions
          PATTERN "tests" EXCLUDE)
endif()

# Set version project properties for builds not from a git repository (e.g. created with git archive)
# If creating the version number via git information fails, the following target properties
# will be used. IMPORTANT - when creating a release tag with git flow:
#  Update this information - the version numbers and the version type.
#  VERSION_TYPE must be either 'release' or 'develop'
set_target_properties(projecteur PROPERTIES
  VERSION_MAJOR "${PROJECT_VERSION_MAJOR}"
  VERSION_MINOR "${PROJECT_VERSION_MINOR}"
  VERSION_PATCH "${PROJECT_VERSION_PATCH}"
  # Source archives do not contain .git. They must still report the release version.
  VERSION_TYPE release
  VERSION_DISTANCE_OFFSET 0
)
add_version_info(projecteur "${CMAKE_CURRENT_SOURCE_DIR}")

# Create files containing generated version strings, helping package maintainers
get_target_property(PROJECTEUR_VERSION_STRING projecteur VERSION_STRING_FULL)
# Arch Linux = PKGBUILD/makepkg: '-' is not allowed in version number
string(REPLACE "-" "" PROJECTEUR_VERSION_STRING_ARCHLINUX "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string" "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string.archlinux" "${PROJECTEUR_VERSION_STRING_ARCHLINUX}")

set(PROJECTEUR_DE_CATALOG "${CMAKE_CURRENT_BINARY_DIR}/locale/de/LC_MESSAGES/projecteur.mo")
add_custom_command(
  OUTPUT "${PROJECTEUR_DE_CATALOG}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/de/LC_MESSAGES"
  COMMAND "${GETTEXT_MSGFMT_EXECUTABLE}" --check -o "${PROJECTEUR_DE_CATALOG}"
          "${CMAKE_CURRENT_SOURCE_DIR}/po/de/projecteur.po"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/de/projecteur.po"
  VERBATIM
)
add_custom_target(pofiles DEPENDS "${PROJECTEUR_DE_CATALOG}")
add_dependencies(projecteur pofiles)
install(FILES "${PROJECTEUR_DE_CATALOG}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/de/LC_MESSAGES")

# Add target with non-source files for convenience when using IDEs like QtCreator and others
add_custom_target(non-sources SOURCES README.md CONTRIBUTING.md LICENSE.md
                                      doc/CHANGELOG.md doc/USER-GUIDE.md
                                      doc/TROUBLESHOOTING.md devices.conf
                                      src/extra-devices.cc.in src/projecteurconfig.kcfg
                                      src/org.projecteur.Projecteur.xml
                                      qml/shaders/textzoom.frag
                                      qml/shaders/diffusedot.frag
                                      qml/shaders/dottrail.frag
                                      55-projecteur.rules.in projecteur.notifyrc
                                      cmake/templates/projecteur.desktop.in)

# Install
#---------------------------------------------------------------------------------------------------
# Set default directory permissions with CMake >= 3.11, avoids some
# lintian 'non-standard-dir-perm' errors for deb packages.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
)

install(TARGETS projecteur DESTINATION bin)
set(PROJECTEUR_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/bin/projecteur") #used in desktop file template

# Use udev.pc pkg-config file to set the dir path
if (NOT CMAKE_INSTALL_UDEVRULESDIR)
  set (UDEVDIR /lib/udev)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PKGCONFIG_UDEV udev)
    if(PKGCONFIG_UDEV_FOUND)
      execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=udevdir udev
        OUTPUT_VARIABLE PKGCONFIG_UDEVDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(PKGCONFIG_UDEVDIR)
        file(TO_CMAKE_PATH "${PKGCONFIG_UDEVDIR}" UDEVDIR)
      endif(PKGCONFIG_UDEVDIR)
    endif(PKGCONFIG_UDEV_FOUND)
  endif(PKG_CONFIG_FOUND)
endif(NOT CMAKE_INSTALL_UDEVRULESDIR)
set (CMAKE_INSTALL_UDEVDIR ${UDEVDIR} CACHE PATH "Udev base dir.")
mark_as_advanced(CMAKE_INSTALL_UDEVDIR)
set (CMAKE_INSTALL_UDEVRULESDIR ${UDEVDIR}/rules.d CACHE PATH "Where to install udev rules")
mark_as_advanced(CMAKE_INSTALL_UDEVRULESDIR)

# Configure and install files
set(OUTDIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TMPLDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates")

# Read devices.conf file
set(idRegex "0x([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])")
set(lineRegex "^[ \t]*${idRegex}[ \t]*,[ \t]*${idRegex}[ \t]*,[ \t]*(usb|bt)[ \t]*,[ \t]*(.*)[ \t]*")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/devices.conf" CONFLINES REGEX "${lineRegex}")
foreach(line ${CONFLINES})
  #message(STATUS "## ${line}")
  if(line MATCHES "${lineRegex}")
    # message(STATUS "vendorId: ${CMAKE_MATCH_1}, productId: ${CMAKE_MATCH_2}, ${CMAKE_MATCH_3}, '${CMAKE_MATCH_4}'")
    set(vendorId "${CMAKE_MATCH_1}")
    set(productId "${CMAKE_MATCH_2}")

    if("${CMAKE_MATCH_3}" STREQUAL "usb")
      string(APPEND EXTRA_USB_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_USB_UDEV_RULES "\nSUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"${vendorId}\"")
      string(APPEND EXTRA_USB_UDEV_RULES ", ATTRS{idProduct}==\"${productId}\", MODE=\"0660\", TAG+=\"uaccess\"")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, false, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
    elseif("${CMAKE_MATCH_3}" STREQUAL "bt")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, true, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(TOLOWER "${vendorId}" vendorId)
      string(TOLOWER "${productId}" productId)
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\nSUBSYSTEM==\"input\", KERNEL==\"event*\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "ATTRS{id/bustype}==\"0005\", ATTRS{id/vendor}==\"${vendorId}\", ATTRS{id/product}==\"${productId}\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "MODE=\"0660\", TAG+=\"uaccess\"")
    endif()
  endif()
endforeach()

configure_file("src/extra-devices.cc.in" "src/extra-devices.cc" @ONLY)
set_property(TARGET projecteur APPEND PROPERTY SOURCES "${CMAKE_CURRENT_BINARY_DIR}/src/extra-devices.cc")

configure_file("55-projecteur.rules.in" "55-projecteur.rules" @ONLY)
install(FILES "${OUTDIR}/55-projecteur.rules" DESTINATION ${CMAKE_INSTALL_UDEVRULESDIR}/)

install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/48x48/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/64x64/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/128x128/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/256x256/apps/ RENAME projecteur.svg)
if(PROJECTEUR_WITH_KDE)
  install(FILES projecteur.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
endif()
install(FILES src/org.projecteur.Projecteur.xml DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces")

# Set variables for file configurations
get_target_property(VERSION_STRING projecteur VERSION_STRING)
get_target_property(VERSION_DATE_MONTH_YEAR projecteur VERSION_DATE_MONTH_YEAR)
set(HOMEPAGE "https://github.com/ALsbraun/Projecteur")

set(PROJECTEUR_APPLICATION_ID "org.projecteur.Projecteur")
set(PROJECTEUR_DESKTOP_ID "${PROJECTEUR_APPLICATION_ID}")
set(PROJECTEUR_DESKTOP_ACTIVATABLE true)
set(PROJECTEUR_DESKTOP_STARTUP_NOTIFY false)
set(PROJECTEUR_DESKTOP_EXEC "${PROJECTEUR_INSTALL_PATH} --show-dialog")
set(PROJECTEUR_DESKTOP_ICON projecteur)
set(PROJECTEUR_DESKTOP_KDE_INTERFACES "")
if(PROJECTEUR_WITH_KDE)
  set(PROJECTEUR_DESKTOP_KDE_INTERFACES
    "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\nX-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1\n")
endif()
if(PROJECTEUR_WITH_GNOME AND NOT PROJECTEUR_WITH_KDE)
  set(PROJECTEUR_DESKTOP_ID "${PROJECTEUR_APPLICATION_ID}.Preferences")
  set(PROJECTEUR_DESKTOP_ACTIVATABLE false)
  set(PROJECTEUR_DESKTOP_STARTUP_NOTIFY true)
  set(PROJECTEUR_DESKTOP_EXEC "gjs -m \"${CMAKE_INSTALL_FULL_LIBEXECDIR}/projecteur/main.js\"")
  set(PROJECTEUR_DESKTOP_ICON "${PROJECTEUR_DESKTOP_ID}")
  install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/scalable/apps/
          RENAME "${PROJECTEUR_DESKTOP_ICON}.svg")
endif()
configure_file("${TMPLDIR}/projecteur.desktop.in"
               "${PROJECTEUR_DESKTOP_ID}.desktop" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_DESKTOP_ID}.desktop"
        DESTINATION share/applications/)
configure_file("${TMPLDIR}/projecteur.service.in" "${PROJECTEUR_APPLICATION_ID}.service" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.service" DESTINATION share/dbus-1/services)

# Configure man page and gzip it.
option(COMPRESS_MAN_PAGE "Compress the man page" ON)
configure_file("${TMPLDIR}/projecteur.1" "${OUTDIR}/projecteur.1" @ONLY)

if(COMPRESS_MAN_PAGE)
  find_program(GZIP_EXECUTABLE gzip)
  add_custom_command(
    OUTPUT ${OUTDIR}/projecteur.1.gz
    COMMAND ${GZIP_EXECUTABLE} -9f -n "${OUTDIR}/projecteur.1"
    WORKING_DIRECTORY ${OUTDIR}
  )
  add_custom_target(gzip-manpage ALL DEPENDS "${OUTDIR}/projecteur.1.gz")
  install(FILES "${OUTDIR}/projecteur.1.gz" DESTINATION share/man/man1/)
else()
  install(FILES "${OUTDIR}/projecteur.1" DESTINATION share/man/man1/)
endif()

configure_file("${TMPLDIR}/projecteur.metainfo.xml"
               "${PROJECTEUR_APPLICATION_ID}.metainfo.xml" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.metainfo.xml"
        DESTINATION share/metainfo/)

configure_file("${TMPLDIR}/projecteur.bash-completion" "projecteur.bash-completion" @ONLY)
install(FILES "${OUTDIR}/projecteur.bash-completion"
        DESTINATION share/bash-completion/completions/
        RENAME projecteur)

configure_file("${TMPLDIR}/preinst.in" "pkg/scripts/preinst" @ONLY)
configure_file("${TMPLDIR}/postinst.in" "pkg/scripts/postinst" @ONLY)

# --- Linux packaging ---
include(LinuxPackaging)

option(PACKAGE_TARGETS "Create packaging build targets" ON)

if(PACKAGE_TARGETS)
  # Add 'source-archive' target
  add_source_archive_target(projecteur)

  # Add 'dist-package' target: Creates a deb/rpm/tgz package depending on the current Linux distribution
  add_dist_package_target(
    PROJECT "${CMAKE_PROJECT_NAME}"
    TARGET projecteur
    DESCRIPTION_BRIEF "Wayland application for the Logitech Spotlight device."
    DESCRIPTION_FULL
"Projecteur is a virtual laser pointer for use with inertial pointers such as
 the Logitech Spotlight. Projecteur can show a colored dot, a highlighted
 circle or a zoom effect to act as a pointer. The location of the pointer
 moves in response to moving the handheld pointer device. The effect is much
 like that of a traditional laser pointer, except that it is captured by
 recording software and works across multiple screens."
    CONTACT "Jahn Fuchs <projecteur@jahn.textmo.de>"
    HOMEPAGE "${HOMEPAGE}"
    DEBIAN_SECTION "utils"
    # PREINST_SCRIPT "${OUTDIR}/pkg/scripts/preinst"
    POSTINST_SCRIPT "${OUTDIR}/pkg/scripts/postinst"
  )
  add_dependencies(dist-package projecteur)
  if(TARGET gzip-manpage)
    add_dependencies(dist-package gzip-manpage)
  endif()

  # Additional files for debian packages, adhering to some debian rules,
  # see https://manpages.debian.org/buster/lintian/lintian.1.en.html
  if ("${PKG_TYPE}" STREQUAL "DEB")
    # TODO Lintian expects the the copyright file in /usr/share/doc/projecteur
    #      This clashes with the default CMAKE_INSTALL_PREFIX /usr/local
    #      Need to check if this would clash with packages from the debian/ubuntu repos.
    #configure_file("${TMPLDIR}/copyright.in" "pkg/copyright" @ONLY)
    #install(FILES "${OUTDIR}/pkg/copyright" DESTINATION /usr/share/doc/projecteur/)

    ## -- prevent additional lintian warnings 'non-standard-dir-perm' for the cmake install prefix
    set(DIR_TO_INSTALL "${CMAKE_INSTALL_PREFIX}")
    while(NOT "${DIR_TO_INSTALL}" STREQUAL "/" AND NOT "${DIR_TO_INSTALL}" STREQUAL "")
      install(DIRECTORY DESTINATION "${DIR_TO_INSTALL}")
      get_filename_component(DIR_TO_INSTALL "${DIR_TO_INSTALL}" PATH)
    endwhile()
  endif()
endif()

option(ENABLE_IWYU "Enable Include-What-You-Use" OFF)
find_program(iwyu_path NAMES include-what-you-use iwyu)
if(ENABLE_IWYU AND iwyu_path)
  set_property(TARGET projecteur PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
