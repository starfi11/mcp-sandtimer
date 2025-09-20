cmake_minimum_required(VERSION 3.20)

project(mcp-sandtimer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ON)

configure_file(
    include/mcp_sandtimer/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/mcp_sandtimer/Version.h
    @ONLY
)

add_library(mcp_sandtimer_lib STATIC
    src/MCPSandTimerServer.cpp
    src/TimerClient.cpp
    src/Json.cpp
    src/ToolDefinition.cpp
)

add_library(mcp_sandtimer::lib ALIAS mcp_sandtimer_lib)

set_target_properties(mcp_sandtimer_lib PROPERTIES
    OUTPUT_NAME mcp_sandtimer
)

target_include_directories(mcp_sandtimer_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
        $<INSTALL_INTERFACE:include/mcp_sandtimer>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(mcp_sandtimer_lib
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
            include/mcp_sandtimer/MCPSandTimerServer.h
            include/mcp_sandtimer/TimerClient.h
            include/mcp_sandtimer/Json.h
            include/mcp_sandtimer/ToolDefinition.h
            ${CMAKE_CURRENT_BINARY_DIR}/generated/mcp_sandtimer/Version.h
)

if (WIN32)
    target_link_libraries(mcp_sandtimer_lib PRIVATE ws2_32)
endif()

add_executable(mcp-sandtimer src/main.cpp)

target_link_libraries(mcp-sandtimer PRIVATE mcp_sandtimer_lib)

include(GNUInstallDirs)

install(TARGETS mcp-sandtimer mcp_sandtimer_lib
    EXPORT mcp_sandtimerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT mcp_sandtimerTargets
    FILE mcp_sandtimerTargets.cmake
    NAMESPACE mcp_sandtimer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mcp_sandtimer
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "mcp-sandtimer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_SYSTEM_NAME "windows-x86_64")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")
include(CPack)

if (BUILD_TESTING)
    include(CTest)
    add_executable(tool_definition_test tests/tool_definition_test.cpp)
    target_link_libraries(tool_definition_test PRIVATE mcp_sandtimer_lib)
    add_test(NAME ToolDefinitions COMMAND tool_definition_test)
endif()
