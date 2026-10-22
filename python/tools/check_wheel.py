#!/usr/bin/env python3
"""Check a built wheel against the source tree it was made from.

Two failure families, both of them silent until somebody looks:

* an artifact that is **missing** something -- a dictionary, the recorded native library name;
* an artifact that carries **more** than the source. setuptools reuses ``python/build/lib`` as its
  staging area, so a module deleted from the source can survive there and be shipped again.

The second family is why the packaged members are compared as a set against the source tree rather
than asked "do you contain X?", and why every packaged file's bytes are compared with its source
file: a stale copy of a module that still exists passes a set comparison and fails a byte
comparison. It is also why the whole thing is a script that can be pointed at an arbitrary wheel
instead of a few assertions inside the test suite.

    python python/tools/check_wheel.py python/dist/cpp_pinyin2-2.0.0-py3-none-win_amd64.whl
    python python/tools/check_wheel.py --self-test

Exit code 0 means every check passed. ``--self-test`` proves the checker can fail: it builds
synthetic wheels (one faithful, the rest damaged in one specific way each) in a temporary
directory and asserts the verdicts, because a checker that quietly stopped comparing looks
exactly like a passing gate.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import sysconfig
import tempfile
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

ROOT = Path(__file__).resolve().parents[2]
PKG = ROOT / "python" / "cpp_pinyin2"

#: Hardcoded, exactly like python/tools/build_native.py: a stray directory must not be shipped.
DIALECT_WHITELIST = ("mandarin", "cantonese")
REQUIRED_TABLES = ("word.txt", "phrases_dict.txt", "phrases_map.txt", "trans_word.txt")

PLATFORM_TAG_ENV = "CPP_PINYIN_PLATFORM_TAG"

#: The Python floor has ONE authority: `python_requires` in python/setup.py, which is what setuptools writes
#: into the wheel's METADATA. This file reads it from there instead of repeating the number, and fails loudly
#: when the file stops stating it -- a second copy of a version floor is exactly the drift this repository
#: forbids elsewhere, and it stays invisible until a wheel ships with a floor nobody chose (measured: the
#: floor moved twice -- 3.9 -> 3.8 in round 28 and back to 3.9 on 2026-10-10 -- and each time a hand-edited
#: constant here would have been the only thing standing between the tree and the wheel).
#: Parsed, not imported: setup.py calls setup() at import time, which is not something a checker should run.
SETUP_PY = PKG.parent / "setup.py"
SETUP_PYTHON_REQUIRES = re.compile(r"""python_requires\s*=\s*["']([^"']+)["']""")


def declared_python_requires(setup_py: Optional[Path] = None) -> str:
    """The floor as `setup.py` states it, e.g. `>=3.9`."""
    path = SETUP_PY if setup_py is None else setup_py
    if not path.is_file():
        raise SystemExit(f"FATAL: {path} is missing; it is the only authority for the Python floor.")
    match = SETUP_PYTHON_REQUIRES.search(path.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(
            f"FATAL: {path} states no python_requires. The wheel's Requires-Python would then be absent, "
            "and absent reads as 'installs on every interpreter'."
        )
    return match.group(1)


def expected_requires_python(setup_py: Optional[Path] = None) -> str:
    """The METADATA line a wheel built from this tree must carry."""
    return f"Requires-Python: {declared_python_requires(setup_py)}"


#: The manylinux policy line this project targets (plan section 6.5, items 1 and 1b). This scan is a report,
#: not a certification: auditwheel compares symbols against manylinux-policy.json, and that stays the
#: authority. What it does catch is the difference between a container build and a build on a host with a
#: newer glibc, which is the mistake the plan explicitly forbids -- a wheel that needs glibc 2.39 installs
#: fine on the machine that built it and nowhere else.
MAX_GLIBC = (2, 28)
GLIBC_REFERENCE = re.compile(rb"GLIBC_(\d+)\.(\d+)")
ELF_MAGIC = b"\x7fELF"

#: The C++ runtime is linked *into* the library on purpose (plan section 6.5 item 4), so the library must not
#: also need it at load time. Whether it does is a property of the dynamic section, and asking the raw bytes
#: instead gets it wrong: measured on the board on 2026-10-02 with gcc 14.2.1, a correctly static-linked
#: object still contained one GLIBCXX_ literal, so a byte scan failed a good wheel -- and told the reader to
#: add -static-libstdc++/-static-libgcc, which that build already had. DT_NEEDED is the authority here: a
#: library can only need GLIBCXX_ version symbols if it names the library that defines them.
CPP_RUNTIME_SONAMES = ("libstdc++.so", "libgcc_s.so")
PT_LOAD = 1
PT_DYNAMIC = 2
DT_NEEDED = 1
DT_STRTAB = 5
DT_STRSZ = 10


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def expected_source_files(root: Optional[Path] = None) -> Dict[str, Path]:
    """Packaged member name -> the source-tree file it must be a copy of."""
    pkg = (ROOT if root is None else root) / "python" / "cpp_pinyin2"
    for required_dir in (pkg / "_native", *(pkg / "data" / "dict" / d for d in DIALECT_WHITELIST)):
        if not required_dir.is_dir():
            raise SystemExit(
                f"FATAL: {required_dir} is missing. Run python/tools/build_native.py first: it "
                "stages the native library and the whitelisted dictionaries into the package."
            )
    files: Dict[str, Path] = {}
    for module in sorted(pkg.glob("*.py")):
        files[f"cpp_pinyin2/{module.name}"] = module
    for entry in sorted((pkg / "_native").iterdir()):
        if entry.is_file():
            files[f"cpp_pinyin2/_native/{entry.name}"] = entry
    for dialect in DIALECT_WHITELIST:
        for entry in sorted((pkg / "data" / "dict" / dialect).iterdir()):
            if entry.is_file():
                files[f"cpp_pinyin2/data/dict/{dialect}/{entry.name}"] = entry
    return files


LICENCE_MEMBERS = ("LICENSE", "THIRD_PARTY_NOTICES.md")


def licence_problems(wheel: Path) -> List[str]:
    """The wheel must carry the licence texts it claims, inside its single `.dist-info` directory.

    A wheel declares its licences in METADATA and is expected to ship the texts next to it. Nothing compared the
    two until 2026-10-21: `member_problems` skips `.dist-info/` on purpose (auditwheel rewrites that directory and
    its entries are not source files), so a wheel whose licence files were dropped still passed every check while
    telling an installer it was under three licences. The names are the ones `setup.py`'s `license_files` produce.
    """
    problems: List[str] = []
    with zipfile.ZipFile(wheel) as archive:
        members = archive.namelist()
    dist_infos = sorted({m.split("/")[0] for m in members if m.split("/")[0].endswith(".dist-info")})
    if len(dist_infos) != 1:
        problems.append(f"expected exactly one .dist-info directory, found {dist_infos}")
        return problems
    for name in LICENCE_MEMBERS:
        member = f"{dist_infos[0]}/{name}"
        if member not in members:
            problems.append(f"missing licence material: {member} (setup.py's license_files ships it, and the "
                            f"METADATA licence expression says which texts apply)")
    return problems


def member_problems(wheel: Path, root: Optional[Path] = None) -> List[str]:
    """The wheel's member set and bytes, against a source tree.

    The other half of this file -- the version, the platform tag, the ELF surface -- stays in check().
    This part is split out because two entry points ask exactly this question now and the answer has to be
    the same one: the CI step that runs this script, and tools/check_bindings.py --wheel. Until 2026-10-02
    the binding gate compared the wheel's dialect NAMES only, and a reviewer built four wheels it accepted:
    every dictionary file empty, one table missing, an extra file, and no dictionary face at all. Each of
    those installs and then fails, so the pair of entry points now share one implementation instead of one
    strong checker plus one that merely mentioned the same subject.
    """
    where = ROOT if root is None else root
    problems: List[str] = []
    with zipfile.ZipFile(wheel) as archive:
        members = archive.namelist()
        # Directory entries are structure, not content: auditwheel rewrites the archive and stores them, and a
        # wheel that carries them is the same wheel. Counting them as unexpected members also used to cascade
        # -- "cpp_pinyin2/data/dict/" is not a file in that directory, so it looked like an unknown dialect.
        packaged: Set[str] = {m for m in members if ".dist-info/" not in m and not m.endswith("/")}
        expected = expected_source_files(where)

        for missing in sorted(set(expected) - packaged):
            problems.append(f"missing from the wheel: {missing}")
        for extra in sorted(packaged - set(expected)):
            problems.append(
                f"unexpected member in the wheel: {extra} "
                "(stale python/build/lib content, or something nobody reviewed)"
            )

        problems.extend(licence_problems(wheel))

        # Byte-for-byte: a stale copy of a module that still exists passes the set comparison above.
        for member, source in sorted(expected.items()):
            if member not in packaged:
                continue
            packaged_bytes = archive.read(member)
            source_bytes = source.read_bytes()
            if packaged_bytes != source_bytes:
                problems.append(
                    f"{member}: packaged bytes differ from {source.relative_to(where)} "
                    f"(sha256 {sha256(packaged_bytes)[:16]} vs {sha256(source_bytes)[:16]})"
                )
    return problems


def expected_version() -> str:
    path = PKG / "_version.py"
    if not path.is_file():
        raise SystemExit(
            f"FATAL: {path} is missing. Run python/tools/build_native.py first: it writes the "
            "version from the single source before anything is built."
        )
    match = re.search(r'^__version__\s*=\s*"([^"]+)"', path.read_text(encoding="utf-8"), re.M)
    if not match:
        raise SystemExit(f"FATAL: no __version__ assignment in {path}")
    return match.group(1)


def expected_platform_tag() -> str:
    """The tag setup.py is supposed to have produced.

    setup.py owns the rule; this mirrors its single line on purpose. If that rule ever changes, this
    check fails loudly instead of accepting a broader tag -- and the mirror is one expression, not a
    second implementation.
    """
    override = os.environ.get(PLATFORM_TAG_ENV, "").strip()
    return override or sysconfig.get_platform().replace("-", "_").replace(".", "_")


def elf_needed_libraries(data: bytes) -> List[str]:
    """The sonames this ELF object lists in DT_NEEDED; [] if it has no dynamic section.

    Deliberately small: the ELF header selects 32/64-bit and endianness, the program headers yield PT_LOAD
    (so the virtual addresses the dynamic section stores can be mapped back to file offsets) and PT_DYNAMIC,
    and the dynamic array yields DT_STRTAB plus the DT_NEEDED string offsets. No symbol table, no section
    header table, no dependency -- and no attempt to be a general ELF reader.
    """
    if not data.startswith(ELF_MAGIC) or len(data) < 64 or data[5] not in (1, 2):
        return []
    is64 = data[4] == 2
    endian = "<" if data[5] == 1 else ">"
    if is64:
        header = struct.unpack_from(endian + "HHIQQQIHHHHHH", data, 16)
        ph_format = endian + "IIQQQQQQ"
        word_format = endian + "qQ"
    else:
        header = struct.unpack_from(endian + "HHIIIIIHHHHHH", data, 16)
        ph_format = endian + "IIIIIIII"
        word_format = endian + "iI"
    phoff, phentsize, phnum = header[4], header[8], header[9]
    ph_size = struct.calcsize(ph_format)
    if phentsize < ph_size:
        return []

    loads: List[Tuple[int, int, int]] = []
    dynamic: Optional[Tuple[int, int]] = None
    for index in range(phnum):
        start = phoff + index * phentsize
        if start + ph_size > len(data):
            break
        entry = struct.unpack_from(ph_format, data, start)
        if entry[0] == PT_LOAD:
            if is64:
                loads.append((entry[3], entry[2], entry[5]))
            else:
                loads.append((entry[2], entry[1], entry[4]))
        elif entry[0] == PT_DYNAMIC:
            dynamic = (entry[2], entry[5]) if is64 else (entry[1], entry[4])
    if dynamic is None:
        return []

    def to_offset(vaddr: int) -> Optional[int]:
        for base, offset, filesz in loads:
            if base <= vaddr < base + filesz:
                return offset + (vaddr - base)
        return None

    word_size = 16 if is64 else 8
    needed: List[int] = []
    strtab_vaddr: Optional[int] = None
    strsz = 0
    cursor = dynamic[0]
    end = min(dynamic[0] + dynamic[1], len(data))
    while cursor + word_size <= end:
        tag, value = struct.unpack_from(word_format, data, cursor)
        cursor += word_size
        if tag == 0:
            break
        if tag == DT_NEEDED:
            needed.append(value)
        elif tag == DT_STRTAB:
            strtab_vaddr = value
        elif tag == DT_STRSZ:
            strsz = value
    if strtab_vaddr is None or not needed:
        return []
    strtab = to_offset(strtab_vaddr)
    if strtab is None:
        return []

    names: List[str] = []
    for name_offset in needed:
        start = strtab + name_offset
        if start >= len(data) or (strsz and start >= strtab + strsz):
            continue
        stop = data.find(b"\x00", start)
        if stop < 0:
            continue
        names.append(data[start:stop].decode("ascii", "replace"))
    return names


def elf_surface(data: bytes) -> Optional[Tuple[Tuple[int, int], List[str]]]:
    """(highest GLIBC_x.y referenced, C++ runtime libraries needed at load time); None if it is not ELF.

    Two different questions, deliberately answered two different ways.

    The glibc number is still a byte scan: an ELF parser, or an auditwheel dependency, would weigh more than
    the question needs, and the fact that actually differs between a container build and a laptop build is a
    literal glibc version string -- which this finds. It cannot see a versioned symbol whose name is assembled
    at run time, and neither can the dynamic linker, so that blind spot is theoretical. Its known cost is that
    it can *over*-report: a version string present as data counts. That has not been observed, and
    auditwheel remains the authority on the glibc question either way.

    The C++ runtime question is answered from the dynamic section (see CPP_RUNTIME_SONAMES), because a byte
    scan demonstrably got it wrong on a real wheel.
    """
    if not data.startswith(ELF_MAGIC):
        return None
    highest = (0, 0)
    for match in GLIBC_REFERENCE.finditer(data):
        candidate = (int(match.group(1)), int(match.group(2)))
        if candidate > highest:
            highest = candidate
    needed = [
        soname
        for soname in elf_needed_libraries(data)
        if any(soname.startswith(prefix) for prefix in CPP_RUNTIME_SONAMES)
    ]
    return highest, needed


def check(wheel: Path, notes: Optional[List[str]] = None) -> List[str]:
    """Every violation found in `wheel`. An empty list means it passed.

    `notes` collects non-fatal measurements, so that a passing wheel still says what was measured about it:
    a gate that prints nothing on success cannot be told apart from one that skipped everything.
    """
    problems: List[str] = []
    name = wheel.name
    if not name.endswith(".whl"):
        return [f"{name}: not a wheel file"]

    parts = name[: -len(".whl")].split("-")
    if len(parts) != 5:
        return [f"{name}: expected <distribution>-<version>-<python>-<abi>-<platform>.whl"]
    _, version, python_tag, abi_tag, platform_tag = parts

    if python_tag != "py3":
        problems.append(f"{name}: expected the py3 tag (ctypes, no CPython ABI), got {python_tag!r}")
    if abi_tag != "none":
        problems.append(f"{name}: expected the none ABI tag, got {abi_tag!r}")
    if platform_tag == "any":
        problems.append(
            f"{name}: the platform tag is 'any'. A wheel carrying a native library would install "
            "on every platform and fail at import time on all but the one it was built for."
        )
    expected_tag = expected_platform_tag()
    # The platform field is a compressed tag set: auditwheel writes "manylinux_2_26_aarch64.manylinux_2_28_aarch64"
    # for a wheel that is compatible with both, and that is a strictly better artifact than this check used to
    # accept. What matters is that the host's tag is among them.
    if expected_tag not in platform_tag.split("."):
        problems.append(
            f"{name}: platform tag {platform_tag!r} does not include this host's {expected_tag!r}"
        )
    if version != expected_version():
        problems.append(f"{name}: version {version!r} != {expected_version()!r} from _version.py")

    # The member set and the packaged bytes come from the shared implementation: tools/check_bindings.py
    # --wheel calls exactly this function, so the two entry points cannot drift apart.
    problems.extend(member_problems(wheel))

    with zipfile.ZipFile(wheel) as archive:
        members = archive.namelist()
        packaged: Set[str] = {m for m in members if ".dist-info/" not in m and not m.endswith("/")}
        for bytecode in sorted(m for m in members if "__pycache__" in m or m.endswith(".pyc")):
            problems.append(f"bytecode in the wheel: {bytecode}")
        for bad_name in sorted(m for m in packaged if m.startswith("cpp_pinyin2/data/dict/")):
            dialect = bad_name.split("/")[3]
            if dialect not in DIALECT_WHITELIST:
                problems.append(f"dictionary outside the whitelist: {bad_name}")

        for member in sorted(m for m in packaged if m.endswith(".so") or ".so." in m.rsplit("/", 1)[-1]):
            surface = elf_surface(archive.read(member))
            if surface is None:
                problems.append(f"{member}: a shared object name, but not an ELF object")
                continue
            highest, cpp_needed = surface
            if cpp_needed:
                problems.append(
                    f"{member}: DT_NEEDED lists {', '.join(cpp_needed)}, so it would load a C++ runtime at "
                    "run time. The C++ runtime is linked statically (plan section 6.5 item 4) so that the "
                    "manylinux GLIBCXX ceiling stops being a variable: build with -static-libstdc++ and "
                    "-static-libgcc."
                )
            if highest > MAX_GLIBC:
                problems.append(
                    f"{member}: references glibc {highest[0]}.{highest[1]}, above the manylinux_2_28 policy "
                    f"line {MAX_GLIBC[0]}.{MAX_GLIBC[1]} (plan section 6.5 items 1 and 1b). Build the wheel "
                    "inside a manylinux_2_28 container rather than on a host with a newer glibc."
                )
            elif notes is not None:
                notes.append(
                    f"{member}: highest glibc reference {highest[0]}.{highest[1]}, no C++ runtime symbols "
                    f"(policy line {MAX_GLIBC[0]}.{MAX_GLIBC[1]}; auditwheel remains the authority)"
                )

        if not any(m.endswith(".so") or ".so." in m.rsplit("/", 1)[-1] for m in packaged) and notes is not None:
            # Printable on purpose, and only from inside the archive scan: a gate that says nothing when it
            # passes cannot be told apart from one that never looked. This line must NOT appear on a Linux
            # wheel -- there it would mean the native library is missing from the package.
            notes.append(
                "no ELF shared objects in this wheel, so the glibc surface was not measured "
                "(expected on Windows; on Linux it would mean the native library is missing)"
            )

        for dialect in DIALECT_WHITELIST:
            tables = {
                m.split("/")[-1]
                for m in packaged
                if m.startswith(f"cpp_pinyin2/data/dict/{dialect}/")
            }
            for table in REQUIRED_TABLES:
                if table not in tables:
                    problems.append(f"dictionary {dialect} is missing {table} in the wheel")

        dist_info = sorted({m.split("/")[0] for m in members if ".dist-info/" in m})
        if len(dist_info) != 1:
            problems.append(f"expected exactly one .dist-info directory, found {dist_info}")
            return problems
        metadata = archive.read(f"{dist_info[0]}/METADATA").decode("utf-8")
        if f"Version: {version}" not in metadata:
            problems.append(f"METADATA does not declare Version: {version}")
        requires_python = expected_requires_python()
        if requires_python not in metadata:
            problems.append(
                f"METADATA does not declare {requires_python!r}; it states the floor setuptools wrote "
                "instead, so either python/setup.py changed without rebuilding or the metadata was edited "
                "by hand"
            )
        wheel_metadata = archive.read(f"{dist_info[0]}/WHEEL").decode("utf-8")
        # WHEEL lists one Tag line per tag, which is what a compressed tag set looks like from the inside; the
        # file name has to be covered by them. Extra metadata tags are not an error: the name is what pip
        # matches on, and a wheel that declares more than it is named for is over-declaring, not lying.
        listed_tags = {
            line.split(":", 1)[1].strip()
            for line in wheel_metadata.splitlines()
            if line.startswith("Tag:")
        }
        named_tags = {f"{python_tag}-{abi_tag}-{tag}" for tag in platform_tag.split(".")}
        unlisted = sorted(named_tags - listed_tags)
        if unlisted:
            problems.append(
                f"WHEEL metadata does not list the tags in the file name: {unlisted} missing from "
                f"{sorted(listed_tags)}"
            )

    return problems


# --- self-test ---------------------------------------------------------------


def implied_directories(members: Set[str]) -> Set[str]:
    """Every directory a member name implies, as zip directory entries spell it (with a trailing slash)."""
    directories: Set[str] = set()
    for member in members:
        parts = member.split("/")[:-1]
        for index in range(1, len(parts) + 1):
            directories.add("/".join(parts[:index]) + "/")
    return directories


def write_synthetic_wheel(
    path: Path,
    files: Dict[str, Path],
    version: str,
    platform_tag: str,
    drop: Optional[Set[str]] = None,
    mutate: Optional[Dict[str, bytes]] = None,
    extra: Optional[Dict[str, bytes]] = None,
    directories: bool = False,
    wheel_tags: Optional[Sequence[str]] = None,
    requires_python: Optional[str] = None,
    licence_files: Sequence[str] = LICENCE_MEMBERS,
) -> None:
    """A wheel built straight from the source tree, plus whatever damage the caller asks for.

    `extra` replaces or adds members verbatim, which is how the ELF controls make a shared object with a
    chosen glibc reference without a compiler in the loop. `directories` stores a directory entry per implied
    directory, and `wheel_tags` writes several Tag lines -- both of them things a repairing tool does to a
    perfectly good wheel, so both directions of those rules need a fixture. `requires_python` overrides the
    METADATA floor, which is how the control proves the checker reads it from `python/setup.py` rather than
    from a constant of its own (2026-10-09).
    """
    drop = drop or set()
    mutate = mutate or {}
    extra = extra or {}
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        if directories:
            for directory in sorted(implied_directories({*files, *extra})):
                archive.writestr(directory, b"")
        for member, source in files.items():
            if member in drop or member in extra:
                continue
            archive.writestr(member, mutate.get(member, source.read_bytes()))
        for member, payload in sorted(extra.items()):
            archive.writestr(member, payload)
        archive.writestr(
            "cpp_pinyin2-UNUSED.dist-info/METADATA",
            "Metadata-Version: 2.1\nName: cpp-pinyin2\nVersion: %s\n%s\n"
            % (version, requires_python or expected_requires_python()),
        )
        tags = list(wheel_tags) if wheel_tags is not None else [f"py3-none-{platform_tag}"]
        archive.writestr(
            "cpp_pinyin2-UNUSED.dist-info/WHEEL",
            "Wheel-Version: 1.0\n" + "".join(f"Tag: {tag}\n" for tag in tags),
        )
        for name in licence_files:
            # The two texts setup.py's license_files ships beside METADATA. `licence_files` lets a control drop
            # one, which is the only way to see the check fail: it cannot go through `drop`, because the member
            # comparison ignores .dist-info/ by design.
            archive.writestr(f"cpp_pinyin2-UNUSED.dist-info/{name}", "licence text")


def synthetic_elf(needed: Sequence[str]) -> bytes:
    """A minimal ELF64 object whose DT_NEEDED entries are exactly `needed`.

    Built for the self-test, so that the dynamic-section rule can be exercised in both directions without a
    compiler in the loop. The layout is the smallest one the reader above needs: a header, one PT_LOAD (which
    is also what maps the dynamic section's virtual addresses back to file offsets -- its base is non-zero on
    purpose, so the mapping is exercised rather than assumed), one PT_DYNAMIC, the string table, the array.
    """
    base = 0x1000
    offsets: List[int] = []
    cursor = 1
    for name in needed:
        offsets.append(cursor)
        cursor += len(name) + 1
    strings = b"\x00" + b"\x00".join(name.encode("ascii") for name in needed) + b"\x00"

    header_size, ph_size, phnum = 64, 56, 2
    phoff = header_size
    strtab_off = phoff + phnum * ph_size
    dynamic_off = strtab_off + len(strings)
    entries = [(DT_NEEDED, offset) for offset in offsets]
    entries.append((DT_STRTAB, base + strtab_off))
    entries.append((DT_STRSZ, len(strings)))
    entries.append((0, 0))
    dynamic = b"".join(struct.pack("<qQ", tag, value) for tag, value in entries)
    total = dynamic_off + len(dynamic)

    header = ELF_MAGIC + bytes([2, 1, 1, 0]) + b"\x00" * 8 + struct.pack(
        "<HHIQQQIHHHHHH", 3, 0xB7, 1, 0, phoff, 0, 0, header_size, ph_size, phnum, 0, 0, 0
    )
    load = struct.pack("<IIQQQQQQ", PT_LOAD, 5, 0, base, base, total, total, 0x1000)
    dynamic_header = struct.pack(
        "<IIQQQQQQ", PT_DYNAMIC, 6, dynamic_off, base + dynamic_off, base + dynamic_off,
        len(dynamic), len(dynamic), 8,
    )
    return header + load + dynamic_header + strings + dynamic


def self_test() -> int:
    files = expected_source_files()
    version = expected_version()
    platform_tag = expected_platform_tag()
    failures: List[str] = []

    def expect(label: str, problems: List[str], must_be_empty: bool, needle: str = "") -> None:
        if must_be_empty:
            ok = not problems
        else:
            ok = any(needle in problem for problem in problems)
        print(f"{'PASS' if ok else 'FAIL'}  self-test: {label}" + ("" if ok else f"  -> {problems}"))
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)
        good = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(good, files, version, platform_tag)
        expect("a faithful wheel passes (the control case)", check(good), True)

        dropped = sorted(m for m in files if m.endswith("/word.txt") and "/mandarin/" in m)[0]
        ghost = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(ghost, files, version, platform_tag, drop={dropped})
        expect(f"a missing {dropped} is caught", check(ghost), False, "missing from the wheel")

        stale = tmp / "stale" / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        stale.parent.mkdir()
        source_module = sorted(m for m in files if m.startswith("cpp_pinyin2/") and m.endswith(".py"))[0]
        write_synthetic_wheel(
            stale, files, version, platform_tag, mutate={source_module: b"# stale copy\n"}
        )
        expect(f"a stale {source_module} is caught", check(stale), False, "packaged bytes differ")

        wrong_tag = tmp / f"cpp_pinyin2-{version}-py3-none-any.whl"
        write_synthetic_wheel(wrong_tag, files, version, "any")
        expect("a py3-none-any tag is caught", check(wrong_tag), False, "platform tag is 'any'")

        wrong_version = tmp / f"cpp_pinyin2-9.9.9-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(wrong_version, files, "9.9.9", platform_tag)
        expect("a version mismatch is caught", check(wrong_version), False, "!= ")

        # What a repairing tool does to a good wheel: store directory entries, and declare one Tag line per
        # tag in a compressed tag set. Both must be accepted -- auditwheel produces exactly this shape, and the
        # plan's Linux story depends on auditwheel.
        dirs_wheel = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(dirs_wheel, files, version, platform_tag, directories=True)
        expect("a wheel that stores directory entries is still accepted", check(dirs_wheel), True)

        second_tag = "manylinux_2_28_x86_64"
        multi = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.{second_tag}.whl"
        write_synthetic_wheel(
            multi, files, version, f"{platform_tag}.{second_tag}",
            wheel_tags=[f"py3-none-{platform_tag}", f"py3-none-{second_tag}"],
        )
        expect("a compressed tag set is accepted", check(multi), True)

        unlisted = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.{second_tag}.whl"
        write_synthetic_wheel(
            unlisted, files, version, f"{platform_tag}.{second_tag}",
            wheel_tags=[f"py3-none-{second_tag}"],
        )
        expect("a tag in the name that WHEEL does not list is caught", check(unlisted), False,
               "does not list the tags in the file name")

        foreign = tmp / f"cpp_pinyin2-{version}-py3-none-{second_tag}.whl"
        write_synthetic_wheel(foreign, files, version, second_tag)
        expect("a wheel built for another platform is caught", check(foreign), False,
               "does not include this host's")

        # The ELF controls need no compiler, because the question is only what the bytes say. A real linked
        # object still cannot be used wholesale: the member name is either absent from the source tree
        # (Windows) or present with real bytes (Linux), and either way the byte comparison would drown the
        # verdict. The dynamic-section control is therefore built byte by byte, and the wheel the board
        # actually built stays the end-to-end control on the Linux side.
        elf_member = "cpp_pinyin2/_native/libcpp-pinyin2.so"
        too_new = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(
            too_new, files, version, platform_tag,
            extra={elf_member: ELF_MAGIC + b"\x00" * 64 + b"GLIBC_2.17\x00GLIBC_2.99\x00"},
        )
        expect("a glibc reference above the policy line is caught", check(too_new), False,
               "above the manylinux_2_28 policy line")

        # Both directions of the dynamic-section rule, innocent one first. A rule only ever fired at failing
        # inputs proves nothing about whether it discriminates -- the lesson this repository records as "a
        # control that goes red for the wrong reason looks exactly like a control that works".
        expect("an ELF object that needs only libc is not reported as needing a C++ runtime",
               elf_surface(synthetic_elf(("libc.so.6",)))[1], True)
        expect("an ELF object that needs libstdc++ is reported",
               elf_surface(synthetic_elf(("libc.so.6", "libstdc++.so.6")))[1], False, "libstdc++.so.6")

        cpp_runtime = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(
            cpp_runtime, files, version, platform_tag,
            extra={elf_member: synthetic_elf(("libc.so.6", "libstdc++.so.6", "libgcc_s.so.1"))},
        )
        expect("a shared object that would load a C++ runtime is caught", check(cpp_runtime), False,
               "DT_NEEDED lists libstdc++.so.6, libgcc_s.so.1")

        not_elf = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(not_elf, files, version, platform_tag,
                              extra={elf_member: b"MZ\x90\x00 a PE image wearing a .so name"})
        expect("a .so member that is not ELF is caught", check(not_elf), False, "not an ELF object")

        # The Python floor: the checker must read it from python/setup.py, not from a constant of its own.
        # A synthetic wheel carrying a different floor has to be refused, and the message has to name the
        # floor the tree states -- which is the only way to tell "read from setup.py" apart from "happened
        # to match a hardcoded number".
        floor = declared_python_requires()
        wrong_floor = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(
            wrong_floor, files, version, platform_tag,
            requires_python="Requires-Python: >=3.5",
        )
        expect("a METADATA floor that disagrees with python/setup.py is caught",
               check(wrong_floor), False, f"Requires-Python: {floor}")
        # The other direction, on its own wheel: `good` was overwritten above by the control that shares
        # its path, so reusing it here would have asserted about the wrong artifact (which is exactly what
        # the first version of this control did -- it failed with "METADATA does not declare ... >3.8",
        # i.e. it was reading the damaged wheel).
        floor_ok_dir = tmp / "floor-ok"
        floor_ok_dir.mkdir()
        floor_ok = floor_ok_dir / f"cpp_pinyin2-{version}-py3-none-{platform_tag}.whl"
        write_synthetic_wheel(floor_ok, files, version, platform_tag)
        missing_notices = tmp / f"cpp_pinyin2-{version}-py3-none-{platform_tag}-nonotices.whl"
        write_synthetic_wheel(missing_notices, files, version, platform_tag, licence_files=("LICENSE",))
        expect("a wheel without the third-party notices is refused",
        any("missing licence material" in p for p in check(missing_notices)), True)

        expect("the honest floor is not reported", check(floor_ok), True)

    print(f"self-test: {len(failures)} failure(s)" + (": " + "; ".join(failures) if failures else ""))
    return 1 if failures else 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wheel", nargs="?", help="the .whl to check")
    parser.add_argument("--self-test", action="store_true", help="prove the checker can fail")
    options = parser.parse_args(argv)

    if options.self_test:
        return self_test()
    if not options.wheel:
        parser.error("a wheel path is required (or --self-test)")

    wheel = Path(options.wheel).resolve()
    if not wheel.is_file():
        raise SystemExit(f"FATAL: {wheel} does not exist")
    notes: List[str] = []
    problems = check(wheel, notes)
    print(f"checking {wheel.name} against the source tree in {ROOT}")
    print(f"expected platform tag: {expected_platform_tag()}")
    print(f"expected version: {expected_version()}")
    for note in notes:
        print(f"note  {note}")
    for problem in problems:
        print(f"FAIL  {problem}")
    if problems:
        print(f"\n{len(problems)} problem(s)")
        return 1
    print("PASS  every packaged file matches its source byte for byte; nothing missing, nothing extra")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
