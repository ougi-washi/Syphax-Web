cmake_minimum_required(VERSION 3.22)

project(syphax_web VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/syphax/s_array.h")
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
    include/syphax/web/sw_export.h
    include/syphax/web/sw_types.h
    include/syphax/web/sw_utility.h
    include/syphax/web/sw_translator.h
    include/syphax/web/sw_html.h
    include/syphax/web/sw_server.h
)

set(SYPHAX_WEB_SOURCES
    src/sw_utility.c
    src/sw_translator.c
    src/sw_html.c
    src/sw_server.c
)

if(WIN32)
    list(APPEND SYPHAX_WEB_SOURCES src/sw_backend_wsapoll.c)
else()
    list(APPEND SYPHAX_WEB_SOURCES src/sw_backend_epoll.c)
endif()

add_library(syphax_web ${SYPHAX_WEB_SOURCES} ${SYPHAX_WEB_PUBLIC_HEADERS})
add_library(syphax_web::syphax_web ALIAS syphax_web)

target_compile_features(syphax_web PUBLIC c_std_11)

target_include_directories(syphax_web
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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

if(SYPHAX_WEB_BUILD_EXAMPLES)
    add_executable(syphax_web_static_example examples/static.c)
    target_link_libraries(syphax_web_static_example PRIVATE syphax_web::syphax_web)
    target_compile_features(syphax_web_static_example PRIVATE c_std_11)
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
    add_test(NAME syphax_web_tests_html_builder COMMAND syphax_web_tests html_builder)
    add_test(NAME syphax_web_tests_request_helpers COMMAND syphax_web_tests request_helpers)
    add_test(NAME syphax_web_tests_live_server COMMAND syphax_web_tests live_server)
endif()

install(TARGETS syphax_web
    EXPORT syphax_webTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/syphax DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    lib/syphax/s_types.h
    lib/syphax/s_array.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/syphax
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
