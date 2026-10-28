cmake_minimum_required(VERSION 3.24)

project(
	FirePots
	VERSION 0.1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "In-source builds are not allowed. Use a preset (cmake --preset release).")
endif()

# ---------------------------------------------------------------------------
# Offline tests first: they need nothing from Skyrim, so they also configure on Linux/macOS
#   ballistics_test  - arc math, angles, gravity fitting
#   arc_preview      - renders the overlay to .ppm images (needs Dear ImGui)
# ---------------------------------------------------------------------------
add_executable(ballistics_test EXCLUDE_FROM_ALL tests/ballistics_test.cpp)
target_include_directories(ballistics_test PRIVATE src)

if(NOT WIN32)
	return()  # the plugin itself only builds on Windows with MSVC
endif()

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (downloaded automatically at configure time), same setup as DaggerfallCombat.
# Builds one DLL that runs on SE (1.5.97) and AE (1.6.x / 1.7.x) via Address Library.
# ---------------------------------------------------------------------------
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)   # prebuilt CommonLib is built with all runtimes
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
	CommonLibSSE
	GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
	GIT_TAG        v14.0.1
	GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

find_package(imgui CONFIG REQUIRED)

set(SOURCES
	src/main.cpp
	src/ArcDraw.cpp
	src/BottleReturn.cpp
	src/Config.cpp
	src/Crafting.cpp
	src/Forms.cpp
	src/LeveledLists.cpp
	src/McmReload.cpp
	src/Overlay.cpp
	src/OverlayD3D.cpp
	src/Thrower.cpp
)

set(HEADERS
	src/PCH.h
	src/ArcDraw.h
	src/ArcState.h
	src/BottleReturn.h
	src/Ballistics.h
	src/Config.h
	src/Crafting.h
	src/Forms.h
	src/LeveledLists.h
	src/McmReload.h
	src/Bombs.h
	src/Overlay.h
	src/OverlayD3D.h
	src/Thrower.h
)

add_commonlibsse_plugin(
	${PROJECT_NAME}
	AUTHOR "akeaneschmidt1998"
	USE_ADDRESS_LIBRARY
	SOURCES ${SOURCES} ${HEADERS}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui d3d11 dxgi)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# These two files must NOT see CommonLib (its WinAPI declarations clash with <Windows.h>).
set_source_files_properties(src/OverlayD3D.cpp src/ArcDraw.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
endif()

# ---------------------------------------------------------------------------
# Output: dist/ is laid out exactly like a mod folder
#   dist/SKSE/Plugins/FirePots.dll + FirePots.ini
# Optionally also copy straight into a mod folder: -DMOD_OUTPUT_DIR="C:/MO2/mods/Fire Pots"
# ---------------------------------------------------------------------------
set(DIST_DIR "${PROJECT_SOURCE_DIR}/dist")
set(MOD_OUTPUT_DIR "" CACHE PATH "Optional mod folder to copy the built plugin into")

add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SKSE/Plugins"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_DIR}/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DIST_DIR}/SKSE/Plugins/"
	VERBATIM
)

if(MOD_OUTPUT_DIR)
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUTPUT_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${MOD_OUTPUT_DIR}/SKSE/Plugins/"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DIST_DIR}/SKSE/Plugins/FirePots.ini" "${MOD_OUTPUT_DIR}/SKSE/Plugins/"
		VERBATIM
	)
endif()

add_executable(arc_preview EXCLUDE_FROM_ALL tests/arc_preview.cpp src/ArcDraw.cpp)
target_include_directories(arc_preview PRIVATE src)
target_link_libraries(arc_preview PRIVATE imgui::imgui)
