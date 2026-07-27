cmake_minimum_required(VERSION 3.22)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Select the MSVC runtime library." FORCE)

project(gode VERSION 2.3.2 LANGUAGES C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES)
	if(NOT CMAKE_BUILD_TYPE)
		set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build configuration." FORCE)
	endif()
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug;Release;RelWithDebInfo;MinSizeRel")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GODE_RUN_CODEGEN "Run the Python binding generator at configure time" ON)
option(GODE_USE_COMPILER_CACHE "Use sccache or ccache as the C/C++ compiler launcher when available" ON)
option(GODE_UNITY_GENERATED_BINDINGS "Build generated binding sources in CMake unity batches" ON)

set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/godot-cpp" CACHE PATH "Path to godot-cpp")
set(NODE_ADDON_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/node-addon-api" CACHE PATH "Path to node-addon-api")
set(TREE_SITTER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/tree-sitter" CACHE PATH "Path to tree-sitter")
set(TREE_SITTER_TYPESCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third/tree-sitter-typescript" CACHE PATH "Path to tree-sitter-typescript")
set(GODE_EXTENSION_API_JSON "" CACHE FILEPATH "extension_api.json used by the Gode binding generator")
set(GODE_UNITY_GENERATED_BATCH_SIZE "8" CACHE STRING "Number of generated binding sources per unity batch")
set(GODE_TARGET_ARCH "" CACHE STRING "Target architecture override: x64, arm64")

if(NOT GODE_UNITY_GENERATED_BATCH_SIZE MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "GODE_UNITY_GENERATED_BATCH_SIZE must be a positive integer.")
endif()

if(GODE_USE_COMPILER_CACHE)
	find_program(GODE_COMPILER_CACHE_PROGRAM sccache)
	if(NOT GODE_COMPILER_CACHE_PROGRAM)
		find_program(GODE_COMPILER_CACHE_PROGRAM ccache)
	endif()
	if(GODE_COMPILER_CACHE_PROGRAM)
		if(NOT CMAKE_C_COMPILER_LAUNCHER)
			set(CMAKE_C_COMPILER_LAUNCHER "${GODE_COMPILER_CACHE_PROGRAM}" CACHE FILEPATH "C compiler cache launcher" FORCE)
		endif()
		if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
			set(CMAKE_CXX_COMPILER_LAUNCHER "${GODE_COMPILER_CACHE_PROGRAM}" CACHE FILEPATH "C++ compiler cache launcher" FORCE)
		endif()
		message(STATUS "Using compiler cache launcher: ${GODE_COMPILER_CACHE_PROGRAM}")
	else()
		message(STATUS "No compiler cache launcher found; set CMAKE_CXX_COMPILER_LAUNCHER or install sccache/ccache to cache generated bindings.")
	endif()
endif()

if(NOT DEFINED GODOTCPP_TARGET OR GODOTCPP_TARGET STREQUAL "")
	set(_GODE_EFFECTIVE_CONFIG "${CMAKE_BUILD_TYPE}")
	if(NOT _GODE_EFFECTIVE_CONFIG)
		set(_GODE_EFFECTIVE_CONFIG "Debug")
	endif()
	if(_GODE_EFFECTIVE_CONFIG STREQUAL "Debug")
		set(_GODE_DEFAULT_GODOTCPP_TARGET "template_debug")
	else()
		set(_GODE_DEFAULT_GODOTCPP_TARGET "template_release")
	endif()
	set(GODOTCPP_TARGET "${_GODE_DEFAULT_GODOTCPP_TARGET}" CACHE STRING "Which godot-cpp target to generate")
endif()
set_property(CACHE GODOTCPP_TARGET PROPERTY STRINGS "template_debug;template_release;editor")

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

	if(GODE_EXTENSION_API_JSON)
		set(_GODE_EXTENSION_API_JSON "${GODE_EXTENSION_API_JSON}")
	elseif(DEFINED ENV{GODOT_EXTENSION_API_JSON} AND NOT "$ENV{GODOT_EXTENSION_API_JSON}" STREQUAL "")
		set(_GODE_EXTENSION_API_JSON "$ENV{GODOT_EXTENSION_API_JSON}")
	else()
		set(_GODE_EXTENSION_API_JSON "${GODOT_CPP_DIR}/gdextension/extension_api.json")
	endif()
	if(NOT EXISTS "${_GODE_EXTENSION_API_JSON}")
		message(FATAL_ERROR "extension_api.json was not found: ${_GODE_EXTENSION_API_JSON}")
	endif()

	file(GLOB_RECURSE GODE_CODEGEN_PYTHON_INPUTS CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/generator/*.py"
	)
	file(GLOB GODE_CODEGEN_TEMPLATE_INPUTS CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/generator/templates/*.jinja2"
	)
	set(GODE_CODEGEN_INPUTS
		${GODE_CODEGEN_PYTHON_INPUTS}
		${GODE_CODEGEN_TEMPLATE_INPUTS}
		"${CMAKE_CURRENT_SOURCE_DIR}/generator/requirements.txt"
		"${_GODE_EXTENSION_API_JSON}"
	)
	list(SORT GODE_CODEGEN_INPUTS)

	set(_GODE_CODEGEN_FINGERPRINT
		"Python3_EXECUTABLE=${Python3_EXECUTABLE}\n"
		"GODOT_CPP_DIR=${GODOT_CPP_DIR}\n"
		"GODE_EXTENSION_API_JSON=${_GODE_EXTENSION_API_JSON}\n"
	)
	foreach(_GODE_CODEGEN_INPUT IN LISTS GODE_CODEGEN_INPUTS)
		file(SHA256 "${_GODE_CODEGEN_INPUT}" _GODE_CODEGEN_INPUT_SHA)
		string(APPEND _GODE_CODEGEN_FINGERPRINT "${_GODE_CODEGEN_INPUT}:${_GODE_CODEGEN_INPUT_SHA}\n")
	endforeach()
	string(SHA256 _GODE_CODEGEN_INPUT_HASH "${_GODE_CODEGEN_FINGERPRINT}")
	set(_GODE_CODEGEN_STAMP_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/gode-codegen")
	set(_GODE_CODEGEN_STAMP_FILE "${_GODE_CODEGEN_STAMP_DIR}/input.sha256")
	set(_GODE_SHOULD_RUN_CODEGEN TRUE)
	if(EXISTS "${_GODE_CODEGEN_STAMP_FILE}")
		file(READ "${_GODE_CODEGEN_STAMP_FILE}" _GODE_PREVIOUS_CODEGEN_INPUT_HASH)
		if(_GODE_PREVIOUS_CODEGEN_INPUT_HASH STREQUAL _GODE_CODEGEN_INPUT_HASH)
			set(_GODE_SHOULD_RUN_CODEGEN FALSE)
		endif()
	endif()

	if(_GODE_SHOULD_RUN_CODEGEN)
		message(STATUS "Running Gode code generation")
		execute_process(
			COMMAND "${CMAKE_COMMAND}" -E env
				"GODOT_CPP_DIR=${GODOT_CPP_DIR}"
				"GODOT_EXTENSION_API_JSON=${_GODE_EXTENSION_API_JSON}"
				"${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generator/generator.py"
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generator"
			RESULT_VARIABLE GODE_CODEGEN_RESULT
		)
		if(NOT GODE_CODEGEN_RESULT EQUAL 0)
			message(FATAL_ERROR "Gode code generation failed with exit code ${GODE_CODEGEN_RESULT}")
		endif()
		file(MAKE_DIRECTORY "${_GODE_CODEGEN_STAMP_DIR}")
		file(WRITE "${_GODE_CODEGEN_STAMP_FILE}" "${_GODE_CODEGEN_INPUT_HASH}")
	else()
		message(STATUS "Gode code generation inputs unchanged; skipping generator.")
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
set_target_properties(gode_tree_sitter PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
)
target_compile_definitions(gode_tree_sitter PRIVATE
	_POSIX_C_SOURCE=200112L
	_DEFAULT_SOURCE
	_BSD_SOURCE
	_DARWIN_C_SOURCE
)
target_include_directories(gode_tree_sitter PUBLIC
	"${TREE_SITTER_DIR}/lib/include"
	"${TREE_SITTER_DIR}/lib/src"
)

add_library(gode_tree_sitter_typescript STATIC
	"${TREE_SITTER_TYPESCRIPT_DIR}/typescript/src/parser.c"
	"${TREE_SITTER_TYPESCRIPT_DIR}/typescript/src/scanner.c"
)
set_target_properties(gode_tree_sitter_typescript PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
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
if(WIN32)
	set(GODE_LIBNODE_LINK_ITEMS "/WHOLEARCHIVE:${GODE_LIBNODE_FILE}")
elseif(APPLE)
	set(GODE_LIBNODE_LINK_ITEMS "-Wl,-force_load,${GODE_LIBNODE_FILE}")
else()
	set(GODE_LIBNODE_LINK_ITEMS "-Wl,--whole-archive" "${GODE_LIBNODE_FILE}" "-Wl,--no-whole-archive")
endif()
set_target_properties(gode_libnode PROPERTIES
	IMPORTED_LOCATION "${GODE_LIBNODE_FILE}"
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/libnode/include"
)

file(GLOB_RECURSE GODE_MANUAL_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE GODE_GENERATED_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/generated/*.cpp"
)
if(GODE_GENERATED_SOURCES)
	list(REMOVE_ITEM GODE_MANUAL_SOURCES ${GODE_GENERATED_SOURCES})
endif()
set(GODE_SOURCES ${GODE_MANUAL_SOURCES} ${GODE_GENERATED_SOURCES})

set(GODE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/example/addons/gode/binary/${GODE_PLATFORM}/${GODE_ARCH}")

add_library(gode SHARED ${GODE_SOURCES})
set_target_properties(gode PROPERTIES
	PREFIX "lib"
	RUNTIME_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
	LIBRARY_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
	ARCHIVE_OUTPUT_DIRECTORY "${GODE_OUTPUT_DIR}"
)
foreach(_GODE_CONFIG Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${_GODE_CONFIG}" _GODE_CONFIG_UPPER)
	set_target_properties(gode PROPERTIES
		"RUNTIME_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${GODE_OUTPUT_DIR}"
		"LIBRARY_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${GODE_OUTPUT_DIR}"
		"ARCHIVE_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${GODE_OUTPUT_DIR}"
	)
endforeach()
if(GODE_UNITY_GENERATED_BINDINGS AND GODE_GENERATED_SOURCES)
	set_target_properties(gode PROPERTIES
		UNITY_BUILD ON
		UNITY_BUILD_BATCH_SIZE "${GODE_UNITY_GENERATED_BATCH_SIZE}"
	)
	if(GODE_MANUAL_SOURCES)
		set_source_files_properties(${GODE_MANUAL_SOURCES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endif()
	message(STATUS "Generated binding unity build enabled with batch size ${GODE_UNITY_GENERATED_BATCH_SIZE}")
endif()
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
	${GODE_LIBNODE_LINK_ITEMS}
	gode_tree_sitter
	gode_tree_sitter_typescript
	${GODE_LIBNODE_SYSTEM_LIBS}
)

if(MSVC)
	target_compile_options(gode PRIVATE /bigobj /utf-8 /EHsc /Zc:__cplusplus)
endif()
