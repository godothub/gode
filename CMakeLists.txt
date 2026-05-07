cmake_minimum_required(VERSION 3.22)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Select the MSVC runtime library." FORCE)

project(gode VERSION 1.6.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GODE_RUN_CODEGEN "Run the Python binding generator at configure time" ON)

set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/godot-cpp" CACHE PATH "Path to godot-cpp")
set(NODE_ADDON_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/node-addon-api" CACHE PATH "Path to node-addon-api")
set(TREE_SITTER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/tree-sitter" CACHE PATH "Path to tree-sitter")
set(TREE_SITTER_JAVASCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/tree-sitter-javascript" CACHE PATH "Path to tree-sitter-javascript")
set(TREE_SITTER_TYPESCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/tree-sitter-typescript" CACHE PATH "Path to tree-sitter-typescript")
set(GODE_TARGET_ARCH "" CACHE STRING "Target architecture override: x64, arm64")
set(GODOTCPP_TARGET "template_release" CACHE STRING "Which godot-cpp target to generate" FORCE)

if(NOT EXISTS "${GODOT_CPP_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "godot-cpp was not found at ${GODOT_CPP_DIR}. Initialize submodules with: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${NODE_ADDON_API_DIR}/napi.h")
	message(FATAL_ERROR "node-addon-api was not found at ${NODE_ADDON_API_DIR}. Initialize submodules with: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${TREE_SITTER_DIR}/lib/include/tree_sitter/api.h")
	message(FATAL_ERROR "tree-sitter was not found at ${TREE_SITTER_DIR}. Initialize submodules with: git submodule update --init --recursive")
endif()

if(GODE_RUN_CODEGEN)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	set(ENV{GODOT_CPP_DIR} "${GODOT_CPP_DIR}")
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generator/generator.py"
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generator"
		RESULT_VARIABLE GODE_CODEGEN_RESULT
	)
	if(NOT GODE_CODEGEN_RESULT EQUAL 0)
		message(FATAL_ERROR "Gode code generation failed with exit code ${GODE_CODEGEN_RESULT}")
	endif()
endif()

add_subdirectory("${GODOT_CPP_DIR}" "${CMAKE_BINARY_DIR}/third/godot-cpp")
if(TARGET godot-cpp)
	set_target_properties(godot-cpp PROPERTIES
		PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	)
endif()

add_library(gode_tree_sitter STATIC
	"${TREE_SITTER_DIR}/lib/src/lib.c"
)
target_include_directories(gode_tree_sitter PUBLIC
	"${TREE_SITTER_DIR}/lib/include"
	"${TREE_SITTER_DIR}/lib/src"
)

add_library(gode_tree_sitter_javascript STATIC
	"${TREE_SITTER_JAVASCRIPT_DIR}/src/parser.c"
	"${TREE_SITTER_JAVASCRIPT_DIR}/src/scanner.c"
)
target_include_directories(gode_tree_sitter_javascript PRIVATE
	"${TREE_SITTER_DIR}/lib/include"
	"${TREE_SITTER_JAVASCRIPT_DIR}/src"
)

add_library(gode_tree_sitter_typescript STATIC
	"${TREE_SITTER_TYPESCRIPT_DIR}/typescript/src/parser.c"
	"${TREE_SITTER_TYPESCRIPT_DIR}/typescript/src/scanner.c"
)
target_include_directories(gode_tree_sitter_typescript PRIVATE
	"${TREE_SITTER_DIR}/lib/include"
	"${TREE_SITTER_TYPESCRIPT_DIR}/typescript/src"
)

if(WIN32)
	set(GODE_PLATFORM "windows")
elseif(ANDROID)
	set(GODE_PLATFORM "android")
elseif(IOS)
	set(GODE_PLATFORM "ios")
elseif(APPLE)
	set(GODE_PLATFORM "macos")
else()
	set(GODE_PLATFORM "linux")
endif()

if(GODE_TARGET_ARCH)
	set(GODE_ARCH "${GODE_TARGET_ARCH}")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
	set(GODE_ARCH "arm64")
else()
	set(GODE_ARCH "x64")
endif()

add_library(gode_libnode STATIC IMPORTED GLOBAL)
if(GODE_PLATFORM STREQUAL "windows")
	set(GODE_LIBNODE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libnode/windows/${GODE_ARCH}/libnode.lib")
	set(GODE_LIBNODE_SYSTEM_LIBS Dbghelp Psapi Winmm Ws2_32 Advapi32 Crypt32 Ole32 Iphlpapi Shell32 User32 Userenv Uuid)
elseif(GODE_PLATFORM STREQUAL "linux")
	set(GODE_LIBNODE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libnode/linux/${GODE_ARCH}/libnode.a")
	set(GODE_LIBNODE_SYSTEM_LIBS dl pthread rt)
elseif(GODE_PLATFORM STREQUAL "macos")
	set(GODE_LIBNODE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libnode/macos/${GODE_ARCH}/libnode.a")
	set(GODE_LIBNODE_SYSTEM_LIBS "-framework CoreFoundation" "-framework Security")
elseif(GODE_PLATFORM STREQUAL "android")
	set(GODE_LIBNODE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libnode/android/${GODE_ARCH}/libnode.a")
	set(GODE_LIBNODE_SYSTEM_LIBS log dl)
elseif(GODE_PLATFORM STREQUAL "ios")
	set(GODE_LIBNODE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/libnode/ios/${GODE_ARCH}/libnode.a")
	set(GODE_LIBNODE_SYSTEM_LIBS "-framework Foundation")
else()
	message(FATAL_ERROR "Unsupported Gode platform: ${GODE_PLATFORM}")
endif()

if(NOT EXISTS "${GODE_LIBNODE_FILE}")
	message(FATAL_ERROR "libnode binary was not found: ${GODE_LIBNODE_FILE}")
endif()
set_target_properties(gode_libnode PROPERTIES
	IMPORTED_LOCATION "${GODE_LIBNODE_FILE}"
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/libnode/include"
)

file(GLOB_RECURSE GODE_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

set(GODE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/example/addons/gode/binary/${GODE_PLATFORM}/${GODE_ARCH}")

add_library(gode SHARED ${GODE_SOURCES})
set_target_properties(gode PROPERTIES
	PREFIX "lib"
	RUNTIME_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
	LIBRARY_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
	ARCHIVE_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
)
target_include_directories(gode PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/include/generated"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${NODE_ADDON_API_DIR}"
	"${TREE_SITTER_DIR}/lib/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include"
)
target_compile_definitions(gode PRIVATE
	NAPI_CPP_EXCEPTIONS
	NAPI_VERSION=10
)
target_link_libraries(gode PRIVATE
	godot-cpp
	gode_libnode
	gode_tree_sitter
	gode_tree_sitter_javascript
	gode_tree_sitter_typescript
	${GODE_LIBNODE_SYSTEM_LIBS}
)

if(MSVC)
	target_compile_options(gode PRIVATE /bigobj /utf-8 /EHsc /Zc:__cplusplus)
endif()
