cmake_minimum_required(VERSION 3.22)

project(syphax_web VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

set(SYPHAX_WEB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SYPHAX_WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SYPHAX_WEB_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(SYPHAX_WEB_SYPHAX_DIR "${SYPHAX_WEB_VENDOR_DIR}/syphax")

if(NOT EXISTS "${SYPHAX_WEB_SYPHAX_DIR}/s_array.h")
    message(FATAL_ERROR "Missing lib/syphax. Run: git submodule update --init --recursive")
endif()

option(SYPHAX_WEB_BUILD_EXAMPLES "Build syphax-web examples" OFF)
option(SYPHAX_WEB_BUILD_TESTS "Build syphax-web tests" OFF)
option(SYPHAX_WEB_BUILD_STRESS "Build syphax-web stress tools" OFF)
option(SYPHAX_WEB_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(SYPHAX_WEB_ENABLE_TLS "Enable OpenSSL-backed TLS support" OFF)
option(SYPHAX_WEB_ENABLE_CRYPTO "Enable OpenSSL-backed encrypted token support" OFF)

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

function(syphax_web_enable_target_warnings target)
    if(NOT SYPHAX_WEB_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
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

set(SYPHAX_WEB_TRANSLATIONS_FILE
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
    target_link_libraries(syphax_web PUBLIC ws2_32 bcrypt)
else()
    target_link_libraries(syphax_web PUBLIC Threads::Threads)
endif()

if(SYPHAX_WEB_ENABLE_TLS)
    set(SYPHAX_WEB_ENABLE_CRYPTO ON)
endif()

if(SYPHAX_WEB_ENABLE_CRYPTO)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(syphax_web PUBLIC OpenSSL::Crypto)
    target_compile_definitions(syphax_web PUBLIC SYPHAX_WEB_HAS_CRYPTO)
endif()

if(SYPHAX_WEB_ENABLE_TLS)
    target_link_libraries(syphax_web PUBLIC OpenSSL::SSL)
    target_compile_definitions(syphax_web PUBLIC SYPHAX_WEB_HAS_TLS)
endif()

syphax_web_enable_target_warnings(syphax_web)

set_target_properties(syphax_web PROPERTIES
    OUTPUT_NAME syphax_web
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

source_group(TREE "${SYPHAX_WEB_INCLUDE_DIR}" PREFIX "include" FILES ${SYPHAX_WEB_PUBLIC_HEADERS})
source_group(TREE "${SYPHAX_WEB_SRC_DIR}" PREFIX "src" FILES ${SYPHAX_WEB_PRIVATE_HEADERS} ${SYPHAX_WEB_SOURCES})
source_group(TREE "${SYPHAX_WEB_VENDOR_DIR}" PREFIX "lib" FILES ${SYPHAX_WEB_VENDOR_HEADERS})

function(syphax_web_add_example target source output_name)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE syphax_web::syphax_web)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_definitions(${target} PRIVATE SYPHAX_WEB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
    syphax_web_set_runtime_output(${target})
    syphax_web_enable_target_warnings(${target})
endfunction()

if(SYPHAX_WEB_BUILD_EXAMPLES)
    syphax_web_add_example(example_01_http examples/01_http.c 01_http)
    syphax_web_add_example(example_02_https examples/02_https.c 02_https)
    syphax_web_add_example(example_03_static_site examples/03_static_site.c 03_static_site)
    syphax_web_add_example(example_04_live_queue examples/04_live_queue.c 04_live_queue)
    syphax_web_add_example(example_05_folder_app examples/05_folder_app.c 05_folder_app)
    syphax_web_add_example(example_06_session_login examples/06_session_login.c 06_session_login)
endif()

if(SYPHAX_WEB_BUILD_STRESS AND NOT WIN32)
    add_executable(server_stress tools/server_stress.c)
    target_link_libraries(server_stress PRIVATE syphax_web::syphax_web)
    target_compile_features(server_stress PRIVATE c_std_11)
    syphax_web_set_runtime_output(server_stress)
    syphax_web_enable_target_warnings(server_stress)
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
    syphax_web_enable_target_warnings(syphax_web_tests)
    add_test(NAME syphax_web_tests_all COMMAND syphax_web_tests)
    add_test(NAME syphax_web_tests_translator COMMAND syphax_web_tests translator)
    add_test(NAME syphax_web_tests_html_short_api COMMAND syphax_web_tests html_short_api)
    add_test(NAME syphax_web_tests_html_short_macros COMMAND syphax_web_tests html_short_macros)
    add_test(NAME syphax_web_tests_js_short_api COMMAND syphax_web_tests js_short_api)
    add_test(NAME syphax_web_tests_public_short_names COMMAND syphax_web_tests public_short_names)
    add_test(NAME syphax_web_tests_request_helpers COMMAND syphax_web_tests request_helpers)
    add_test(NAME syphax_web_tests_utility_helpers COMMAND syphax_web_tests utility_helpers)
    add_test(NAME syphax_web_tests_live_server COMMAND syphax_web_tests live_server)
    add_test(NAME syphax_web_tests_large_multipart_upload COMMAND syphax_web_tests large_multipart_upload)
    add_test(NAME syphax_web_tests_large_multipart_upload_failure COMMAND syphax_web_tests large_multipart_upload_failure)
    add_test(NAME syphax_web_tests_cookie_helpers COMMAND syphax_web_tests cookie_helpers)
    add_test(NAME syphax_web_tests_session_helpers COMMAND syphax_web_tests session_helpers)
    add_test(NAME syphax_web_tests_token_helpers COMMAND syphax_web_tests token_helpers)
    add_test(NAME syphax_web_tests_server_config COMMAND syphax_web_tests server_config)
    add_test(NAME syphax_web_tests_server_keep_alive_and_limits COMMAND syphax_web_tests server_keep_alive_and_limits)
    add_test(NAME syphax_web_tests_server_loop_shards COMMAND syphax_web_tests server_loop_shards)
    add_test(NAME syphax_web_tests_tls_support COMMAND syphax_web_tests tls_support)
    if(SYPHAX_WEB_BUILD_STRESS AND NOT WIN32)
        add_test(NAME syphax_web_stress_short COMMAND server_stress --mode short --workers 2 --requests 50 --connections 16)
        add_test(NAME syphax_web_stress_idle COMMAND server_stress --mode idle --workers 2 --connections 16 --ms 50)
        add_test(NAME syphax_web_stress_mixed COMMAND server_stress --mode mixed --workers 2 --connections 8 --requests 50)
        add_test(NAME syphax_web_stress_slowloris COMMAND server_stress --mode slowloris --workers 2 --connections 4 --ms 150)
        add_test(NAME syphax_web_stress_backpressure COMMAND server_stress --mode backpressure --workers 2 --requests 8 --large-bytes 131072)
        add_test(NAME syphax_web_stress_spam COMMAND server_stress --mode spam --workers 2 --connections 64 --requests 256 --ms 1000)
    endif()
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
install(FILES ${SYPHAX_WEB_TRANSLATIONS_FILE}
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
