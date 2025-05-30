cmake_minimum_required(VERSION 3.17)
project(cpp-pinyin VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ----------------------------------
# Build Options
# ----------------------------------
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
option(CPP_PINYIN_BUILD_TESTS "Build test cases" OFF)
option(CPP_PINYIN_INSTALL "Install library" ON)

# ----------------------------------
# CMake Settings
# ----------------------------------
if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif ()

if (NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

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
# Project sources and targets
# ----------------------------------
file(GLOB_RECURSE _src include/*.h src/*.h src/*.cpp src/*/*.h src/*/*.cpp)

add_library(${PROJECT_NAME})
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CPP_PINYIN_STATIC)
endif ()
target_compile_definitions(${PROJECT_NAME} PRIVATE CPP_PINYIN_LIBRARY)

target_sources(${PROJECT_NAME} PRIVATE ${_src})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

if (CPP_PINYIN_BUILD_TESTS)
    add_subdirectory(tests)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/res/dict
            ${CMAKE_BINARY_DIR}/tests/dict
    )
endif ()

# ----------------------------------
# Add platform-specific settings
# ----------------------------------
if (WIN32)
    set(RC_DESCRIPTION "A lightweight Chinese/Cantonese to Pinyin library.")
    set(RC_COPYRIGHT "Copyright (C) 2023-2024 wolfgitpr")
    include("cmake/winrc.cmake")
endif ()

# ----------------------------------
# Link libraries
# ----------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE include src src/toneUtil)
target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ----------------------------------
# Copy dictionary files
# ----------------------------------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/res/dict
        ${CMAKE_BINARY_DIR}/bin/dict
)

# ----------------------------------
# Install settings
# ----------------------------------
if (CPP_PINYIN_INSTALL)
    target_include_directories(${PROJECT_NAME} PUBLIC
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    install(TARGETS ${PROJECT_NAME}
            EXPORT ${PROJECT_NAME}Targets
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" OPTIONAL
    )

    install(DIRECTORY include/${PROJECT_NAME}
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # Generate and install package config and version files
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY AnyNewerVersion
    )

    configure_package_config_file(
            ${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}Config.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
            NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )

    install(EXPORT ${PROJECT_NAME}Targets
            FILE ${PROJECT_NAME}Targets.cmake
            NAMESPACE ${PROJECT_NAME}::
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )

    if (NOT DEFINED CPP_PINYIN_VCPKG_DICT_DIR)
        set(dictdir ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/dict)
        install(
                DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res/dict
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}
        )
    else ()
        set(dictdir ${CPP_PINYIN_VCPKG_DICT_DIR}/dict)
        install(
                DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res/dict
                DESTINATION ${CPP_PINYIN_VCPKG_DICT_DIR}
        )
    endif ()
endif ()

# ----------------------------------
# Package config (.pc file) generation
# ----------------------------------
set(version ${PROJECT_VERSION})

# Configure the .pc file
configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cpp-pinyin.pc.in
        ${CMAKE_BINARY_DIR}/cpp-pinyin.pc
        @ONLY
)

# Install the .pc file
install(
        FILES ${CMAKE_BINARY_DIR}/cpp-pinyin.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
