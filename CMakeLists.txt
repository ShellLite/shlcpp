# ==============================================================================
# shlcpp Build Configuration (Reproducible Build Baseline)
# Target Version: 0.1.0
# ==============================================================================
cmake_minimum_required(VERSION 3.15)
project(shlcpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_SKIP_INSTALL_RULES TRUE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Quality Gates: Compiler warning configurations
if(MSVC)
    add_compile_options(/W3 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable)
endif()

option(SHL_ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(SHL_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

include(FetchContent)

# ------------------------------------------------------------------------------
# 1. Pinned External Dependencies
# ------------------------------------------------------------------------------

# 1.1 zlib (v1.3.1 - Jan 2024)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
)
FetchContent_MakeAvailable(zlib)
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# 1.2 libcurl (curl-8_7_1 - Mar 2024)
FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_7_1
)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
elseif(APPLE)
    set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(curl)

# 1.3 clip (Clipboard library - Dec 2023)
FetchContent_Declare(
    clip
    GIT_REPOSITORY https://github.com/dacap/clip.git
    GIT_TAG main
)
FetchContent_MakeAvailable(clip)

# 1.4 SDL2 (release-2.30.2 - Apr 2024)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.2
)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# 1.5 Dear ImGui (v1.90.5 - Apr 2024)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.5
)
FetchContent_MakeAvailable(imgui)

# 1.6 cpp-httplib (v0.15.3 - Feb 2024)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
set(HTTPLIB_COMPILE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# 1.7 libzip (v1.10.1 - Aug 2023)
FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG v1.10.1
)
set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libzip)

# 1.8 SQLite3 (Amalgamation)
FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sqlite3)

# ------------------------------------------------------------------------------
# 2. Source Files
# ------------------------------------------------------------------------------
set(SHELL_LITE_SRCS
    shell_lite/main.cpp
    shell_lite/parser/parser_core.cpp
    shell_lite/parser/parser_control_flow.cpp
    shell_lite/parser/parser_functions.cpp
    shell_lite/parser/parser_nlp.cpp
    shell_lite/parser/parser_web_db.cpp
    shell_lite/parser/parser_concurrency.cpp
    shell_lite/parser/parser_imports.cpp
    shell_lite/parser/parser_expressions.cpp
    shell_lite/error/error_reporter.cpp
    shell_lite/concurrency/thread_pool.cpp
    shell_lite/compiler.cpp
    shell_lite/vm.cpp
    shell_lite/objects.cpp
    shell_lite/gc.cpp
    shell_lite/modules/module_core.cpp
    shell_lite/modules/module_csv.cpp
    shell_lite/modules/module_archive.cpp
    shell_lite/modules/module_net.cpp
    shell_lite/modules/module_os.cpp
    shell_lite/modules/module_math.cpp
    shell_lite/modules/module_web.cpp
    shell_lite/modules/module_db.cpp
    shell_lite/modules/module_io.cpp
    shell_lite/modules/module_nlp.cpp
    shell_lite/modules/module_ui.cpp
    shell_lite/ui/ui_subsystem.cpp
)

if(WIN32)
    list(APPEND SHELL_LITE_SRCS shell_lite/pal/platform_win.cpp)
else()
    list(APPEND SHELL_LITE_SRCS shell_lite/pal/platform_posix.cpp)
endif()

set(SOURCES
    ${SHELL_LITE_SRCS}
    shell_lite/lexer.cpp
    shell_lite/gbp_core_logic.cpp
    shell_lite/gbp_chunk_cache.cpp
    shell_lite/value.cpp
    shell_lite/chunk.cpp
    shell_lite/event_loop.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# ------------------------------------------------------------------------------
# 3. Executable and Library Targets
# ------------------------------------------------------------------------------
add_library(shell_lite_core OBJECT ${SOURCES})
set_target_properties(shell_lite_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(shell_lite_core PUBLIC 
    shell_lite
    ${sqlite3_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(shell_lite_core PUBLIC libcurl clip SDL2-static httplib zip SQLite3)
if(WIN32)
    target_link_libraries(shell_lite_core PUBLIC ws2_32 user32 gdi32)
endif()

add_executable(shell_lite_exec $<TARGET_OBJECTS:shell_lite_core>)
target_include_directories(shell_lite_exec PRIVATE 
    shell_lite
    ${sqlite3_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(shell_lite_exec PRIVATE libcurl clip SDL2-static httplib zip SQLite3)

if(WIN32)
    target_link_libraries(shell_lite_exec PRIVATE ws2_32 user32 gdi32)
endif()

add_library(shell_lite_lib SHARED $<TARGET_OBJECTS:shell_lite_core>)
target_include_directories(shell_lite_lib PRIVATE 
    shell_lite
    ${sqlite3_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(shell_lite_lib PRIVATE libcurl clip SDL2-static httplib zip SQLite3)

if(WIN32)
    target_link_libraries(shell_lite_lib PRIVATE ws2_32 user32 gdi32)
endif()