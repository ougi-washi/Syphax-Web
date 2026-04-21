cmake_minimum_required(VERSION 3.22)

project(syphax_web VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(SYPHAX_WEB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SYPHAX_WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SYPHAX_WEB_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(SYPHAX_WEB_SYPHAX_DIR "${SYPHAX_WEB_VENDOR_DIR}/syphax")

if(NOT EXISTS "${SYPHAX_WEB_SYPHAX_DIR}/s_array.h")
    message(FATAL_ERROR "Missing lib/syphax. Run: git submodule update --init --recursive")
endif()

option(SYPHAX_WEB_BUILD_EXAMPLES "Build syphax-web examples" OFF)
option(SYPHAX_WEB_BUILD_TESTS "Build syphax-web tests" OFF)
option(SYPHAX_WEB_ENABLE_WARNINGS "Enable compiler warnings" ON)

set(SYPHAX_WEB_RUNTIME_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")

function(syphax_web_set_runtime_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SYPHAX_WEB_RUNTIME_OUTPUT_DIR}"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${target} PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${SYPHAX_WEB_RUNTIME_OUTPUT_DIR}"
        )
    endforeach()
endfunction()

set(SYPHAX_WEB_PUBLIC_HEADERS
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_export.h
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_utility.h
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_translator.h
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_html.h
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_js.h
    ${SYPHAX_WEB_INCLUDE_DIR}/sw_server.h
)

set(SYPHAX_WEB_PRIVATE_HEADERS
    ${SYPHAX_WEB_SRC_DIR}/sw_backend.h
    ${SYPHAX_WEB_SRC_DIR}/sw_internal.h
)

set(SYPHAX_WEB_VENDOR_HEADERS
    ${SYPHAX_WEB_SYPHAX_DIR}/s_types.h
    ${SYPHAX_WEB_SYPHAX_DIR}/s_array.h
)

set(SYPHAX_WEB_TRANSLATION_CATALOG
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/translations.json
)

set(SYPHAX_WEB_SOURCES
    ${SYPHAX_WEB_SRC_DIR}/sw_utility.c
    ${SYPHAX_WEB_SRC_DIR}/sw_translator.c
    ${SYPHAX_WEB_SRC_DIR}/sw_html.c
    ${SYPHAX_WEB_SRC_DIR}/sw_js.c
    ${SYPHAX_WEB_SRC_DIR}/sw_server.c
)

if(WIN32)
    list(APPEND SYPHAX_WEB_SOURCES ${SYPHAX_WEB_SRC_DIR}/sw_backend_wsapoll.c)
else()
    list(APPEND SYPHAX_WEB_SOURCES ${SYPHAX_WEB_SRC_DIR}/sw_backend_epoll.c)
endif()

add_library(syphax_web ${SYPHAX_WEB_SOURCES} ${SYPHAX_WEB_PUBLIC_HEADERS} ${SYPHAX_WEB_PRIVATE_HEADERS})
add_library(syphax_web::syphax_web ALIAS syphax_web)

target_compile_features(syphax_web PUBLIC c_std_11)

target_include_directories(syphax_web
    PUBLIC
        $<BUILD_INTERFACE:${SYPHAX_WEB_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${SYPHAX_WEB_VENDOR_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${SYPHAX_WEB_SRC_DIR}
)

target_compile_definitions(syphax_web PRIVATE SYPHAX_WEB_BUILDING_LIBRARY)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(syphax_web PUBLIC SYPHAX_WEB_SHARED_BUILD)
endif()

if(WIN32)
    target_link_libraries(syphax_web PUBLIC ws2_32)
endif()

if(SYPHAX_WEB_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(syphax_web PRIVATE /W4)
    else()
        target_compile_options(syphax_web PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()

set_target_properties(syphax_web PROPERTIES
    OUTPUT_NAME syphax_web
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

source_group(TREE "${SYPHAX_WEB_INCLUDE_DIR}" PREFIX "include" FILES ${SYPHAX_WEB_PUBLIC_HEADERS})
source_group(TREE "${SYPHAX_WEB_SRC_DIR}" PREFIX "src" FILES ${SYPHAX_WEB_PRIVATE_HEADERS} ${SYPHAX_WEB_SOURCES})
source_group(TREE "${SYPHAX_WEB_VENDOR_DIR}" PREFIX "lib" FILES ${SYPHAX_WEB_VENDOR_HEADERS})

if(SYPHAX_WEB_BUILD_EXAMPLES)
    add_executable(syphax_web_static_example examples/static.c)
    target_link_libraries(syphax_web_static_example PRIVATE syphax_web::syphax_web)
    target_compile_features(syphax_web_static_example PRIVATE c_std_11)
    target_compile_definitions(syphax_web_static_example PRIVATE SYPHAX_WEB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(syphax_web_static_example PROPERTIES OUTPUT_NAME syphax_web_static)
    syphax_web_set_runtime_output(syphax_web_static_example)
    if(SYPHAX_WEB_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(syphax_web_static_example PRIVATE /W4)
        else()
            target_compile_options(syphax_web_static_example PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        endif()
    endif()
endif()

if(SYPHAX_WEB_BUILD_TESTS)
    enable_testing()
    add_executable(syphax_web_tests tests/test_suite.c)
    target_link_libraries(syphax_web_tests PRIVATE syphax_web::syphax_web)
    target_compile_features(syphax_web_tests PRIVATE c_std_11)
    target_compile_definitions(syphax_web_tests PRIVATE SYPHAX_WEB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    syphax_web_set_runtime_output(syphax_web_tests)
    if(UNIX AND NOT APPLE)
        target_link_libraries(syphax_web_tests PRIVATE pthread)
    endif()
    if(SYPHAX_WEB_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(syphax_web_tests PRIVATE /W4)
        else()
            target_compile_options(syphax_web_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        endif()
    endif()
    add_test(NAME syphax_web_tests_all COMMAND syphax_web_tests)
    add_test(NAME syphax_web_tests_translator COMMAND syphax_web_tests translator)
    add_test(NAME syphax_web_tests_html_short_api COMMAND syphax_web_tests html_short_api)
    add_test(NAME syphax_web_tests_html_short_macros COMMAND syphax_web_tests html_short_macros)
    add_test(NAME syphax_web_tests_js_short_api COMMAND syphax_web_tests js_short_api)
    add_test(NAME syphax_web_tests_public_short_names COMMAND syphax_web_tests public_short_names)
    add_test(NAME syphax_web_tests_request_helpers COMMAND syphax_web_tests request_helpers)
    add_test(NAME syphax_web_tests_utility_helpers COMMAND syphax_web_tests utility_helpers)
    add_test(NAME syphax_web_tests_live_server COMMAND syphax_web_tests live_server)
endif()

install(TARGETS syphax_web
    EXPORT syphax_webTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${SYPHAX_WEB_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    ${SYPHAX_WEB_SYPHAX_DIR}/s_types.h
    ${SYPHAX_WEB_SYPHAX_DIR}/s_array.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/syphax
)
install(FILES ${SYPHAX_WEB_TRANSLATION_CATALOG}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/syphax_web
)

configure_package_config_file(
    cmake/syphax_webConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/syphax_webConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/syphax_web
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/syphax_webConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT syphax_webTargets
    FILE syphax_webTargets.cmake
    NAMESPACE syphax_web::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/syphax_web
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/syphax_webConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/syphax_webConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/syphax_web
)
