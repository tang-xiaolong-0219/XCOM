cmake_minimum_required(VERSION 3.21)

project(XCom VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialPort)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/foundation)
add_subdirectory(src/api)
add_subdirectory(src/serial)
add_subdirectory(src/services)
add_subdirectory(src/ui-kit)
add_subdirectory(src/shell)
add_subdirectory(src/app)
add_subdirectory(src/cli)

option(XCOM_BUILD_PLUGINS "Build bundled plugins" ON)
if (XCOM_BUILD_PLUGINS)
    add_subdirectory(plugins/builtin/sessions)
    add_subdirectory(plugins/builtin/checksum)
    add_subdirectory(plugins/builtin/string-hex)
endif()

option(XCOM_BUILD_TESTS "Build integration tests" ON)
if (XCOM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/integration)
endif()
