cmake_minimum_required(VERSION 3.19)
project(baviewer C ASM)

set(TARGET baviewer)
set(PUBLIC_HDR_DIR include)

if (CMAKE_CROSSCOMPILING OR FILAMENT_IMPORT_PREBUILT_EXECUTABLES)
    include(${IMPORT_EXECUTABLES})
endif()

# ==================================================================================================
# Sources and headers
# ==================================================================================================

set(PUBLIC_HDRS
    include/baviewer/DebugServer.h
    include/baviewer/JsonWriter.h
    include/baviewer/BufferAllocatorInfo.h
)

set(SRCS
    src/ApiHandler.cpp
    src/ApiHandler.h
    src/DebugServer.cpp
    src/JsonWriter.cpp
)

# ==================================================================================================
# Resources
# ==================================================================================================

set(RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(RESOURCE_BINS
        ${CMAKE_CURRENT_SOURCE_DIR}/web/api.js
        ${CMAKE_CURRENT_SOURCE_DIR}/web/app.js
        ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
)

get_resgen_vars(${RESOURCE_DIR} baviewer_resources)

add_custom_command(
        OUTPUT ${RESGEN_OUTPUTS}
        COMMAND resgen -t ${RESGEN_FLAGS} ${RESOURCE_BINS}
        DEPENDS resgen ${RESOURCE_BINS}
)

if (DEFINED RESGEN_SOURCE_FLAGS)
    set_source_files_properties(${RESGEN_SOURCE} PROPERTIES COMPILE_FLAGS ${RESGEN_SOURCE_FLAGS})
endif()

set(DUMMY_SRC "${RESOURCE_DIR}/dummy.c")
add_custom_command(OUTPUT ${DUMMY_SRC} COMMAND echo "//" > ${DUMMY_SRC})

add_library(baviewer_resources ${DUMMY_SRC} ${RESGEN_SOURCE})
set_target_properties(baviewer_resources PROPERTIES FOLDER Libs)

# ==================================================================================================
# Include and target definitions
# ==================================================================================================

include_directories(${PUBLIC_HDR_DIR} ${RESOURCE_DIR})

add_library(${TARGET} STATIC ${PUBLIC_HDRS} ${SRCS})

target_link_libraries(${TARGET} PUBLIC
        civetweb
        baviewer_resources
        utils
)

target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/filament/src)

target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
set_target_properties(${TARGET} PROPERTIES FOLDER Libs)

# ==================================================================================================
# Compiler flags
# ==================================================================================================

if (MSVC)
else()
    target_compile_options(${TARGET} PRIVATE -Wno-deprecated-register)
endif()
