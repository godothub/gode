cmake_minimum_required(VERSION 3.12)
project(gode)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Godot-CPP
# -----------------------------------------------------------------------------

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp submodule not found! Please initialize it.")
endif()

# Add godot-cpp as a subdirectory
set(GODOTCPP_USE_STATIC_CPP OFF CACHE BOOL "Force dynamic runtime" FORCE)
add_subdirectory(godot-cpp)

# -----------------------------------------------------------------------------
# Node-Addon-API & LibNode
# -----------------------------------------------------------------------------

# Path to node-addon-api (header-only wrapper for N-API)
set(NODE_ADDON_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/node-addon-api")

if(NOT EXISTS "${NODE_ADDON_API_DIR}/napi.h")
    message(WARNING "node-addon-api headers not found at ${NODE_ADDON_API_DIR}. Please check path.")
endif()

# Path to Node.js source or install
# If using source, we need headers from src, deps/v8/include, deps/uv/include
set(NODE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/node")
set(NODE_INCLUDE_DIRS
    "${NODE_ROOT}/src"
    "${NODE_ROOT}/deps/v8/include"
    "${NODE_ROOT}/deps/uv/include"
)

# Try to find libnode library
# On Windows, we look for node.lib
if(WIN32)
    # Common locations for prebuilt node.lib or build artifacts
    # You might need to adjust this path or pass -DNODE_LIBRARY=path/to/node.lib
    # Prioritize libnode (DLL import lib) over node (EXE export lib)
    
    set(NODE_ROOT_OUT "${NODE_ROOT}/out")
    
    # Define debug and release library paths (prefer libnode.lib for DLL import lib)
    # Check if libnode.lib exists (usually for shared library build), otherwise fallback to node.lib
    
    if(EXISTS "${NODE_ROOT_OUT}/Debug/libnode.lib")
        set(NODE_LIB_DEBUG "${NODE_ROOT_OUT}/Debug/libnode.lib")
    else()
        set(NODE_LIB_DEBUG "${NODE_ROOT_OUT}/Debug/node.lib")
    endif()

    if(EXISTS "${NODE_ROOT_OUT}/Release/libnode.lib")
        set(NODE_LIB_RELEASE "${NODE_ROOT_OUT}/Release/libnode.lib")
    else()
        set(NODE_LIB_RELEASE "${NODE_ROOT_OUT}/Release/node.lib")
    endif()
    
    if(EXISTS "${NODE_LIB_DEBUG}" AND EXISTS "${NODE_LIB_RELEASE}")
        # If both exist, use generator expression to select based on configuration
        set(NODE_LIBRARY "$<$<CONFIG:Debug>:${NODE_LIB_DEBUG}>$<$<CONFIG:Release>:${NODE_LIB_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${NODE_LIB_RELEASE}>$<$<CONFIG:MinSizeRel>:${NODE_LIB_RELEASE}>")
        message(STATUS "Found both Debug and Release node.lib, will select based on build config.")
    elseif(EXISTS "${NODE_LIB_DEBUG}")
        set(NODE_LIBRARY "${NODE_LIB_DEBUG}")
        message(STATUS "Found only Debug node.lib at ${NODE_LIBRARY}")
    elseif(EXISTS "${NODE_LIB_RELEASE}")
        set(NODE_LIBRARY "${NODE_LIB_RELEASE}")
        message(STATUS "Found only Release node.lib at ${NODE_LIBRARY}")
    endif()

    if(NOT NODE_LIBRARY)
        find_library(NODE_LIBRARY NAMES libnode node
            PATHS
            "${NODE_ROOT_OUT}/Release"
            "${NODE_ROOT_OUT}/Debug"
            NO_DEFAULT_PATH
        )
    endif()
endif()

if(NOT NODE_LIBRARY)
    message(WARNING "libnode library not found. Linking might fail. Please set NODE_LIBRARY manually using -DNODE_LIBRARY=path/to/libnode.")
endif()

#
# -----------------------------------------------------------------------------
# Tree-sitter
# -----------------------------------------------------------------------------

# Add tree-sitter core library
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter/CMakeLists.txt")
    message(STATUS "Initializing tree-sitter submodule...")
    execute_process(COMMAND git submodule update --init --recursive tree-sitter WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Tree-sitter core library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libs" FORCE)
add_subdirectory(tree-sitter)

# Add tree-sitter-javascript
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-javascript/src/parser.c")
    message(STATUS "Initializing tree-sitter-javascript submodule...")
    execute_process(COMMAND git submodule update --init --recursive tree-sitter-javascript WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_library(tree-sitter-javascript STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-javascript/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-javascript/src/scanner.c"
)
target_include_directories(tree-sitter-javascript PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-javascript/src")

# -----------------------------------------------------------------------------
# Target: gode
# -----------------------------------------------------------------------------

file(GLOB_RECURSE GODE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

option(GODE_BUILD_GENERATED_BINDINGS "Build generated binding sources" OFF)
if(NOT GODE_BUILD_GENERATED_BINDINGS)
    list(FILTER GODE_SOURCES EXCLUDE REGEX "/src/generated/.*\\.gen\\.cpp$")
endif()

add_library(gode SHARED ${GODE_SOURCES})

if(MSVC)
    set_property(TARGET gode PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

target_include_directories(gode PRIVATE
    include
    include/generated
    ${NODE_ADDON_API_DIR}
    ${NODE_INCLUDE_DIRS}
)

# Preprocessor definitions
target_compile_definitions(gode PRIVATE
    NAPI_VERSION=8          # Set N-API version
    # NAPI_CPP_EXCEPTIONS   # Enable/Disable C++ exceptions for N-API
    _HAS_EXCEPTIONS=0       # Disable C++ exceptions for Node.js compatibility
    NODE_ADDON_API_DISABLE_CPP_EXCEPTIONS # Explicitly disable N-API C++ exceptions
    NOMINMAX                # Prevent windows.h from defining min/max macros
    WIN32_LEAN_AND_MEAN     # Reduce windows.h include size
)

if(MSVC)
    # Match Node.js default build config (Release mode typically uses /MT)
    # But Godot-CPP typically defaults to /MD (MultiThreadedDLL). 
    # Since we are linking against a prebuilt node.lib (likely /MT in Release), 
    # we might need to force /MT if node_shared=false. 
    # However, if linking against libnode.dll (node_shared=true), we should use /MD.
    # Assuming standard libnode distribution (shared library):
    
    # Force C++20 standard to match Node.js 20+ defaults
    target_compile_options(gode PRIVATE /std:c++20)
    
    # Disable C++ exceptions at compiler level
    target_compile_options(gode PRIVATE /EHs-c-)
endif()

target_link_libraries(gode
    godot::cpp
    tree-sitter
    tree-sitter-javascript
)

if(MINGW)
    # Statically link MinGW runtimes to avoid DLL dependency issues (Error 126)
    target_link_options(
        gode PRIVATE
            -static 
            -static-libgcc 
            -static-libstdc++
    )
    # 显式移除可能由 godot-cpp 或其他地方带入的 MSVC 风格选项（如果需要）
    # 目前 godot-cpp 会根据 MINGW 变量自动处理，这里主要关注 gode 自身的链接
endif()

if(NODE_LIBRARY)
    target_link_libraries(gode ${NODE_LIBRARY})
    
    # Try to find the corresponding DLL
    # Note: This logic is partially redundant with the generator expression above but kept for compatibility
    get_filename_component(NODE_LIB_DIR "${NODE_LIBRARY}" DIRECTORY)
    
    # Only search if NODE_LIBRARY is a real path, not a generator expression
    if(NOT "${NODE_LIBRARY}" MATCHES "\\$<")
        find_file(NODE_DLL NAMES libnode.dll node.dll
            PATHS "${NODE_LIB_DIR}"
            NO_DEFAULT_PATH
        )
    endif()
    
    # Fallback logic for generator expressions where find_file might not work directly
    # If NODE_LIBRARY contains generator expressions, we can't easily extract the dir at config time
    # So we try to guess the DLL path based on the LIB path structure
    
    # Define DLL paths
    set(NODE_DLL_DEBUG "${NODE_ROOT_OUT}/Debug/libnode.dll")
    set(NODE_DLL_RELEASE "${NODE_ROOT_OUT}/Release/libnode.dll")

    # Select DLL based on configuration for debugging
    set(NODE_DLL_TO_COPY "$<$<CONFIG:Debug>:${NODE_DLL_DEBUG}>$<$<CONFIG:Release>:${NODE_DLL_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${NODE_DLL_RELEASE}>$<$<CONFIG:MinSizeRel>:${NODE_DLL_RELEASE}>")

    add_custom_command(TARGET gode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying libnode.dll from: ${NODE_DLL_TO_COPY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NODE_DLL_TO_COPY}"
            "${CMAKE_SOURCE_DIR}/example/addons/gode/bin"
    )
endif()

add_custom_command(TARGET gode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gode>
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/libgode.$<CONFIG>.dll"
)
