cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR X86)

set(CMAKE_C_COMPILER      cl.exe)
set(CMAKE_CXX_COMPILER    cl.exe)
set(CMAKE_LINKER          link.exe)

set(CMAKE_C_FLAGS_INIT    "/TC /nologo /D_UNICODE /D_UNICODE_NT /D_WIN32_WINNT=0x0601")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/DLL /MACHINE:X86")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "/MACHINE:X86")

project(WinQLX32 C)


set(PY_PATH "" CACHE PATH "Python external absolute path")

if(NOT PY_PATH)
    message(STATUS "Python Path Not set - assumes NOPY ")
    set(NOPY "ON")
    execute_process(
        COMMAND git describe --long --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MINQLX_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE SCRIPT_ERROR
        RESULT_VARIABLE SCRIPT_RESULT
    )

    string(CONCAT MINQLX_VERSION "${MINQLX_VERSION}" "-nopy") 
    add_compile_definitions(MINQLX_VERSION=\"${MINQLX_VERSION}\")
 

elseif(NOT IS_DIRECTORY "${PY_PATH}")
    message(FATAL_ERROR "Validation Failed: Python Path '${PY_PATH}' does not exist!")
else()
    message(STATUS  "Python installed in: '${PY_PATH}'")

    set(PY_LIB "${PY_PATH}/libs")
    set(PY_INC "${PY_PATH}/include")
    
    execute_process(
        COMMAND "${PY_PATH}/python.exe" "${CMAKE_SOURCE_DIR}/python/version.py"
        OUTPUT_VARIABLE MINQLX_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE SCRIPT_ERROR
        RESULT_VARIABLE SCRIPT_RESULT
    )

    message(STATUS  "minqlx_version: '${MINQLX_VERSION}'")

    add_compile_definitions(MINQLX_VERSION=\"${MINQLX_VERSION}\")
    add_compile_definitions(NDEBUG)
 
endif()

message(STATUS "Build type: '${CMAKE_BUILD_TYPE}'")

 set(SOURCES
    dllmain.c
    hooks.c
    commands.c
    #misc.c
     
    # minqlxtended
    demos.c	
 )

if(NOPY) 
   add_compile_definitions(NOPY)
else()
    list(APPEND SOURCES
        python_embed.c
        python_dispatchers.c
    )
    include_directories(${PY_INC})
endif()

include_directories(include deps/minhook/include)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_library(winqlx SHARED ${SOURCES})

add_subdirectory(launcher)

file(GLOB PY_LIBS "${PY_LIB}/*.lib")

target_link_libraries(winqlx
    PRIVATE
    "${CMAKE_SOURCE_DIR}/deps/lib/Minhook.lib"
    "${PY_LIBS}"
)

if(NOT NOPY) 

    file(GLOB_RECURSE PYTHON_DIR_FILES "${CMAKE_SOURCE_DIR}/python/*")

    add_custom_command(
        OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/minqlx.zip"
        COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/minqlx.zip" --format=zip "."
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/python"
        DEPENDS ${PYTHON_DIR_FILES}
        COMMENT "Compressing python directory into bin/minqlx.zip..."
    )

    add_custom_target(minqlx_zip ALL 
        DEPENDS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/minqlx.zip"
    )

    # 4. Asegurar que se ejecute después de compilar winqlx si fuera necesario
    add_dependencies(minqlx_zip winqlx)
endif()

set(OFILES
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/winqlx.exp"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/winqlx.ilk"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/winqlx.lib"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/winqlx.pdb"
)

add_custom_command(
    TARGET winqlx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${OFILES}
    COMMENT "Cleaning..."
)