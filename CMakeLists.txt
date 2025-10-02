# Syphax-Web - Ougi Washi

## General
cmake_minimum_required(VERSION 3.22.1)

project(SYPHAX_WEB LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

if(NOT CMAKE_GENERATOR)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "Ninja")
        message(STATUS "Ninja build system found. Using Ninja generator.")
    else()
        message(STATUS "Ninja build system not found. Using default generator.")
    endif()
endif()

# Directory setup
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

# setup library macro
macro(setup_library arg_lib_name arg_source_path arg_include_path)
	message(STATUS "Adding ${arg_lib_name}")
	file(GLOB CURRENT_SRC_FILES	"${arg_source_path}/*.c" 
					"${arg_source_path}/*.cpp"
					"${arg_source_path}/**/*.c" 
					"${arg_source_path}/**/*.cpp")
	add_library(${arg_lib_name} STATIC ${CURRENT_SRC_FILES})
	target_include_directories(${arg_lib_name} PUBLIC ${arg_include_path})
endmacro()

# Main module
set(MAIN_MODULE_INCLUDES ${SRC_DIR} ${LIB_DIR})
setup_library(main ${SRC_DIR} "${MAIN_MODULE_INCLUDES}")

# setup executable macro
macro(setup_executable arg_exec_dir arg_exec_name modules)
    message(STATUS "Generating executable ${arg_exec_name}")
    add_executable(${arg_exec_name} "${arg_exec_dir}/${arg_exec_name}.c")
    target_link_libraries(${arg_exec_name} PUBLIC ${modules})
    set_target_properties(${arg_exec_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
endmacro()

setup_executable(${PROJECT_SOURCE_DIR} index main)

