# SPDX-License-Identifier: GPL-3.0-or-later
# XPlasma: the msstyles core, renderer, Qt style plugin and tools.
#
# One source tree, built once per Qt major (build.sh does this in podman):
#   cmake -S . -B build/dev-qt6 -G Ninja -DQT_MAJOR=6
#   cmake -S . -B build/dev-qt5 -G Ninja -DQT_MAJOR=5
#
# Only public Qt API is allowed (no *Private targets, no _p.h headers). That is
# what lets a plugin built against the oldest supported Qt of a major load into
# every newer one -- see PLAN.md, D1.
cmake_minimum_required(VERSION 3.16)
project(xplasma LANGUAGES CXX)

set(QT_MAJOR 6 CACHE STRING "Qt major version to build against (5 or 6)")
set_property(CACHE QT_MAJOR PROPERTY STRINGS 5 6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# Hidden by default: the plugin is loaded into every Qt process on the desktop
# and must never interpose on (or be interposed by) another library's symbols.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(QT_MAJOR EQUAL 6)
  find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets)
else()
  find_package(Qt${QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
endif()
set(QT Qt${QT_MAJOR})

# The app (Qt 6 only) and the archive libraries it links: third-party code
# first, before the project's own warning flags.
if(QT_MAJOR EQUAL 6)
  add_subdirectory(src/third_party)
endif()

add_compile_options(-Wall -Wextra)
add_compile_definitions(QT_NO_KEYWORDS QT_USE_QSTRINGBUILDER)

# --- the core: .msstyles in, typed theme data out (QtCore + QtGui only) --------
add_library(xplasma-core STATIC
  src/core/pe.cpp
  src/core/dib.cpp
  src/core/ini.cpp
  src/core/theme.cpp
  src/core/schema.cpp
  src/core/render.cpp
  src/core/themefile.cpp
  src/core/classic.cpp
  src/core/icons.cpp
  src/core/cursors.cpp
)
target_include_directories(xplasma-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xplasma-core PUBLIC ${QT}::Gui)
set_target_properties(xplasma-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- the style plugin ---------------------------------------------------------
add_library(xplasma MODULE
  src/style/plugin.cpp
  src/style/xplasmastyle.cpp
)
# libstdc++ first. Link order decides which library a symbol is bound to, and
# the Qt Company's official Qt 5.15 binaries export sized operator delete
# (_ZdlPvm@Qt_5): linked after Qt, the plugin binds it there and then fails
# to resolve it on every distribution's Qt 5, which doesn't. check-abi.sh
# guards against this whole class.
target_link_libraries(xplasma PRIVATE stdc++ xplasma-core ${QT}::Widgets)
set_target_properties(xplasma PROPERTIES PREFIX "lib" OUTPUT_NAME xplasma)
# Installs where the app looks for the prebuilt plugin (paths::shippedStylePlugin):
#   <prefix>/lib/xplasma/qt<N>/styles/libxplasma.so
install(TARGETS xplasma LIBRARY DESTINATION lib/xplasma/qt${QT_MAJOR}/styles)

# --- the KWin window decoration (Qt 6, where KDecoration3 is installed) -------
# Bound to the KDecoration it's built against, as XPlasma's builds of Plasma's
# widgets are to their Plasma: the Plasma version goes beside it, and apply
# uses it only on that Plasma series (an Aurorae theme otherwise).
if(QT_MAJOR EQUAL 6)
  find_package(KDecoration3 CONFIG QUIET)
  find_package(KF6CoreAddons CONFIG QUIET)
  if(KDecoration3_FOUND AND KF6CoreAddons_FOUND)
    add_library(xplasma-decoration MODULE src/decoration/decoration.cpp)
    target_link_libraries(xplasma-decoration PRIVATE stdc++ xplasma-core KDecoration3::KDecoration KF6::CoreAddons ${QT}::Gui)
    # (KDecoration3's headers are C++20.)
    set_target_properties(xplasma-decoration PROPERTIES PREFIX "" OUTPUT_NAME fyi.hotsocket.xplasma.decoration CXX_STANDARD 20)
    file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fyi.hotsocket.xplasma.decoration.plasma CONTENT "${KDecoration3_VERSION}\n")
    install(TARGETS xplasma-decoration LIBRARY DESTINATION lib/xplasma/decoration)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fyi.hotsocket.xplasma.decoration.plasma DESTINATION lib/xplasma/decoration)
  else()
    message(STATUS "KDecoration3 not found: no native window decoration (apply uses an Aurorae theme)")
  endif()
endif()

# --- tools --------------------------------------------------------------------
add_executable(xplasma-probe src/tools/probe.cpp)
target_link_libraries(xplasma-probe PRIVATE ${QT}::Widgets)

add_executable(xplasma-gallery src/tools/gallery.cpp)
target_link_libraries(xplasma-gallery PRIVATE ${QT}::Widgets)

add_executable(xplasma-render src/tools/render.cpp)
target_link_libraries(xplasma-render PRIVATE stdc++ xplasma-core)

add_executable(xplasma-dump src/tools/dump.cpp)
target_link_libraries(xplasma-dump PRIVATE stdc++ xplasma-core)

# --- the app: CLI, editor and the library under them (Qt 6) ------------------------
if(QT_MAJOR EQUAL 6)
  add_subdirectory(src/app)
  # A single runtime target keeps optional installed plugins in the build set.
  add_custom_target(xplasma-runtime DEPENDS xplasma-cli xplasma-edit xplasma-setup xplasma)
  if(TARGET xplasma-decoration)
    add_dependencies(xplasma-runtime xplasma-decoration)
  endif()
endif()

# --- tests ------------------------------------------------------------------------
add_executable(test-core tests/core/test_core.cpp)
target_link_libraries(test-core PRIVATE stdc++ xplasma-core)
if(QT_MAJOR EQUAL 6)
  add_subdirectory(tests/app)
  add_subdirectory(tests/style)
endif()

# --- fuzzing (fuzz.sh; clang only) --------------------------------------------
option(XPLASMA_FUZZ "Build the libFuzzer targets (needs clang)" OFF)
if(XPLASMA_FUZZ)
  foreach(target theme dib ini scheme icon)
    string(TOUPPER ${target} TARGET_UPPER)
    add_executable(fuzz-${target} tests/core/fuzz.cpp)
    target_compile_definitions(fuzz-${target} PRIVATE FUZZ_TARGET_${TARGET_UPPER}=1)
    target_link_libraries(fuzz-${target} PRIVATE xplasma-core)
    target_link_options(fuzz-${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()

