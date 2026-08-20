cmake_minimum_required(VERSION 3.19)
project(uboviewer C ASM)

set(TARGET uboviewer)
set(PUBLIC_HDR_DIR include)

if (CMAKE_CROSSCOMPILING OR FILAMENT_IMPORT_PREBUILT_EXECUTABLES)
    include(${IMPORT_EXECUTABLES})
endif()

set(PUBLIC_HDRS
        include/uboviewer/DebugServer.h
        include/uboviewer/UboInfo.h
)

set(SRCS
        src/ApiHandler.cpp
        src/ApiHandler.h
        src/DebugServer.cpp
)

set(RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(RESOURCE_BINS
        ${CMAKE_CURRENT_SOURCE_DIR}/web/app.js
        ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
)

get_resgen_vars(${RESOURCE_DIR} uboviewer_resources)

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

add_library(uboviewer_resources ${DUMMY_SRC} ${RESGEN_SOURCE})
set_target_properties(uboviewer_resources PROPERTIES FOLDER Libs)

include_directories(${PUBLIC_HDR_DIR} ${RESOURCE_DIR})

add_library(${TARGET} STATIC ${PUBLIC_HDRS} ${SRCS})
target_link_libraries(${TARGET} PUBLIC
        civetweb
        uboviewer_resources
        utils
)
target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
set_target_properties(${TARGET} PROPERTIES FOLDER Libs)

if (NOT MSVC)
    target_compile_options(${TARGET} PRIVATE -Wno-deprecated-register)
endif()

if (FILAMENT_BUILD_TESTING AND NOT IOS AND NOT WASM)
    add_executable(test_uboviewer test/test_DebugServer.cpp)
    target_link_libraries(test_uboviewer PRIVATE gtest uboviewer)
    target_compile_options(test_uboviewer PRIVATE ${COMPILER_FLAGS})
    set_target_properties(test_uboviewer PROPERTIES FOLDER Tests)
endif()

set(UBOVIEWER_DEPS
        uboviewer
        uboviewer_resources
        civetweb
)
set(UBOVIEWER_COMBINED_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libuboviewer_combined.a")
combine_static_libs(uboviewer "${UBOVIEWER_COMBINED_OUTPUT}" "${UBOVIEWER_DEPS}")

set(UBOVIEWER_LIB_NAME ${CMAKE_STATIC_LIBRARY_PREFIX}uboviewer${CMAKE_STATIC_LIBRARY_SUFFIX})
install(FILES "${UBOVIEWER_COMBINED_OUTPUT}" DESTINATION lib/${DIST_DIR}
        RENAME ${UBOVIEWER_LIB_NAME})
