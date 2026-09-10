cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(CMAKE_CXX_COMPILER    cl.exe)
set(CMAKE_LINKER          link.exe)

set(CMAKE_CXX_FLAGS_INIT    "/nologo /EHsc")

project(WinQLX32_Launcher)

set(SOURCES
    src/injector.cpp
    src/watcher.cpp
    src/main.cpp	
 )

include_directories(include src)

add_executable(launcher ${SOURCES})

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(OFILES
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/launcher.exp"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/launcher.ilk"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/launcher.lib"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/launcher.pdb"
)

add_custom_command(
    TARGET launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${OFILES}
    COMMENT "Cleaning..."
)
