cmake_minimum_required(VERSION 3.10..3.20)

project(nifly)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(NIFLY_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NIFLY_TESTS_DIR ${NIFLY_DIR}/tests)
set(NIFLY_TESTS_MY_DIR ${NIFLY_DIR}/tests-my)
set(NIFLY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(NIFLY_EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# 修改编译标记，尝试静态编译（catch2似乎无法通过）
#set(CompilerFlags
#        CMAKE_CXX_FLAGS
#        CMAKE_CXX_FLAGS_DEBUG
#        CMAKE_CXX_FLAGS_RELEASE
#        CMAKE_C_FLAGS
#        CMAKE_C_FLAGS_DEBUG
#        CMAKE_C_FLAGS_RELEASE
#        )

#foreach (CompilerFlag ${CompilerFlags})
#    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
#    message(Flag: "${${CompilerFlag}}")
#endforeach ()

set(NIFLY_VERSION 1.0.0)

# Load automatically VCPKG toolchain if available
#IF(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
#    SET(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
#    IF(NOT DEFINED VCPKG_TARGET_TRIPLET AND DEFINED ENV{VCPKG_DEFAULT_TRIPLET})
#        SET(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
#    ENDIF()
#ENDIF()
message(VCPKG_ROOT: "$ENV{VCPKG_ROOT}")
message(CMAKE_TOOLCHAIN_FILE: "${CMAKE_TOOLCHAIN_FILE}")
message(CMAKE_MODULE_PATH: "${CMAKE_MODULE_PATH}")

add_subdirectory(src)

include(CTest)

message(BUILD_TESTING: "${BUILD_TESTING}")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(tests-my)
endif()
