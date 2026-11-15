# the launcher of powwow: lists the MUDs, edits the settings and starts
# powwow in a terminal
#
#     cmake -S launcher -B launcher/build && cmake --build launcher/build

cmake_minimum_required(VERSION 3.21)
project(powwow-launcher VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# for clang-tidy, which make launcher-tidy runs on the sources
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# the version of powwow, from configure.ac
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../configure.ac" ac_init
     REGEX "^AC_INIT")
string(REGEX REPLACE ".*\\[powwow\\],[ ]*\\[([^]]*)\\].*" "\\1"
       POWWOW_VERSION "${ac_init}")

# Qt can draw with Vulkan, and read keyboard layouts with XKB, which the
# launcher does not need: do not look for them, rather than report them
# missing
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_XKB TRUE)
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(powwow-launcher
    main.cpp
    launcher.cpp
    launcher.h
)

qt_add_qml_module(powwow-launcher
    URI Powwow
    VERSION 1.0
    QML_FILES
        Main.qml
        MudsPage.qml
        SettingsPage.qml
        MudDialog.qml
        AboutPage.qml
        ManualPage.qml
        SelectableText.qml
        EntryField.qml
        PowwowDialog.qml
        InfoDialog.qml
        DuplicateDialog.qml
        ListRow.qml
        FocusRing.qml
        PowwowToolTip.qml
        ShortcutMenuItem.qml
        HiliteEditor.qml
        ColorButton.qml
)

# the icon of powwow, the placeholder that icons/make-icon.sh makes: of
# the windows, and on Windows of the program
set(icon "${CMAKE_CURRENT_SOURCE_DIR}/../icons/powwow.ico")
set_source_files_properties("${icon}" PROPERTIES QT_RESOURCE_ALIAS powwow.ico)
qt_add_resources(powwow-launcher icon PREFIX / FILES "${icon}")
if(WIN32)
    target_sources(powwow-launcher PRIVATE powwow-launcher.rc)
endif()
# and on macOS of the app, in Finder and the Dock
if(APPLE)
    set(icns "${CMAKE_CURRENT_BINARY_DIR}/powwow.icns")
    add_custom_command(OUTPUT "${icns}"
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/make-icns.sh" "${icon}"
                "${icns}"
        DEPENDS "${icon}" make-icns.sh)
    target_sources(powwow-launcher PRIVATE "${icns}")
    set_source_files_properties("${icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(powwow-launcher PROPERTIES
        MACOSX_BUNDLE_ICON_FILE powwow.icns)
endif()

# the warnings for the sources of the launcher, not for the C++ that Qt
# generates from the QML files
if(MSVC)
    set(warnings /W4)
else()
    set(warnings -Wall -Wextra)
endif()
set_source_files_properties(main.cpp launcher.cpp PROPERTIES
    COMPILE_OPTIONS "${warnings}")

# on macOS, AppKit for bringing a window of the terminal to the front
if(APPLE)
    enable_language(OBJCXX)
    target_sources(powwow-launcher PRIVATE macos.mm macos.h)
    set_source_files_properties(macos.mm PROPERTIES
        COMPILE_OPTIONS "${warnings}")
    target_link_libraries(powwow-launcher PRIVATE "-framework AppKit")
endif()

# the powwow of the same tree, which the launcher runs when it runs from
# its build directory, as powwow then takes the manual of its source tree;
# make launcher gives that of its build directory
set(POWWOW_BUILD_PROGRAM "${CMAKE_CURRENT_SOURCE_DIR}/../src/powwow"
    CACHE FILEPATH
    "the powwow that the launcher runs from its build directory")

target_compile_definitions(powwow-launcher PRIVATE
    POWWOW_VERSION="${POWWOW_VERSION}"
    LAUNCHER_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    POWWOW_BUILD_PROGRAM="${POWWOW_BUILD_PROGRAM}")

# the commit of a -dev version, which the launcher shows as powwow does
add_custom_target(gitversion
    COMMAND "${CMAKE_COMMAND}" "-DTOP=${CMAKE_CURRENT_SOURCE_DIR}/.."
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gitversion.h"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/gitversion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/gitversion.h")
add_dependencies(powwow-launcher gitversion)
target_include_directories(powwow-launcher PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(powwow-launcher PRIVATE Qt6::Quick Qt6::QuickControls2)

# the QML plugins that the launcher imports are linked into it only with a
# static Qt; a shared one loads them when it runs, so do not look for them,
# which Qt 6.8 reports as missing targets
if(QT6_IS_SHARED_LIBS_BUILD)
    set_target_properties(powwow-launcher PROPERTIES
        QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()

set_target_properties(powwow-launcher PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME powwow
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.mume.powwow
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${POWWOW_VERSION}"
)

include(GNUInstallDirs)
install(TARGETS powwow-launcher
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
