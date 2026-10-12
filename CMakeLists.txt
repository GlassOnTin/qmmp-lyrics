# Standalone build of the qmmp 2.3.x lyrics plugin against an installed qmmp.
#
# Requires: qt6-base-dev (headers + CMake configs), and the host tools
# (moc/uic/rcc/lrelease) provided by qt6-base-dev-tools / qt6-tools-dev-tools.
#
# Links against the system libqmmp / libqmmpui that qmmp links at runtime.
cmake_minimum_required(VERSION 3.16)

project(qmmp-lyrics VERSION 2.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.2 CONFIG REQUIRED COMPONENTS Widgets Network Test)

# Path to the installed qmmp libraries (Debian/Ubuntu package layout).
set(QMMP_LIB_DIR "/usr/lib/qmmp" CACHE PATH "Directory holding libqmmp and libqmmpui")

# Generate .qm translations next to their .ts files, like the upstream
# top-level CMakeLists does at configure time. Optional: if lrelease is not
# found, translations fall back to English at runtime.
find_program(QT_LRELEASE_EXECUTABLE lrelease HINTS /usr/lib/qt6/bin NO_CACHE)
if(QT_LRELEASE_EXECUTABLE)
    file(GLOB TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.ts")
    foreach(_ts ${TS_FILES})
        get_filename_component(_name "${_ts}" NAME_WE)
        execute_process(
            COMMAND ${QT_LRELEASE_EXECUTABLE} -silent "${_ts}"
                    -qm "${CMAKE_CURRENT_SOURCE_DIR}/translations/${_name}.qm"
            OUTPUT_QUIET)
    endforeach()
endif()

set(lyrics_SRCS
    lyrics.cpp
    lyricsfactory.cpp
    lyricsprovider.cpp
    lyricswidget.cpp
    lyricssettingsdialog.cpp
    ultimatelyricsparser.cpp
    lyricswidget.ui
    lyricssettingsdialog.ui
    translations/translations.qrc
    providers/providers.qrc
)

# <qmmpui/playlistmodel.h> (installed by the qmmp package) includes the private
# header coverloader_p.h, which is not shipped by the package. It is vendored
# under compat/qmmpui to make the installed header usable.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/compat/qmmpui)

add_library(lyrics MODULE ${lyrics_SRCS})
target_link_libraries(lyrics PRIVATE Qt6::Widgets Qt6::Network
    ${QMMP_LIB_DIR}/libqmmpui.so
    ${QMMP_LIB_DIR}/libqmmp.so)

# The harness links the installed plugin .so at runtime, so it only needs Qt.
add_executable(lyrics_test test/test_main.cpp)
target_link_libraries(lyrics_test PRIVATE Qt6::Widgets Qt6::Network Qt6::Test
    ${QMMP_LIB_DIR}/libqmmpui.so
    ${QMMP_LIB_DIR}/libqmmp.so)