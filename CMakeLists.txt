cmake_minimum_required(VERSION 3.22)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Select the MSVC runtime library." FORCE)

project(gode VERSION 2.4.5 LANGUAGES C CXX)

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
option(GODE_BUILD_EDITOR_EXTENSION "Build the desktop editor-only Gode extension" ON)
option(GODE_BUILD_NODE_HELPER "Build the desktop Node-compatible helper executable used by child_process.fork" ON)

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
	set(GODE_LIBNODE_LINK_ITEMS)
	set(GODE_LIBNODE_LINK_OPTIONS "/WHOLEARCHIVE:${GODE_LIBNODE_FILE}")
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
if(APPLE)
	set(GODE_LIBNODE_LINK_ITEMS "-Wl,-force_load,${GODE_LIBNODE_FILE}")
	set(GODE_LIBNODE_LINK_OPTIONS)
elseif(NOT WIN32)
	set(GODE_LIBNODE_LINK_ITEMS "-Wl,--whole-archive" "${GODE_LIBNODE_FILE}" "-Wl,--no-whole-archive")
	set(GODE_LIBNODE_LINK_OPTIONS)
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

set(GODE_RUNTIME_SOURCES ${GODE_MANUAL_SOURCES} ${GODE_GENERATED_SOURCES})
set(GODE_NODE_HELPER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/gode_node_main.cpp")
set(GODE_NODE_PROBE_HOST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/node_probe_host.cpp")
list(REMOVE_ITEM GODE_RUNTIME_SOURCES "${GODE_NODE_HELPER_SOURCE}")
list(REMOVE_ITEM GODE_RUNTIME_SOURCES "${GODE_NODE_PROBE_HOST_SOURCE}")
set(GODE_EDITOR_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/compiler/typescript_compiler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/compiler/typescript_project_compiler.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/register_editor_types.cpp"
)
foreach(_GODE_EDITOR_SOURCE IN LISTS GODE_EDITOR_SOURCES)
	list(REMOVE_ITEM GODE_RUNTIME_SOURCES "${_GODE_EDITOR_SOURCE}")
endforeach()
list(REMOVE_ITEM GODE_RUNTIME_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/register_runtime_types.cpp"
)
list(APPEND GODE_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/register_runtime_types.cpp")

set(GODE_RUNTIME_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/example/addons/gode/binary/${GODE_PLATFORM}/${GODE_ARCH}")
set(GODE_EDITOR_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/example/addons/gode/binary/editor/${GODE_PLATFORM}/${GODE_ARCH}")
set(GODE_BUILD_EDITOR_EXTENSION_EFFECTIVE OFF)
if(GODE_BUILD_EDITOR_EXTENSION AND (GODE_PLATFORM STREQUAL "windows" OR GODE_PLATFORM STREQUAL "linux" OR GODE_PLATFORM STREQUAL "macos"))
	set(GODE_BUILD_EDITOR_EXTENSION_EFFECTIVE ON)
endif()
set(GODE_BUILD_NODE_HELPER_EFFECTIVE OFF)
if(GODE_BUILD_NODE_HELPER AND (GODE_PLATFORM STREQUAL "windows" OR GODE_PLATFORM STREQUAL "linux" OR GODE_PLATFORM STREQUAL "macos"))
	set(GODE_BUILD_NODE_HELPER_EFFECTIVE ON)
endif()
if(GODE_BUILD_NODE_HELPER_EFFECTIVE)
	list(APPEND GODE_RUNTIME_SOURCES "${GODE_NODE_PROBE_HOST_SOURCE}")
endif()

function(gode_configure_extension_target p_target p_output_dir)
	set_target_properties(${p_target} PROPERTIES
		PREFIX "lib"
		RUNTIME_OUTPUT_DIRECTORY "${p_output_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${p_output_dir}"
		ARCHIVE_OUTPUT_DIRECTORY "${p_output_dir}"
	)
	foreach(_GODE_CONFIG Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER "${_GODE_CONFIG}" _GODE_CONFIG_UPPER)
		set_target_properties(${p_target} PROPERTIES
			"RUNTIME_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${p_output_dir}"
			"LIBRARY_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${p_output_dir}"
			"ARCHIVE_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${p_output_dir}"
		)
	endforeach()
endfunction()

function(gode_configure_executable_target p_target p_output_dir)
	set_target_properties(${p_target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${p_output_dir}"
	)
	foreach(_GODE_CONFIG Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER "${_GODE_CONFIG}" _GODE_CONFIG_UPPER)
		set_target_properties(${p_target} PROPERTIES
			"RUNTIME_OUTPUT_DIRECTORY_${_GODE_CONFIG_UPPER}" "${p_output_dir}"
		)
	endforeach()
endfunction()

add_library(gode_runtime SHARED ${GODE_RUNTIME_SOURCES})
set_target_properties(gode_runtime PROPERTIES OUTPUT_NAME "gode_runtime")
gode_configure_extension_target(gode_runtime "${GODE_RUNTIME_OUTPUT_DIR}")
if(GODE_UNITY_GENERATED_BINDINGS AND GODE_GENERATED_SOURCES)
	set_target_properties(gode_runtime PROPERTIES
		UNITY_BUILD ON
		UNITY_BUILD_BATCH_SIZE "${GODE_UNITY_GENERATED_BATCH_SIZE}"
	)
	if(GODE_MANUAL_SOURCES)
		set_source_files_properties(${GODE_MANUAL_SOURCES} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
	endif()
	message(STATUS "Generated binding unity build enabled with batch size ${GODE_UNITY_GENERATED_BATCH_SIZE}")
endif()
target_include_directories(gode_runtime PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/include/generated"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${NODE_ADDON_API_DIR}"
	"${TREE_SITTER_DIR}/lib/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include"
)
target_compile_definitions(gode_runtime PRIVATE
	NAPI_CPP_EXCEPTIONS
	NAPI_VERSION=10
)
target_link_libraries(gode_runtime PRIVATE
	godot-cpp
	${GODE_LIBNODE_LINK_ITEMS}
	gode_tree_sitter
	gode_tree_sitter_typescript
	${GODE_LIBNODE_SYSTEM_LIBS}
)
target_link_options(gode_runtime PRIVATE ${GODE_LIBNODE_LINK_OPTIONS})
set_property(TARGET gode_runtime APPEND PROPERTY LINK_DEPENDS "${GODE_LIBNODE_FILE}")
if(WIN32)
	set(GODE_WINDOWS_NODE_SHIM_DEF "${CMAKE_CURRENT_BINARY_DIR}/gode_node_api_forwarders.def")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/js_native_api.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/node_api.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/node.h"
	)
	set(_GODE_NODE_API_FORWARDER_SYMBOLS)
	foreach(_GODE_NODE_API_HEADER IN ITEMS
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/js_native_api.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/node_api.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/libnode/include/node.h"
	)
		file(READ "${_GODE_NODE_API_HEADER}" _GODE_NODE_API_HEADER_TEXT)
		string(REGEX MATCHALL [[(napi|node_api)_[A-Za-z0-9_]+\(]] _GODE_NODE_API_HEADER_MATCHES "${_GODE_NODE_API_HEADER_TEXT}")
		foreach(_GODE_NODE_API_HEADER_MATCH IN LISTS _GODE_NODE_API_HEADER_MATCHES)
			string(REPLACE "(" "" _GODE_NODE_API_SYMBOL "${_GODE_NODE_API_HEADER_MATCH}")
			list(APPEND _GODE_NODE_API_FORWARDER_SYMBOLS "${_GODE_NODE_API_SYMBOL}")
		endforeach()
		string(REGEX MATCHALL [[node_module_register\(]] _GODE_NODE_MODULE_HEADER_MATCHES "${_GODE_NODE_API_HEADER_TEXT}")
		foreach(_GODE_NODE_MODULE_HEADER_MATCH IN LISTS _GODE_NODE_MODULE_HEADER_MATCHES)
			string(REPLACE "(" "" _GODE_NODE_MODULE_SYMBOL "${_GODE_NODE_MODULE_HEADER_MATCH}")
			list(APPEND _GODE_NODE_API_FORWARDER_SYMBOLS "${_GODE_NODE_MODULE_SYMBOL}")
		endforeach()
	endforeach()
	if(NOT _GODE_NODE_API_FORWARDER_SYMBOLS)
		message(FATAL_ERROR "No Node-API symbols were found for the Windows node.dll shim.")
	endif()
	list(REMOVE_DUPLICATES _GODE_NODE_API_FORWARDER_SYMBOLS)
	list(SORT _GODE_NODE_API_FORWARDER_SYMBOLS)
	set(_GODE_NODE_API_FORWARDER_DEF "LIBRARY node\nEXPORTS\n")
	foreach(_GODE_NODE_API_SYMBOL IN LISTS _GODE_NODE_API_FORWARDER_SYMBOLS)
		string(APPEND _GODE_NODE_API_FORWARDER_DEF "\t${_GODE_NODE_API_SYMBOL}=libgode_runtime.${_GODE_NODE_API_SYMBOL}\n")
	endforeach()
	file(WRITE "${GODE_WINDOWS_NODE_SHIM_DEF}" "${_GODE_NODE_API_FORWARDER_DEF}")

	if(NOT MSVC)
		message(FATAL_ERROR "The Windows node.dll Node-API forwarder requires the MSVC linker.")
	endif()
	if(GODE_ARCH STREQUAL "arm64")
		set(GODE_WINDOWS_NODE_SHIM_MACHINE "ARM64")
	elseif(GODE_ARCH STREQUAL "x64")
		set(GODE_WINDOWS_NODE_SHIM_MACHINE "X64")
	else()
		message(FATAL_ERROR "Unsupported Windows architecture for node.dll shim: ${GODE_ARCH}")
	endif()
	set(GODE_WINDOWS_NODE_SHIM_IMPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gode-node-shim/$<CONFIG>")
	add_custom_command(TARGET gode_runtime POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:gode_runtime>" "${GODE_WINDOWS_NODE_SHIM_IMPORT_DIR}"
		COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:gode_runtime>/node.dll" "${GODE_WINDOWS_NODE_SHIM_IMPORT_DIR}/node.lib"
		COMMAND "${CMAKE_LINKER}"
			/nologo
			/dll
			/noentry
			/ignore:4001
			"/machine:${GODE_WINDOWS_NODE_SHIM_MACHINE}"
			"/def:${GODE_WINDOWS_NODE_SHIM_DEF}"
			"/out:$<TARGET_FILE_DIR:gode_runtime>/node.dll"
			"/implib:${GODE_WINDOWS_NODE_SHIM_IMPORT_DIR}/node.lib"
		COMMENT "Generating Windows node.dll Node-API forwarder"
		VERBATIM
	)
endif()

if(MSVC)
	target_compile_options(gode_runtime PRIVATE /bigobj /utf-8 /EHsc /Zc:__cplusplus)
endif()

if(GODE_BUILD_NODE_HELPER_EFFECTIVE)
	add_executable(gode_node "${GODE_NODE_HELPER_SOURCE}")
	set_target_properties(gode_node PROPERTIES OUTPUT_NAME "gode_node")
	gode_configure_executable_target(gode_node "${GODE_RUNTIME_OUTPUT_DIR}")
	target_link_libraries(gode_node PRIVATE gode_runtime)
	if(APPLE)
		set_target_properties(gode_node PROPERTIES
			BUILD_WITH_INSTALL_RPATH TRUE
			BUILD_RPATH "@loader_path;@loader_path/../../Frameworks;@loader_path/../../../Frameworks"
			INSTALL_RPATH "@loader_path;@loader_path/../../Frameworks;@loader_path/../../../Frameworks"
		)
	elseif(UNIX)
		set_target_properties(gode_node PROPERTIES
			BUILD_WITH_INSTALL_RPATH TRUE
			BUILD_RPATH "$ORIGIN"
			INSTALL_RPATH "$ORIGIN"
		)
	endif()
	if(MSVC)
		target_compile_options(gode_node PRIVATE /utf-8 /EHsc /Zc:__cplusplus)
	endif()
endif()

if(GODE_BUILD_EDITOR_EXTENSION_EFFECTIVE)
	add_library(gode_editor SHARED ${GODE_EDITOR_SOURCES})
	set_target_properties(gode_editor PROPERTIES OUTPUT_NAME "gode_editor")
	gode_configure_extension_target(gode_editor "${GODE_EDITOR_OUTPUT_DIR}")
	target_include_directories(gode_editor PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${CMAKE_CURRENT_SOURCE_DIR}/include/generated"
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
	)
	target_link_libraries(gode_editor PRIVATE godot-cpp)
	if(MSVC)
		target_compile_options(gode_editor PRIVATE /bigobj /utf-8 /EHsc /Zc:__cplusplus)
	endif()
	set(_GODE_DEFAULT_TARGETS gode_runtime gode_editor)
else()
	set(_GODE_DEFAULT_TARGETS gode_runtime)
endif()
if(GODE_BUILD_NODE_HELPER_EFFECTIVE)
	list(APPEND _GODE_DEFAULT_TARGETS gode_node)
endif()
add_custom_target(gode DEPENDS ${_GODE_DEFAULT_TARGETS})
