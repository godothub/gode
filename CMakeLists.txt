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
# 注意：libnode.dll 需用户手动编译（cd node && vcbuild.bat dll release nosign）
# -----------------------------------------------------------------------------

set(NODE_ADDON_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/node-addon-api")

if(NOT EXISTS "${NODE_ADDON_API_DIR}/napi.h")
    message(WARNING "node-addon-api headers not found at ${NODE_ADDON_API_DIR}.")
endif()

set(NODE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/node")
set(NODE_INCLUDE_DIRS
    "${NODE_ROOT}/src"
    "${NODE_ROOT}/deps/v8/include"
    "${NODE_ROOT}/deps/uv/include"
)

set(NODE_ROOT_OUT "${NODE_ROOT}/out")
set(NODE_LIB_RELEASE "${NODE_ROOT_OUT}/Release/libnode.lib")
set(NODE_DLL_RELEASE "${NODE_ROOT_OUT}/Release/libnode.dll")
set(NODE_DEF_RELEASE "${NODE_ROOT_OUT}/Release/node.def")
set(NODE_LIB_DEBUG   "${NODE_ROOT_OUT}/Debug/libnode.lib")
set(NODE_DLL_DEBUG   "${NODE_ROOT_OUT}/Debug/libnode.dll")

if(NOT EXISTS "${NODE_LIB_RELEASE}")
    message(FATAL_ERROR
        "未找到 libnode.lib，请先手动编译 Node.js：\n"
        "  cd ${NODE_ROOT}\n"
        "  vcbuild.bat dll release nosign"
    )
endif()

# Debug 回退到 Release
if(EXISTS "${NODE_LIB_DEBUG}")
    set(NODE_LIBRARY "$<$<CONFIG:Debug>:${NODE_LIB_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${NODE_LIB_RELEASE}>")
    set(NODE_DLL_TO_COPY "$<$<CONFIG:Debug>:${NODE_DLL_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${NODE_DLL_RELEASE}>")
else()
    set(NODE_LIBRARY "${NODE_LIB_RELEASE}")
    set(NODE_DLL_TO_COPY "${NODE_DLL_RELEASE}")
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

# Add tree-sitter-typescript
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/typescript/src/parser.c")
    message(STATUS "Initializing tree-sitter-typescript submodule...")
    execute_process(COMMAND git submodule update --init --recursive tree-sitter-typescript WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_library(tree-sitter-typescript STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/typescript/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/typescript/src/scanner.c"
)
target_include_directories(tree-sitter-typescript PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/typescript/src")

add_library(tree-sitter-tsx STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/tsx/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/tsx/src/scanner.c"
)
target_include_directories(tree-sitter-tsx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tree-sitter-typescript/tsx/src")

# -----------------------------------------------------------------------------
# Target: gode
# -----------------------------------------------------------------------------

file(GLOB_RECURSE GODE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

option(GODE_BUILD_GENERATED_BINDINGS "Build generated binding sources" ON)
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
    tree-sitter-typescript
    tree-sitter-tsx
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

# gode 链接 libnode，并确保 node_stub 随之构建
target_link_libraries(gode "${NODE_LIBRARY}")
add_dependencies(gode node_stub)

add_custom_command(TARGET gode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NODE_DLL_TO_COPY}"
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>/libnode.dll"
    COMMENT "复制 libnode.dll 到输出目录"
)

# node.dll — 纯转发存根 DLL，供 win_delay_load_hook 使用。
# win_delay_load_hook.cc 调用 GetModuleHandleA("node.dll") 获取 NAPI 函数地址；
# node.def 每条导出为 "symbol = libnode.symbol"，无 V8 代码，纯转发。
set(NODE_STUB_SRC "${CMAKE_BINARY_DIR}/node_stub_empty.c")
file(WRITE "${NODE_STUB_SRC}" "// Empty stub — all exports are forwarders via node.def\n")

add_library(node_stub SHARED "${NODE_STUB_SRC}")
set_target_properties(node_stub PROPERTIES OUTPUT_NAME "node" PREFIX "")
target_link_options(node_stub PRIVATE
    "/DEF:${NODE_DEF_RELEASE}"
    "/NOENTRY"
    "/NODEFAULTLIB"
)
target_link_libraries(node_stub PRIVATE "${NODE_LIB_RELEASE}")

add_custom_command(TARGET node_stub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:node_stub>
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>/node.dll"
    COMMENT "复制 node.dll (转发存根) 到输出目录"
)

add_custom_command(TARGET gode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:gode>
        "${CMAKE_SOURCE_DIR}/example/addons/gode/bin/$<CONFIG>/libgode.dll"
)
