# ----------------------------------
# Project
# ----------------------------------
cmake_minimum_required(VERSION 3.17)
project(cpp-pinyin VERSION 0.0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ----------------------------------
# Build Options
# ----------------------------------
option(CPP_PINYIN_BUILD_TESTS "Build test cases" ON)
option(CPP_PINYIN_INSTALL "Install library" ON)

# ----------------------------------
# CMake Settings
# ----------------------------------
if (MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

    if (NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif ()
endif ()

if (CPP_PINYIN_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif ()
# ----------------------------------
# Main Project
# ----------------------------------
include_directories(include lib)
file(GLOB_RECURSE _src include/*.h src/*.h src/*.cpp src/*/*.h src/*/*.cpp lib/*.h)
add_library(${PROJECT_NAME} STATIC ${_src})

if (CPP_PINYIN_BUILD_TESTS)
    add_subdirectory(tests)
    set(DIRECTORIES_TO_COPY
            dict
    )

    foreach (dir ${DIRECTORIES_TO_COPY})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND
                ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/res/${dir}
                ${CMAKE_BINARY_DIR}/tests/${dir}
        )
        install(
                DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res/${dir}
                DESTINATION .
        )
    endforeach ()
endif ()

# ----------------------------------
# link libraries
# ----------------------------------
target_include_directories(${PROJECT_NAME} PUBLIC include lib src/util)

# ----------------------------------
# copy dictionary
# ----------------------------------
set(DIRECTORIES_TO_COPY
        dict
)

foreach (dir ${DIRECTORIES_TO_COPY})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/res/${dir}
            ${CMAKE_BINARY_DIR}/bin/${dir}
    )
    install(
            DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res/${dir}
            DESTINATION .
    )
endforeach ()

# ----------------------------------
# install
# ----------------------------------
if (CPP_PINYIN_INSTALL)
    target_include_directories(${PROJECT_NAME} PUBLIC
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )

    install(TARGETS ${PROJECT_NAME}
            EXPORT ${OPENDSPX_INSTALL_NAME}Targets
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
    )
endif ()