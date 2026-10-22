"""The verifications that need a compiler, in one place and on demand.

`tools/run_all_gates.py` covers only what a text scan can decide. Four questions cannot be answered
that way, and this file is where they live:

  * ``root``: a translation unit writing a root-style name (``cpp_pinyin::X``) must FAIL to compile,
              and the generation-qualified spelling (``cpp_pinyin::vN::X``) must COMPILE. Both halves are
              required -- a toolchain that cannot compile anything at all would otherwise satisfy
              "must fail" for the wrong reason, which is how a broken guard reads as a working one.
  * ``pj``  : the C ABI symbols carry the generation token. A C probe calling the old spelling must FAIL
              to link against the built library; the same probe with the tokenised spelling must LINK.
              Before that rename lands the two probes report the mirror image, so the expectation is READ
              FROM THE TREE (does the header spell a tokenised symbol?) instead of assumed here.
  * ``g3``  : both generations in ONE translation unit -- a v2 header next to its v3 counterpart, with the
              frozen public-name list driving an ``is_same_v`` probe per name. An alias whose right-hand
              side names only standard-library types is the SAME type in both generations, so writing the
              wrong generation still compiles; the probe NAMES those aliases instead of asserting a count.
  * ``v3``  : a v3-day tree (produced by the manual procedure in ``docs/UPGRADING_A_GENERATION.md``)
              configures, builds and passes ``ctest``. This tool does not produce that tree: the one-off
              migration machinery is deliberately not kept in the repository, so the tree is an input
              (``--v3-tree``) rather than something generated here.

Building is a permissioned action in this project, not a side effect of running a checker, so nothing is
built without ``--authorised``. Without it every section still runs: it prints the command it would use
and the verdict it expects, which is the entire point of the dry run.

Usage
  python tools/verify_build_dependent.py                     # dry run: describes all four sections
  python tools/verify_build_dependent.py --authorised        # root + pj (builds into --build-dir)
  python tools/verify_build_dependent.py --authorised --v3-tree <dir>    # adds g3 and v3
  python tools/verify_build_dependent.py --authorised --only root
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

#: The frozen, per-generation list of public names. Its header says how many are types and how many are
#: functions; the probe below reads that split out of the list rather than hard-coding it.
NAMES_LIST = Path("tests/abi/expected_public_names.v2.txt")

#: Test switches are tokenised per generation (docs/STABILITY.md, R5). The token IS the package's major
#: version, so the numbers here are derived, never typed: a switch spelled without its token is silently
#: ignored by CMake, and a configure that ignores the test switch builds zero tests and still exits 0.
SWITCH_FAMILY = "CPP_PINYIN{generation}_BUILD_{kind}"


def find_root() -> Path:
    here = Path(__file__).resolve()
    for candidate in (here.parent, *here.parents):
        if (candidate / "CMakeLists.txt").is_file() and (candidate / "include").is_dir():
            return candidate
    return Path.cwd()


def package_generation(root: Path) -> int:
    """The major version, read from the one place that owns it (the root CMakeLists.txt)."""
    import re
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    match = re.search(r"project\s*\(\s*\S+\s+VERSION\s+(\d+)", text)
    if not match:
        raise SystemExit("verify_build_dependent: no project(<name> VERSION x.y.z) line in CMakeLists.txt")
    return int(match.group(1))


def configure_args(generation: int, tests: bool = True, generator: str = "Ninja") -> list[str]:
    """The canonical configuration, with the test switch spelled through its generation token.

    A switch written without its token is ignored by CMake, so a configure that meant to build the tests
    would build none and still exit 0 -- the two link probes only need the library, and the tree that is
    meant to run ctest asks for the tests explicitly.

    The generator is a parameter and is printed by every section that builds. The repository's preset uses
    Ninja; Ninja is blocked in some sandboxes (its jobserver pipe), and a silent generator swap would let
    two runs report under the same name while building differently.
    """
    state = "ON" if tests else "OFF"
    tests_switch = f"-D{SWITCH_FAMILY.format(generation=generation, kind='TESTS')}={state}"
    examples = f"-D{SWITCH_FAMILY.format(generation=generation, kind='EXAMPLES')}=OFF"
    return ["-G", generator, "-DCMAKE_BUILD_TYPE=Release", tests_switch, examples]


def run(command: list[str], cwd: Path, env: dict[str, str] | None = None, quiet: bool = False) -> tuple[int, str]:
    print("    $ " + " ".join(str(part) for part in command))
    completed = subprocess.run([str(part) for part in command], cwd=str(cwd), env=env,
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
    parts = (completed.stdout or "").strip().splitlines()[-4:]
    parts += (completed.stderr or "").strip().splitlines()[-4:]
    if not quiet:
        for line in parts:
            print("      " + line[:120])
    return completed.returncode, "\n".join(parts)


def build_env(tree: Path) -> dict[str, str]:
    """TEMP/TMP inside the workspace, at the given directory itself.

    Two measured facts, both about this environment rather than about any tree under test (2026-10-16):
    a scratch directory under the machine's %TEMP% made `std::ofstream` fail to open a file directly in it
    (a two-line probe reported `open=0`), and pointing TEMP at `<build>/tmp` instead of `<build>` turned
    three failing tests on the v3-day tree into eight. So TEMP is the build directory itself, which the
    build system created and which the same probe reports as `open=1`.
    """
    env = dict(os.environ)
    tree.mkdir(parents=True, exist_ok=True)
    env["TEMP"] = str(tree)
    env["TMP"] = str(tree)
    return env


def names_from_list(root: Path) -> list[str]:
    """Every name in the frozen list. The list's own comments are the authority for what it holds.

    The kind of a name (type/alias vs function) is deliberately NOT read from the prose: the probe below
    asks the compiler which spelling fits, so a list edited by hand cannot silently mislead it.
    """
    path = root / NAMES_LIST
    if not path.is_file():
        raise SystemExit(f"verify_build_dependent: {NAMES_LIST} is missing -- the g3 probe has no input")
    body = [line.strip() for line in path.read_text(encoding="utf-8-sig").splitlines()]
    return [line for line in body if line and not line.startswith("#")]


def headers_from_list(root: Path, generation: int) -> list[str]:
    """The frozen public headers as paths relative to a generation directory.

    The g3 probe has to include EVERY public header, not one guessed header: measured 2026-10-16, a probe
    that included only `core/Types.h` reported 18 of 36 names as NOT PROBED -- they are declared in the
    other headers (`core/Engine.h`, `core/U16Str.h`, `dialects/*`, `ext/*`), so the probe never saw them
    and "not probed" would have been read as a property of the tree instead of of the probe.
    """
    path = root / "tests" / "abi" / "expected_public_headers.txt"
    if not path.is_file():
        raise SystemExit("verify_build_dependent: expected_public_headers.txt is missing -- the g3 probe "
                         "would include a guessed subset")
    prefix = f"cpp-pinyin{generation}/"
    return [line.strip()[len(prefix):] for line in path.read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.strip().startswith("#") and line.strip().startswith(prefix)]


def probe_one(name: str, generation: int, same: int, includes: list[str], scratch: Path,
              workdir: Path, headers: list[str]) -> tuple[str, str]:
    """Ask what one frozen name shows about two generations: ("DISTINCT"|"SAME"|"DECLARED"|"NOT PROBED", why).

    Types carry the R16 question, and a type is named directly -- or with one `int` argument, which is what
    turns a template name (the frozen list holds `Outcome`) from "cannot be named" into a real verdict. A
    failed static assertion means the two spellings denote the SAME type, which is exactly the shape R16
    forbids: it is what an alias to a standard-library type looks like.

    Functions are NOT probed with `is_same_v` any more. Measured 2026-10-16 on a correct tree: three
    functions (`isSpace`, `isSpecialKana`, `utf8strToU16str`) were reported SAME, because a function
    pointer's type does not mention the namespace it was taken from -- two generational functions with the
    same signature have the same pointer type by construction. That is a property of the question, not of
    the tree, so a function gets the weaker verdict DECLARED: both generations declare it, and the
    namespace already keeps them apart. Overload sets and other names that are not entities at all stay
    NOT PROBED, which is reported as "nothing was proven" rather than as a pass.
    """
    preamble = "".join(f"#include <cpp-pinyin{generation}/{header}>\n"
                       f"#include <cpp-pinyin{same}/{header}>\n" for header in headers)
    forms = (("type", f"cpp_pinyin::v{generation}::{name}, cpp_pinyin::v{same}::{name}"),
             ("template", f"cpp_pinyin::v{generation}::{name}<int>, cpp_pinyin::v{same}::{name}<int>"))
    last = ""
    for kind, operands in forms:
        source = scratch / f"probe_{kind}_{name}.cpp"
        source.write_text(
            preamble
            + "#include <type_traits>\n"
            + f"static_assert(!std::is_same_v<{operands}>);\n"
            + "int main() { return 0; }\n", encoding="utf-8", newline="")
        completed = subprocess.run(["g++", "-std=c++17", *includes, "-fsyntax-only", str(source)],
                                   cwd=str(workdir), capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=600)
        output = ((completed.stdout or "") + (completed.stderr or "")).strip()
        if completed.returncode == 0:
            return "DISTINCT", kind
        last = output
        if "static assertion failed" in output:
            return "SAME", kind
    source = scratch / f"probe_declared_{name}.cpp"
    source.write_text(
        preamble
        + f"int main() {{ (void)&cpp_pinyin::v{generation}::{name}; (void)&cpp_pinyin::v{same}::{name};"
          " return 0; }\n", encoding="utf-8", newline="")
    completed = subprocess.run(["g++", "-std=c++17", *includes, "-fsyntax-only", str(source)],
                               cwd=str(workdir), capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=600)
    if completed.returncode == 0:
        return "DECLARED", "declared"
    # An overload set has no address and no single instantiation, so nothing can be compiled for it. The
    # fallback is textual, and it is reported AS textual: `u16strToUtf8str` (overloads on `char16_t` and
    # `std::u16string`) is the one frozen name that reaches this path, and calling it DECLARED without
    # saying how it was checked would be the quiet weakening this device exists to avoid.
    seen = []
    for index, entry in enumerate(includes):
        generation_for_root = generation if index == 0 else same
        include_root = Path(entry[2:])
        for header in headers:
            path = include_root / f"cpp-pinyin{generation_for_root}" / header
            if path.is_file() and name in path.read_text(encoding="utf-8", errors="replace"):
                seen.append(generation_for_root)
                break
    if sorted(seen) == sorted({generation, same}):
        return "DECLARED", "declared-by-text (an overload set has no addressable form; weaker than a compile)"
    first = (last.splitlines() or [""])[0]
    return "NOT PROBED", first[:100]


def cross_generation(root: Path, build: Path, v3_tree: Path | None, authorised: bool, generator: str) -> int:
    del generator                                  # this section only compiles syntax, it never builds a tree
    generation = package_generation(root)
    names = names_from_list(root)
    same = generation + 1
    print("== g3: both generations in ONE translation unit, with the frozen names probed one by one ==")
    print(f"    one TU per name, including a cpp-pinyin{generation} header AND its cpp-pinyin{same}"
          " counterpart,")
    print(f"    carrying static_assert(!std::is_same_v<...>) -- input: {NAMES_LIST} ({len(names)} name(s))")
    print("    expect: every name DISTINCT; a name reported SAME is the R16 shape -- an alias whose")
    print("            right-hand side names only standard-library types, so writing the wrong generation")
    print("            still compiles. NOT PROBED means neither spelling fitted and nothing was proven.")
    if v3_tree is None:
        print("    SKIPPED: needs --v3-tree <dir> (a v3-day header tree; only the headers are read)")
        return 0
    if not authorised:
        print("    SKIPPED (needs --authorised)")
        return 0
    scratch = build / "probes-g3"
    scratch.mkdir(parents=True, exist_ok=True)
    includes = [f"-I{root / 'include'}", f"-I{v3_tree / 'include'}"]
    headers = headers_from_list(root, generation)
    verdicts = []
    for name in names:
        verdict, why = probe_one(name, generation, same, includes, scratch, build, headers)
        verdicts.append((name, verdict, why))
    distinct = [(n, w) for n, v, w in verdicts if v == "DISTINCT"]
    shared = [(n, w) for n, v, w in verdicts if v == "SAME"]
    declared = [(n, w) for n, v, w in verdicts if v == "DECLARED"]
    unprobed = [(n, w) for n, v, w in verdicts if v == "NOT PROBED"]
    print(f"    DISTINCT {len(distinct)}/{len(verdicts)}"
          f" (named directly {sum(1 for _, w in distinct if w == 'type')}"
          f", via one `int` argument {sum(1 for _, w in distinct if w == 'template')})"
          f"   DECLARED (functions and other non-types) {len(declared)}"
          f"   SAME {len(shared)}   NOT PROBED {len(unprobed)}")
    for name, why in shared:
        print(f"    SAME TYPE ACROSS GENERATIONS ({why} form): {name}")
    for name, why in unprobed:
        print(f"    NOT PROBED: {name} -- {why}")
    print("    reading: DECLARED for a function is the expected verdict -- the R16 question is about")
    print("             types, and a function pointer's type does not mention its namespace.")
    ok = not shared and not unprobed
    print(f"    => {'VERIFIED' if ok else 'NOT VERIFIED'}")
    return 0 if ok else 1


def root_name_refusal(root: Path, build: Path, authorised: bool) -> int:
    good_include = root / "include"
    generation = package_generation(root)
    header = f"#include <cpp-pinyin{generation}/core/Engine.h>\n"
    bad_src = header + "int main() { cpp_pinyin::Engine *e = nullptr; return e == nullptr ? 0 : 1; }\n"
    good_src = (header + f"int main() {{ cpp_pinyin::v{generation}::Engine *e = nullptr;"
                " return e == nullptr ? 0 : 1; }\n")
    print("== root: a root-style reference must FAIL to compile; the qualified spelling must COMPILE ==")
    print(f"    bad  : {bad_src.strip()}")
    print(f"    good : {good_src.strip()}")
    print(f"    g++ -std=c++17 -I{good_include} -fsyntax-only <file>")
    print("    expect: bad exit != 0, good exit == 0")
    if not authorised:
        print("    SKIPPED (needs --authorised)")
        return 0
    scratch = build / "probes-root"
    scratch.mkdir(parents=True, exist_ok=True)
    bad, good = scratch / "root_bad.cpp", scratch / "root_good.cpp"
    bad.write_text(bad_src, encoding="utf-8", newline="")
    good.write_text(good_src, encoding="utf-8", newline="")

    def compile_one(path: Path) -> tuple[int, str]:
        completed = subprocess.run(["g++", "-std=c++17", f"-I{good_include}", "-fsyntax-only", str(path)],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace",
                                   timeout=600)
        return completed.returncode, ((completed.stdout or "") + (completed.stderr or "")).strip()

    bad_code, bad_out = compile_one(bad)
    good_code, good_out = compile_one(good)
    print(f"    bad  exit={bad_code} (want non-zero) {bad_out.splitlines()[0][:90] if bad_out else ''}")
    print(f"    good exit={good_code} (want 0)        {good_out.splitlines()[0][:90] if good_out else ''}")
    ok = bad_code != 0 and good_code == 0
    print(f"    => {'VERIFIED' if ok else 'NOT VERIFIED'}")
    return 0 if ok else 1


def link_probes(root: Path, build: Path, authorised: bool, generator: str) -> int:
    generation = package_generation(root)
    header = root / "include" / f"cpp-pinyin{generation}" / "ext" / "c_api.h"
    text = header.read_text(encoding="utf-8", errors="replace") if header.is_file() else ""
    tokenised = f"cppp{generation}_" in text
    new_symbol = f"cppp{generation}_open"
    probes = root / "tests" / "abi" / "probes"
    print("== pj: the C ABI symbol carries the generation token ==")
    print(f"    header: {header.relative_to(root)}")
    print("    the probe in tests/abi/probes/link_old_symbol.c calls the PRE-RENAME spelling -- it must")
    print("    FAIL to link once the rename has landed")
    print(f"    the probe in tests/abi/probes/link_new_symbol.c calls {new_symbol}() -- it must LINK")
    print(f"    expectation read from the tree: {'tokenised (post-rename)' if tokenised else 'NOT tokenised yet'}")
    print("    expect: " + (f"old exit != 0, new exit == 0"
                          if tokenised else f"old exit == 0, new exit != 0 (the rename has not landed)"))
    if not authorised:
        print("    SKIPPED (needs --authorised)")
        return 0
    args = configure_args(generation, tests=False, generator=generator)
    if run(["cmake", "-S", ".", "-B", str(build), *args], root)[0]:
        print("    configure failed -- nothing to link against")
        return 1
    if run(["cmake", "--build", str(build), "--parallel"], root)[0]:
        print("    the library did not build -- a link verdict would mean nothing")
        return 1

    library = next((p for p in sorted((build / "bin").glob(f"*cpp-pinyin{generation}*"))
                    if p.suffix in (".dll", ".so", ".dylib")), None)
    if library is None:
        print(f"    no cpp-pinyin{generation} library under {build / 'bin'} -- cannot link a probe")
        return 1
    print(f"    library: {library if library.is_relative_to(root) else library.name}")
    scratch = build / "probes-link"
    scratch.mkdir(parents=True, exist_ok=True)
    sources = {}
    for label in ("old", "new"):
        source = probes / f"link_{label}_symbol.c"
        if not source.is_file():
            print(f"    {source.relative_to(root)} is missing -- the negative/positive probe pair is the device")
            return 1
        sources[label] = source
    linker = shutil.which("gcc") or shutil.which("cc")
    if linker is None:
        print("    no C compiler on PATH (looked for gcc, then cc) -- cannot compile the probes")
        return 1
    results: dict[str, int] = {}
    for label, source in sources.items():
        out = scratch / f"probe_{label}.exe"
        code, _ = run([linker, "-o", str(out), str(source), "-L", str(build / "bin"),
                       f"-l{library.stem[3:] if library.stem.startswith('lib') else library.stem}"],
                      root, quiet=True)
        results[label] = code
        print(f"    {label}: exit={code} ({'linked' if code == 0 else 'DID NOT LINK'})")
    ok = (results["new"] == 0 and results["old"] != 0) if tokenised else (results["old"] == 0)
    print(f"    => {'VERIFIED' if ok else 'NOT VERIFIED'} (expectation: "
          f"{'tokenised' if tokenised else 'pre-rename'})")
    return 0 if ok else 1


def v3_day_tree(root: Path, build: Path, v3_tree: Path | None, authorised: bool, generator: str) -> int:
    print("== v3: a v3-day tree configures, builds and passes ctest ==")
    print("    the tree is an INPUT: produce it with the manual procedure in docs/UPGRADING_A_GENERATION.md")
    print("    expect: configure 0, build 0, ctest 0")
    if v3_tree is None or not (v3_tree / "CMakeLists.txt").is_file():
        # Two different inputs wear the name "--v3-tree": a full clone rebuilt as generation N (which can
        # be configured and ctested) and the header-only rehearsal tree, which is what proves the compile
        # level questions and has no build system at all. Measured 2026-10-16: pointing THIS section at
        # the header-only tree produced three command failures that read like a broken tree, when the
        # truth was "this section needs a full-tree rehearsal (e.g. make_two_gen_tree.py)".
        print("    SKIPPED: --v3-tree must be a FULL clone rebuilt as the next generation (a tree with a")
        print("             CMakeLists.txt). The header-only rehearsal tree proves the compile-level")
        print("             questions in the g3 section; it has no build system, so ctest cannot run on it.")
        return 0
    if not authorised:
        print("    SKIPPED (needs --authorised)")
        return 0
    tree_build = build / "v3-tree-build"
    generation = package_generation(root) + 1
    codes = []
    for command in (["cmake", "-S", ".", "-B", str(tree_build), *configure_args(generation, generator=generator)],
                    ["cmake", "--build", str(tree_build), "--parallel"],
                    ["ctest", "--test-dir", str(tree_build), "--output-on-failure"]):
        # Every step gets TEMP/TMP INSIDE the workspace. Measured 2026-10-16: handing the tests a scratch
        # directory under the machine's %TEMP% made `std::ofstream` fail to open a file directly in it (a
        # two-line probe reported `open=0`), which failed three test binaries for a reason that had nothing
        # to do with the tree; the same probe in a build directory inside the workspace reports `open=1`.
        code, _ = run(command, v3_tree, env=build_env(tree_build))
        codes.append(code)
    print(f"    => configure/build/ctest = {'/'.join(str(code) for code in codes)}")
    return 0 if codes == [0, 0, 0] else 1


def main() -> int:
    root = find_root()
    parser = argparse.ArgumentParser(description="the verifications that need a compiler")
    parser.add_argument("--only", choices=["v3", "pj", "g3", "root", "all"], default="all")
    parser.add_argument("--authorised", action="store_true",
                        help="required: building is a permissioned action in this project")
    parser.add_argument("--build-dir", default=str(root / ".tmp" / "verify-build"),
                        help="where the library is built and the probe sources land (gitignored)")
    parser.add_argument("--v3-tree", default=None,
                        help="a v3-day tree, needed by --only v3 and --only g3")
    parser.add_argument("--generator", default="Ninja",
                        help="the CMake generator to build with (the preset uses Ninja; MinGW Makefiles "
                             "is the alternative where Ninja's jobserver pipe is blocked)")
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    v3_tree = Path(args.v3_tree).resolve() if args.v3_tree else None
    print(f"generator: {args.generator}   build dir: {build}")
    if not args.authorised:
        print("DRY RUN -- nothing is built, nothing outside the build directory is written.")
        print("Re-run with --authorised once a build is approved.\n")
    sections = (("root", root_name_refusal), ("pj", link_probes),
                ("g3", cross_generation), ("v3", v3_day_tree))
    failures = 0
    described = 0
    for name, function in sections:
        if args.only in ("all", name):
            described += 1
            if function is root_name_refusal:
                failures += function(root, build, args.authorised)
            elif function is link_probes:
                failures += function(root, build, args.authorised, args.generator)
            else:
                failures += function(root, build, v3_tree, args.authorised, args.generator)
            print()
    if not args.authorised:
        print(f"dry run: {described} section(s) described, none executed; nothing was built")
    else:
        print("sections run:" + (" all green" if failures == 0 else f" {failures} failing"))
    return failures


if __name__ == "__main__":
    sys.exit(main())
