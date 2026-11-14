cmake_minimum_required(VERSION 3.16)   # Bullseye ships 3.18

project(myide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Svg SerialPort)   # Svg: themed icons

# ---- QScintilla (no CMake config shipped; locate it next to Qt) ----------
get_filename_component(QT_LIB_DIR "${Qt5_DIR}/../.." ABSOLUTE)   # <prefix>/lib or /usr/lib/<arch>

find_path(QSCINTILLA_INCLUDE_DIR Qsci/qsciscintilla.h
    HINTS ${Qt5Core_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS})
find_library(QSCINTILLA_LIBRARY_RELEASE NAMES qscintilla2_qt5 HINTS "${QT_LIB_DIR}")
find_library(QSCINTILLA_LIBRARY_DEBUG   NAMES qscintilla2_qt5d HINTS "${QT_LIB_DIR}")

if(NOT QSCINTILLA_INCLUDE_DIR OR NOT QSCINTILLA_LIBRARY_RELEASE)
    message(FATAL_ERROR "QScintilla (qscintilla2_qt5) not found. Searched headers in "
                        "${Qt5Core_INCLUDE_DIRS} and libs in ${QT_LIB_DIR}")
endif()

if(WIN32 AND NOT QSCINTILLA_LIBRARY_DEBUG)
    # MSVC debug builds must link the debug library (different CRT).
    message(WARNING "qscintilla2_qt5d.lib not found: Debug builds will fail to link correctly.")
endif()
if(NOT QSCINTILLA_LIBRARY_DEBUG)
    set(QSCINTILLA_LIBRARY_DEBUG "${QSCINTILLA_LIBRARY_RELEASE}")
endif()
message(STATUS "QScintilla: ${QSCINTILLA_INCLUDE_DIR} | ${QSCINTILLA_LIBRARY_RELEASE} | ${QSCINTILLA_LIBRARY_DEBUG}")

# ---- Application -----------------------------------------------------------
add_executable(myide WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindowMenus.cpp
    src/SettingsDialogs.cpp
    src/SettingsDialogs.h
    src/PortComboBox.cpp
    src/PortComboBox.h
    src/ArduinoLexer.h
    src/BoardComboBox.cpp
    src/BoardComboBox.h
    src/Theme.cpp
    src/Theme.h
    src/SerialMonitor.cpp
    src/SerialMonitor.h
    src/ManualControl.cpp
    src/ManualControl.h
    src/SerialCommandGuide.cpp
    src/SerialCommandGuide.h
    src/Portable.cpp
    src/Portable.h
    src/PortableBuilder.cpp
    src/PortableBuilder.h
    src/UsbDrivers.cpp
    src/UsbDrivers.h
    resources/resources.qrc
)

target_include_directories(myide PRIVATE ${QSCINTILLA_INCLUDE_DIR})
target_compile_definitions(myide PRIVATE MYIDE_VERSION="${PROJECT_VERSION}")
target_link_libraries(myide PRIVATE
    Qt5::Widgets
    Qt5::Svg
    Qt5::SerialPort
    optimized ${QSCINTILLA_LIBRARY_RELEASE}
    debug     ${QSCINTILLA_LIBRARY_DEBUG}
)
if(WIN32)
    target_compile_definitions(myide PRIVATE QSCINTILLA_DLL)  # dllimport QScintilla symbols
    # USB driver check (UsbDrivers.cpp): Windows system libraries, not extra dependencies.
    target_link_libraries(myide PRIVATE setupapi cfgmgr32 version)
endif()
if(MSVC)
    target_compile_options(myide PRIVATE /utf-8)   # sources are UTF-8 (default is the ANSI code page)
endif()

# ---- Windows: make the build folder runnable (`cmake --build build --target deploy`)
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${QT_LIB_DIR}/../bin")
    get_filename_component(QSCI_DLL_DIR "${QT_LIB_DIR}/../bin" ABSOLUTE)
    # --printsupport: QScintilla needs Qt5PrintSupport.dll, but windeployqt only
    # scans myide.exe's own imports, so it can't see that dependency.
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime --printsupport
                $<IF:$<CONFIG:Debug>,--debug,--release> "$<TARGET_FILE:myide>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QSCI_DLL_DIR}/qscintilla2_qt5$<$<CONFIG:Debug>:d>.dll" "$<TARGET_FILE_DIR:myide>"
        DEPENDS myide
        COMMENT "Running windeployqt")
endif()
