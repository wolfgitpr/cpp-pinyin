cmake_minimum_required(VERSION 3.17)

# Verification devices. Built only when CPP_PINYIN2_BUILD_TESTS is on, because they depend on the
# test corpus under tests/data/verified/source and on data/dialects, both of which live in this repository.

# ---- Strong equivalence gate (docs/REQUIREMENTS.md §2.2 / protocol P1) ----
add_executable(DumpEquivalence dump_equivalence.cpp)
target_link_libraries(DumpEquivalence PRIVATE cpp-pinyin2::cpp-pinyin2)
target_compile_features(DumpEquivalence PRIVATE cxx_std_17)

# Frozen reference: the digest of the full dump (4 datasets x includeCandidates{false,true}).
#
# THIS BLOCK IS THE SINGLE AUTHORITY for all three numbers -- digest, entry count, byte count. A
# script or a document that needs one of them reads it here. A copy is a second authority, and the
# copy is never the one that gets updated: the board's baseline script used to carry its own
# entry/byte count while this file carried the digest, so one freeze lived in two places and nothing
# could notice a half-edit. The gate script now reads all three from here and prints what it read.
#
# Re-recorded 2026-10-01: 5,384,712 entries / 115,666,595 bytes / 820c880f...
#
# Why it changed: the previous value (b871e3b7... / 115,712,228 B) was frozen on 2026-09-23 and
# stopped describing HEAD when data/dialects changed in e03a772 ("Adopt Mandarin phrase corrections:
# polyphone 95.52% -> 96.52%", 2026-09-29). The gate had been failing since that commit -- in every
# environment, not just one machine -- while the docs still described it as green.
#
# The byte format did NOT change, and the old value stays reproducible from that revision's inputs:
#   git worktree add <tmp> 8e992bc^
#   dump_equivalence <tmp>/data/dialects <tmp>/tests/data/verified/source --expect b871e3b7... --quiet
# Dated records quoting the old digest/byte count: docs/HISTORY.md, docs/PERFORMANCE.md
# (§1003, §1259), docs/BACKLOG.md, docs/REQUIREMENTS.md §3.3. They stay as history -- the CURRENT
# value exists only here, so a document that quotes a byte count must say which era it belongs to.
#
# CACHE STRING, so an existing build tree keeps its old value: after updating it here, re-run
#   cmake -B <build> -U CPP_PINYIN2_FROZEN_DUMP_SHA256
# or start from a fresh build directory. (Hit for real on 2026-10-01: the value above was edited
# and the gate still reported the old expectation.)
set(CPP_PINYIN2_FROZEN_DUMP_SHA256
    "820c880ff5b38680087ed079c0a929690d9384891727754f7800f5d0bcc9c9b2"
    CACHE STRING "Expected SHA-256 of the full equivalence dump")

# The two record numbers are plain set(), not cache entries: CTest does not consume them. They are
# here so that the board's baseline script reads the same freeze the digest lives in.
set(CPP_PINYIN2_FROZEN_DUMP_ENTRIES 5384712)
set(CPP_PINYIN2_FROZEN_DUMP_BYTES 115666595)

# Hash-only by default (no 115 MB file on disk); pass --out to keep the dump for diffing.
add_test(NAME DumpEquivalence
    COMMAND DumpEquivalence
    ${CMAKE_SOURCE_DIR}/data/dialects
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/data/verified/source
    --expect ${CPP_PINYIN2_FROZEN_DUMP_SHA256}
    --quiet)
set_tests_properties(DumpEquivalence PROPERTIES LABELS "slow" TIMEOUT 1800)

# The tool's own SHA-256 must be right, or every "P1 passed" is worthless.
add_test(NAME DumpEquivalenceSelfTest COMMAND DumpEquivalence --self-test)
set_tests_properties(DumpEquivalenceSelfTest PROPERTIES LABELS "slow")   # self-test of the slow equivalence entry

add_executable(LoadCheck load_check.cpp)
target_link_libraries(LoadCheck PRIVATE cpp-pinyin2::cpp-pinyin2)
target_compile_features(LoadCheck PRIVATE cxx_std_17)

# Same strict set as the library (defined once in the root CMakeLists.txt).
# The development flag sets (warnings, and the sanitizers when CPP_PINYIN2_SANITIZE is ON) travel together
# through one aggregator target, so adding a set does not mean editing every call site.
target_link_libraries(DumpEquivalence PRIVATE cpp-pinyin2_dev_flags)
target_link_libraries(LoadCheck PRIVATE cpp-pinyin2_dev_flags)
