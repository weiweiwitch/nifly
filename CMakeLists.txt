cmake_minimum_required(VERSION 3.10..3.20)

# 这行是我自己的配置
set(CMAKE_TOOLCHAIN_FILE D:/nerv/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake)

project(nifly)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(NIFLY_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NIFLY_TESTS_DIR ${NIFLY_DIR}/tests)
set(NIFLY_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(NIFLY_EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# 这段是我自己的配置
set(CompilerFlags
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        )
foreach (CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
    message(Flag: "${${CompilerFlag}}")
endforeach ()

set(NIFLY_VERSION 1.0.0)

add_subdirectory(src)

include(CTest)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
