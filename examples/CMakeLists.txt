cmake_minimum_required(VERSION 3.17)
project(cpp-pinyin2-examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(mandarin_example mandarin_example.cpp)
# cpp-pinyin2_dev_flags carries the same warning set (and, when CPP_PINYIN2_SANITIZE is ON, the sanitizers) as
# the library, the tools and the tests. The example was the one target that did not have it, so it used to be
# the only C++ code in the tree compiled without the project's own warning set.
target_link_libraries(mandarin_example PRIVATE cpp-pinyin2::cpp-pinyin2 cpp-pinyin2_dev_flags)

# Same fix as tests/CMakeLists.txt:50-83, for the same measured reason: a POST_BUILD copy only runs
# when the executable *relinks*, so editing a dictionary leaves a stale copy in the build tree that the
# example then loads without complaining. The copy is unconditional here too -- copy_directory costs
# about 15 ms for the 5 MB of assets, which is cheaper than trusting a stamp.
#
# Destination is the build's bin/ directory, which matches how the example finds its data: it looks for
# "dict" relative to the *current* directory (mandarin_example.cpp:6), so run it from bin/.
add_custom_target(CopyExampleAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data/dialects
            ${CMAKE_BINARY_DIR}/bin/dict
    COMMENT "Copying dictionaries next to the example")
add_dependencies(mandarin_example CopyExampleAssets)

# The six-line path from AC-1 (docs/DECISIONS.md), as a separate, buildable program.
#
# It does NOT depend on CopyExampleAssets and does not read "dict" relative to the current directory:
# the dictionary root arrives as argv[1]. The CTest entry below passes the build's bin/dict explicitly,
# so the sample is a program a test runs -- not a code block a document claims compiles.
add_executable(MinimalUsage minimal_usage.cpp)
target_link_libraries(MinimalUsage PRIVATE cpp-pinyin2::cpp-pinyin2 cpp-pinyin2_dev_flags)

# Run the sample, don't just build it -- but only when the dictionaries are where it can find them.
#
# The dictionary tree at <build>/bin/dict is populated by tests/CMakeLists.txt's CopyTestAssets, i.e.
# whenever CPP_PINYIN2_BUILD_TESTS is ON. A build with tests OFF has no dictionary to point the sample
# at, so registering the entry there would fail for a reason that has nothing to do with the sample,
# and registering it as "expected to fail" would hide the real failure. So the entry is conditional and
# says which way it went; the always-on half of AC-1 lives in tests/cpp/test_minimal_usage.cpp, which
# counts the six lines in this file and runs in every configuration.
# The condition tests the OPTION, not the directory. It used to be
# `if (EXISTS "${CMAKE_BINARY_DIR}/bin/dict")`, which is false on every fresh configure (the directory
# is created at build time by tests/CMakeLists.txt's CopyTestAssets, and tests/ is processed before
# examples/) and true only on a tree that had already been built once -- so a clean CI checkout never
# got this entry while a developer's second configure did. CPP_PINYIN2_BUILD_TESTS is the real
# precondition: it is what enables CopyTestAssets, i.e. what puts dict/ under bin/.
if (CPP_PINYIN2_BUILD_TESTS)
    add_test(NAME MinimalUsage COMMAND MinimalUsage "${CMAKE_BINARY_DIR}/bin/dict")
    # Labelled so it is selectable (`ctest -L ac1`) and so tools/check_gate_inventory.py, which FAILS on
    # an unlabelled entry, stays green on a build with examples on -- a configuration nobody had run that
    # tool against until the pre-delivery checklist did.
    set_tests_properties(MinimalUsage PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
                                                LABELS "ac1")
    message(STATUS "MinimalUsage: registered as a CTest entry against ${CMAKE_BINARY_DIR}/bin/dict")
else ()
    message(STATUS "MinimalUsage: CTest entry not registered (CPP_PINYIN2_BUILD_TESTS is OFF, so "
                   "bin/dict is never populated; -DCPP_PINYIN2_BUILD_TESTS=ON enables both)")
endif ()
