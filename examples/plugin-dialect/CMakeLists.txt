cmake_minimum_required(VERSION 3.17)
project(cpp-pinyin2-plugin-dialect LANGUAGES CXX)

# A dialect written the way a THIRD PARTY writes one: this project knows nothing about the cpp-pinyin2
# source tree, it calls find_package and links the imported target. It builds against an installed tree
# and nothing else:
#
#   cmake -S examples/plugin-dialect -B <build> -DCMAKE_PREFIX_PATH=<prefix>
#
# tests/install/check_install_consumer.cmake does exactly that -- against a scratch prefix, on every
# run -- so "a third party can add a dialect" is an executed check, not a paragraph in a document.
#
# It is kept out of the in-tree examples on purpose: inside this build every header is already on the
# include path and every target is already defined, which is precisely the state that hides a missing
# install rule or a public header that includes something that is never installed.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cpp-pinyin2 2.0 CONFIG REQUIRED)

add_executable(plugin_dialect plugin_dialect.cpp)
target_link_libraries(plugin_dialect PRIVATE cpp-pinyin2::cpp-pinyin2)
