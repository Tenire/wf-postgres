cmake_minimum_required(VERSION 3.10)
project(wf_postgres VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTING "Build test programs" OFF)
option(BUILD_TUTORIAL "Build tutorial programs" ON)

find_package(OpenSSL REQUIRED)

# ---------------------------------------------------------------------------
# Locate Workflow library:
# Supports three clean ways:
# 1. Standard system package: find_package(Workflow)
# 2. Pre-built directory via -DWORKFLOW_DIR=/path/to/workflow
# 3. Sibling directory ../workflow
# ---------------------------------------------------------------------------
if(NOT DEFINED WORKFLOW_DIR)
    set(WORKFLOW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../workflow")
endif()

find_package(Workflow QUIET)

if(Workflow_FOUND)
    set(WORKFLOW_LIB Workflow::workflow)
else()
    find_library(WORKFLOW_LIB NAMES workflow libworkflow.a
                 HINTS "${WORKFLOW_DIR}/_lib" "${WORKFLOW_DIR}/lib"
                 PATH_SUFFIXES lib lib64)
    find_path(WORKFLOW_INCLUDE_DIR NAMES workflow/Workflow.h
              HINTS "${WORKFLOW_DIR}/_include" "${WORKFLOW_DIR}/include"
              PATH_SUFFIXES include)

    if(NOT WORKFLOW_LIB OR NOT WORKFLOW_INCLUDE_DIR)
        message(FATAL_ERROR "Workflow library not found. Build workflow first or pass -DWORKFLOW_DIR=/path/to/workflow")
    endif()

    include_directories(${WORKFLOW_INCLUDE_DIR} ${WORKFLOW_INCLUDE_DIR}/workflow)
endif()

# Core static library
file(GLOB_RECURSE PG_SRC
    "src/factory/*.cc"
    "src/client/*.cc"
    "src/protocol/*.cc"
    "src/auth/*.cc"
)

add_library(wf_postgres STATIC ${PG_SRC})
add_library(wfpg::wf_postgres ALIAS wf_postgres)

target_include_directories(wf_postgres
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/src/auth
        ${CMAKE_CURRENT_SOURCE_DIR}/src/factory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
)

target_link_libraries(wf_postgres
    PUBLIC
        ${WORKFLOW_LIB}
        OpenSSL::SSL
        OpenSSL::Crypto
    PRIVATE
        pthread
)

# Tutorials
if(BUILD_TUTORIAL)
    file(GLOB TUTORIAL_SRCS "tutorial/*.cc")
    foreach(TUTORIAL_SRC ${TUTORIAL_SRCS})
        get_filename_component(TUTORIAL_NAME ${TUTORIAL_SRC} NAME_WE)
        add_executable(${TUTORIAL_NAME} ${TUTORIAL_SRC})
        target_link_libraries(${TUTORIAL_NAME} PRIVATE wf_postgres)
    endforeach()
endif()

# Tests
if(BUILD_TESTING)
    enable_testing()
    file(GLOB TEST_SRCS "test/test_*.cc")
    foreach(TEST_SRC ${TEST_SRCS})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} PRIVATE wf_postgres)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
