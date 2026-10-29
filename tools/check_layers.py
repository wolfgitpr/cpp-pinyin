#!/usr/bin/env python3
"""AC-3 as a machine gate: lower layers must not include upper layers.

AC-3 (docs/DECISIONS.md) is a rule, and a rule enforced by grepping by hand carries its own caveat every
time. The inventory is the LAYERS table below: each layer plus the markers it must not include. The prose
does not restate that table, because two lists disagree eventually. When the table misses a file the
uncovered-file check reports it, instead of the scan silently shrinking.

Two facts the scan encodes:

  * src/infra/ is a SET of two translation units, not a directory -- listing it as a directory made the
    tool print "does not exist, its rule did not run" and then "AC-3 OK", which reads as if the criterion
    had passed;
  * the public core headers are deliberately NOT upper layers: a dict that could not use
    `utf8strToU16str` would be unimplementable.

Usage:
    python tools/check_layers.py               # check the tree, exit 1 on a violation
    python tools/check_layers.py --self-test   # prove the checker can fail: a clean fixture passes and
                                               # fixtures with planted violations must be rejected
"""

import re
import sys
import tempfile
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)

# layer -> (files relative to the source root, markers this layer must not include)
LAYERS = {
    "infra": (("src/U16Str.cpp", "src/G2pglobal.cpp", "src/Utf8Conv.h"),
              ("dict/", "toneUtil/", "Engine.h", "EngineImpl", "Mandarin.h", "Cantonese.h")),
    "dict": (("src/dict/DictManifest.cpp", "src/dict/DictManifest.h",
              "src/dict/DictUtil.cpp", "src/dict/DictUtil.h",
              "src/dict/PhraseIndex.cpp", "src/dict/PhraseIndex.h",
              "src/dict/PhraseLayers.cpp", "src/dict/PhraseLayers.h",
              "src/dict/EmbeddedTables.cpp", "src/dict/EmbeddedTables.h"),
             ("tone/", "toneUtil/", "Engine.h", "EngineImpl", "Mandarin.h", "Cantonese.h",
              "ManTone.h", "CanTone.h")),
    "tone": (("src/toneUtil/ManTone.cpp", "src/toneUtil/ManToneUtil.cpp", "src/toneUtil/ManToneUtil.h",
              "src/toneUtil/CanTone.cpp", "src/toneUtil/ReadingCodec.cpp", "src/toneUtil/ReadingCodec.h"),
             ("dict/", "Engine.h", "EngineImpl", "dialects/")),
    "core": (("src/ToneConverter.cpp", "src/ToneSandhi.cpp", "src/Result.cpp", "src/Outcome.cpp",
              "src/Capabilities.h"),
             # `ext/` is here as of 2026-10-20: `src/Capabilities.h` DOES include
             # the public `ext/Dialect.h`, and until this marker existed neither this scan nor the
             # public-header tier scan could see that edge -- the file sat in the core row, whose
             # forbidden list had no `ext/`, while the tier scan only reads `include/`. The edge is
             # legitimate (the mask needs a `Dialect`), so it is registered in LAYER_EXEMPT and printed
             # on every run rather than left invisible; the marker is what makes a SECOND, illegitimate
             # core->ext edge impossible to hide in the same blind spot.
             ("dict/", "ext/", "Engine.h", "EngineImpl", "Mandarin.h", "Cantonese.h")),
    "engine": (("src/Engine.cpp", "src/EngineImpl.cpp", "src/EngineImpl.h", "src/EngineImplLoad.cpp"),
               ()),  # composition layer: it may reach downwards into anything
    "dialect": (("src/Mandarin.cpp", "src/Cantonese.cpp", "src/BuiltinDialects.h"),
                ("dict/", "EngineImpl")),
    "c_api": (("src/c_api_internal.h", "src/c_api_lifecycle.cpp", "src/c_api_query.cpp", "src/DictLocation.cpp"),
              ("dict/", "EngineImpl")),
}

#: The upward edges that ARE there, registered so the gate reports them instead of being blind to them
#: (2026-10-20). This is not a whitelist to make the tree green: each entry says which
#: file, which layer, which edge, and WHY the edge is legitimate, and the run prints every one of them. The
#: point is that a second, illegitimate edge cannot hide in the same blind spot -- `ReadingCodec.cpp` calling
#: `ManTone::toneToInitials` is a real coupling that `check_layers` did not see at all before this table
#: existed, because `ManTone.cpp` is listed in the tone row itself (so the layer model said "same layer")
#: while its HEADER lives in the public `dialects/` tier.
LAYER_EXEMPT = {
    ("src/toneUtil/ReadingCodec.cpp", "dialects/"): (
        "toneMarked splitting/merging IS ManTone's tables (`toneToInitials`/`toneToFinals`/`toneTone3`), and "
        "ReadingCodec.h says so in its own comment; the fix would be to inject the splitter with the Dialect, "
        "which is a design change, not a cleanup -- so the edge stays and is registered here"),
    ("src/Capabilities.h", "ext/"): (
        "the capability mask IS a property of the Dialect (`supportsVToU`/`supportsNeutralTone5`), so `core` "
        "cannot own it without that header; `docs/CONTRACT.md` keeps `Dialect` in `ext/`, and Capabilities.h "
        "cannot move up into `ext/` (it would add a public name to the frozen surface) -- registered 2026-10-20 "
        "so the edge is printed instead of invisible"),
}


def includes_of(path):
    return INCLUDE_RE.findall(path.read_text(encoding="utf-8", errors="replace"))


#: A definition line (`class X {` / `struct X : public Y`), capturing the type name. Anchored at the line
#: start through `re.M` -- a mention inside a sentence (`/// this is an `EngineImpl``) is NOT a declaration,
#: and neither is `class Impl;` nested in a class body (see FORWARD_RX for why that distinction matters).
DECL_RX = re.compile(r"^\s*(?:class|struct)\s+(?:\w+_EXPORT\s+)?(\w+)", re.M)

#: A forward declaration (`class X;`) at NAMESPACE level. This is how a leak appears: a public header needs
#: the name in a signature but not the definition. Kept separate from DECL_RX because conflating them made
#: this scan blind -- a leaked name is BY DEFINITION also declared in the public header, so asking "is this
#: name declared only in src?" excluded exactly the case the check exists for (measured: the self-test's
#: forward-declaration fixture produced ZERO findings and the check looked healthy).
#:
#: The indentation class is load-bearing, and it took two false positives to get right: `[ ]{0,4}` also
#: matched `class Impl;` nested inside a class body, which made `Registry::Impl` a src-"defined" type and
#: reported the public `Registry` as a leak. Namespace level is exactly one space in this tree (the
#: `namespace cpp_pinyin::v2 {` body is indented once), so the class is `[ ]` and nothing deeper.
FORWARD_RX = re.compile(r"^[ ](?:class|struct)\s+(\w+)\s*;")

#: A public header reaching into `src/`, which is a leak regardless of what the type is called.
INCLUDE_SRC_RX = re.compile(r'#\s*include\s*["<](?:\.\./)*src/')


# --- the public-header graph ------------------------------------------------------------------------
# A second question, about headers rather than translation units: the public headers live in a tiered
# tree (`include/cpp-pinyin2/<tier>/`), and until 2026-10-10 nothing checked the DIRECTION of the edges
# between them -- check_tree only ever read files listed in LAYERS, all of them under src/.
#
# Measured when the scan was added: 14 headers, 13 internal edges, exactly ONE upward edge,
# `core/Engine.h -> ext/Dialect.h`. It stays, and the reason is the frozen surface rather than taste:
# 8 of the 18 declarations in `tests/abi/expected_plugin_surface.txt` are `ext/Dialect.h`'s fields, so
# that header IS most of the plugin contract, and moving it down would split the plugin surface across
# two tiers while `ext/Registry.h` (the other 10) stayed put. The invariant this scan enforces instead:
# the exemption list below is the ONLY upward edge allowed, so a second one cannot appear unnoticed.
PUBLIC_TIERS = ("core", "dialects", "ext")
UPWARD_EXEMPT = {
    ("cpp-pinyin2/core/Engine.h", "cpp-pinyin2/ext/Dialect.h"): (
        "Dialect is the plugin contract's own type (8 of the 18 frozen plugin-surface declarations are "
        "its fields), and Engine takes one by value; moving the header down would split that surface "
        "across two tiers"),
}


def check_public_headers(root):
    """(findings, edges_checked) for upward edges between the public headers' tiers."""
    include_root = root / "include" / "cpp-pinyin2"
    findings = []
    edges = 0
    headers = 0
    for path in sorted(include_root.rglob("*.h")):
        source_tier = path.parent.name
        if source_tier not in PUBLIC_TIERS:
            continue
        headers += 1
        source = path.relative_to(root / "include").as_posix()
        for inc in includes_of(path):
            parts = inc.split("/")
            if len(parts) < 3 or parts[0] != "cpp-pinyin2":
                continue
            target_tier = parts[1]
            if target_tier not in PUBLIC_TIERS or target_tier == source_tier:
                continue
            target = "/".join(parts)          # the include as written: `cpp-pinyin2/<tier>/<Header>.h`
            edges += 1
            if PUBLIC_TIERS.index(target_tier) <= PUBLIC_TIERS.index(source_tier):
                continue                      # a tier reaching DOWN, or sideways: what tiers are for
            if (source, target) in UPWARD_EXEMPT:
                continue
            findings.append(
                f"VIOLATION (public tier) {source} ({source_tier}) includes {inc} ({target_tier}): "
                f"only {len(UPWARD_EXEMPT)} upward edge is allowed, and this is not one of them")
    if not edges and headers:
        # A guard against the failure mode this file has history with: a scan that reads nothing and
        # says the criterion passed. It fires only when the tier directories HOLD headers -- the
        # self-test's fixtures carry none, and demanding an edge there was a false positive (measured:
        # self-test case 6 failed on this guard alone).
        findings.append(f"VIOLATION (public tier) {headers} headers live in the tiers but the scan found "
                        f"no internal edge at all -- the scan is not reading them")
    return findings, edges


def check_public_surface_leaks(root):
    """Does the public surface hand out a type that only `src/` declares?

    `check_public_headers` above proves no public header INCLUDES across tiers, and `HeaderSelfCheck` proves
    each public header compiles alone. Neither asks whether the public surface still HANDS OUT a name the
    consumer cannot define -- which is the decoupling question a third-party dialect author actually meets.

    Measured 2026-10-10 on this tree: 14 public headers declare 17 types, src/ defines 13 types of its own.
    `Engine.h` forward-declares `Registry` at namespace level, and `Registry` IS a public type of its own
    (`class Registry` in `include/cpp-pinyin2/ext/Registry.h`) -- so pointing a public header at a type `src/`
    defines does not happen today. Four internal names are MENTIONED in public headers, all in explanatory
    prose (`EngineImpl` as "what sits underneath and is not part of the ABI", `PhraseGate` in a comment about
    an export leak, `PhraseLayers` naming the file a rule lives in, `cppp2_engine` as the deliberate opaque
    handle whose incomplete type the C API is built on) -- naming an internal in a comment is documentation,
    not a leak, so mentions are not findings.

    The finding is therefore narrow on purpose: a namespace-level forward declaration of a type that `src/`
    defines, or any include of a `src/` header. Those are the shapes a leak takes, and each one breaks the
    "build a consumer against include/ alone" promise that InstallConsumerSmoke checks end-to-end.
    """
    include_root = root / "include"
    src_root = root / "src"
    if not include_root.is_dir() or not src_root.is_dir():
        return [], 0, 0

    def declared(paths):
        names = set()
        for path in paths:
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                match = DECL_RX.match(line)
                if match:
                    names.add(match.group(1))
        return names

    defined_by_src = declared(sorted([*src_root.rglob("*.h"), *src_root.rglob("*.cpp")]))

    findings = []
    checked = 0
    for path in sorted(include_root.rglob("*.h")):
        rel = path.relative_to(root).as_posix()
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if INCLUDE_SRC_RX.search(line):
                findings.append(f"VIOLATION (public surface) {rel}:{number} includes a src/ header: "
                                f"{line.strip()[:70]}")
                continue
            forward = FORWARD_RX.match(line)
            if forward:
                checked += 1
                if forward.group(1) in defined_by_src:
                    findings.append(
                        f"VIOLATION (public surface) {rel}:{number} forward-declares {forward.group(1)}, "
                        f"which src/ defines -- a consumer including only include/ cannot complete that type")
            elif DECL_RX.match(line):
                checked += 1
    return findings, checked, len(defined_by_src)


def check_tree(root):
    findings = []
    scanned = 0
    per_layer = {}
    exempted = []
    per_layer = {}
    for layer, (files, forbidden) in LAYERS.items():
        layer_scanned = 0
        for rel in files:
            path = root / rel
            if not path.is_file():
                # A named file that is gone is not a note. It used to be: the rule simply did not run,
                # and the script still printed "AC-3 OK" -- a rename or a move could take a whole layer
                # out of the scan with nothing turning red. Deleting the rule is not a legal way to
                # satisfy it, so this is a violation like any other.
                findings.append(
                    f"VIOLATION (missing) {rel}: the layer table names it, but it is not in the tree")
                continue
            layer_scanned += 1
            scanned += 1
            # A file including its OWN header is not a layer edge: `src/toneUtil/ManTone.cpp` includes
            # `cpp-pinyin2/dialects/ManTone.h`, the public declaration of the very translation unit being
            # compiled. Without this rule, adding `dialects/` to the tone row's forbidden list (2026-10-20)
            # turned every implementation's own include into a violation -- a false positive that says
            # nothing about coupling.
            own_stem = path.stem
            for inc in includes_of(path):
                if inc.rsplit("/", 1)[-1].rsplit(".", 1)[0] == own_stem:
                    continue
                for needle in forbidden:
                    if needle in inc:
                        if (rel, needle) in LAYER_EXEMPT:
                            exempted.append(f"{rel} -> {inc!r} (registered: {LAYER_EXEMPT[(rel, needle)]})")
                            break
                        findings.append(
                            f"VIOLATION {rel}: includes {inc!r} (layer {layer} must not include "
                            f"{needle!r})")
                        break
        per_layer[layer] = layer_scanned
    # The other half of the same hole: a file nobody named was in NO layer, so no rule applied to it
    # and nothing said so. Three files were in exactly that state (src/Outcome.cpp and
    # src/toneUtil/ReadingCodec.{cpp,h}); they are in the table now, and this keeps the next one from
    # slipping through the same gap.
    named = set()
    for files, _ in LAYERS.values():
        named.update(files)
    for path in sorted(root.glob("src/**/*")):
        if path.suffix in (".cpp", ".h") and path.is_file():
            rel = path.relative_to(root).as_posix()
            if rel not in named:
                findings.append(
                    f"VIOLATION (uncovered) {rel}: no layer names this file, so no rule applies to it")
    findings.extend(check_public_headers(root)[0])
    findings.extend(check_public_surface_leaks(root)[0])
    for entry in exempted:
        print(f"note: registered upward edge {entry}")
    return findings, scanned, per_layer


def complete_fixture(tmp_root):
    """Materialise every file the layer table names, with harmless content.

    The first version of this self-test only created src/dict/, which was enough while three
    directories were scanned. Once the layer table covered the whole src/ tree it became a fixture
    that could not represent a clean tree at all: case 1 reported 25 "file is missing" notes and
    FAILED, and case 2 (a dict file including the engine) never even ran, because the dict file it
    planted was not in the list. A self-test whose fixture no longer matches the shape of a real
    tree is worse than none, so the fixture is now generated from the same table the check uses.
    """
    for files, _ in LAYERS.values():
        for rel in files:
            p = tmp_root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            if not p.exists():
                p.write_text("// fixture\n", encoding="utf-8")
    return tmp_root / "src" / "dict"


def main():
    root = Path(__file__).resolve().parent.parent

    if "--self-test" in sys.argv:
        ok = True
        cases = []
        total_files = sum(len(f) for f, _ in LAYERS.values())

        # 1. clean fixture: no finding at all, and every named file was really read
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            findings, scanned, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("clean fixture accepted",
                          not bad and scanned == total_files and not findings,
                          f"findings={len(findings)} scanned={scanned}/{total_files}"))

        # 2. the original rule: a dict file including the engine. The violation is planted in a file
        #    the table names (not in an extra file), so "found exactly one" also proves the table's
        #    own files are the ones being read.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "dict" / "PhraseIndex.h").write_text(
                '#include "engine/Engine.h"\n', encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("dict->engine caught (in a table-named file)", len(bad) == 1, f"{bad}"))

        # 3. a rule added on 2026-10-03: the c_api bridge reaching into dict privately
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "c_api_query.cpp").write_text(
                '#include "dict/PhraseIndex.h"\n', encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("c_api->dict caught", len(bad) == 1, f"{bad}"))

        # 4. another added rule: a dialect facade reaching into the engine's private header
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "Mandarin.cpp").write_text('#include "EngineImpl.h"\n',
                                                           encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("dialect->EngineImpl caught", len(bad) == 1, f"{bad}"))

        # 5. a file named by a rule but absent: must FAIL, not be reported as a note. This case used to
        #    pin the old behaviour (notes, exit 0) -- which is how a whole layer could vanish from the
        #    scan while the script still printed "AC-3 OK".
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            findings, scanned, _ = check_tree(tmp_root)
            missing = [f for f in findings if f.startswith("VIOLATION (missing)")]
            cases.append(("missing files are violations",
                          len(missing) == total_files and scanned == 0 and not [f for f in findings
                                                                                   if f.startswith("note:")],
                          f"{len(missing)}/{total_files} missing"))

        # 6. the same rule read from a different layer's list (dict may not include the tone layer
        #    either): without this, a bug that only ever applied the c_api list would still look green.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "dict" / "DictUtil.h").write_text(
                '#include "toneUtil/ManToneUtil.h"\n', encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("dict->toneUtil caught", len(bad) == 1, f"{bad}"))

        # 7. a NEW file in src/ that no layer names: the hole that let three files sit unruled. The
        #    fixture is built from the table, so planting one file must produce exactly one finding.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "NewThing.cpp").write_text("// nothing\n", encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION (uncovered)")]
            cases.append(("new unlisted src file caught", len(bad) == 1, f"{bad}"))

        # 8. the renamed/deleted half of the same hole.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "Result.cpp").unlink()
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION (missing)")]
            cases.append(("renamed layer file caught", len(bad) == 1, f"{bad}"))

        # 8b/8c. the src-side `core -> ext/` rule (added 2026-10-20). Until it existed,
        #    `src/Capabilities.h` could include the public `ext/Dialect.h` with BOTH scans blind: the core
        #    row's forbidden list had no `ext/`, and the public-header tier scan only reads `include/`.
        #    8b plants the real, legitimate edge and demands it stay allowed; 8c plants the same include in
        #    a DIFFERENT core file, which must be caught -- otherwise the marker would only be proving that
        #    one file is exempt, not that the rule reaches its neighbours.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "Capabilities.h").write_text(
                '#include <cpp-pinyin2/ext/Dialect.h>\n', encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("core->ext allowed on the registered file (Capabilities.h)", not bad, f"{bad}"))

        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            (tmp_root / "src" / "ToneSandhi.cpp").write_text(
                '#include <cpp-pinyin2/ext/Dialect.h>\n', encoding="utf-8")
            findings, _, _ = check_tree(tmp_root)
            bad = [f for f in findings if f.startswith("VIOLATION")]
            cases.append(("core->ext caught on any other core file", len(bad) == 1, f"{bad}"))

        # 9. the public-header tier scan (added 2026-10-10). Its fixtures are built here because the
        #    layer fixtures carry no `include/` tree at all -- and that is exactly case 9a: with no
        #    headers the edge guard must stay quiet (demanding an edge there was a false positive).
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            findings, _, _ = check_tree(tmp_root)
            cases.append(("no public headers -> no tier finding",
                          not [f for f in findings if "(public tier)" in f],
                          f"{[f for f in findings if '(public tier)' in f]}"))

        # 9b..9e: a tier fixture with the real tree's one exempt edge, then three planted shapes.
        tier_findings = {}
        for label, core_body, ext_body in (
            ("core->ext caught", '#include <cpp-pinyin2/ext/Other.h>\n', "#pragma once\n"),
            ("ext->core allowed", "#pragma once\n", "#include <cpp-pinyin2/core/Thing.h>\n"),
            ("exempt edge allowed", '#include <cpp-pinyin2/ext/Dialect.h>\n', "#pragma once\n"),
            ("unread headers caught", "#pragma once\n", "#pragma once\n"),
        ):
            with tempfile.TemporaryDirectory() as tmp:
                tmp_root = Path(tmp)
                complete_fixture(tmp_root)
                core = tmp_root / "include" / "cpp-pinyin2" / "core"
                ext = tmp_root / "include" / "cpp-pinyin2" / "ext"
                core.mkdir(parents=True)
                ext.mkdir(parents=True)
                (core / "Engine.h").write_text(core_body, encoding="utf-8")
                (core / "Thing.h").write_text("#pragma once\n", encoding="utf-8")
                (ext / "Dialect.h").write_text("#pragma once\n", encoding="utf-8")
                (ext / "Other.h").write_text(ext_body, encoding="utf-8")
                tier_findings[label] = [f for f in check_tree(tmp_root)[0] if "(public tier)" in f]
        cases.append(("a new core->ext edge is caught",
                      len(tier_findings["core->ext caught"]) == 1, f"{tier_findings['core->ext caught']}"))
        cases.append(("an ext->core edge is allowed",
                      not tier_findings["ext->core allowed"], f"{tier_findings['ext->core allowed']}"))
        cases.append(("the one exempt edge is allowed",
                      not tier_findings["exempt edge allowed"],
                      f"{tier_findings['exempt edge allowed']}"))
        cases.append(("headers with no edge at all are caught",
                      len(tier_findings["unread headers caught"]) == 1,
                      f"{tier_findings['unread headers caught']}"))

        # 9f. the `dialects/` tier is scanned AT ALL (added 2026-10-19). It used to be missing from
        # PUBLIC_TIERS, so every header under include/cpp-pinyin2/dialects/ was skipped outright: a
        # `dialects/ -> ext/` upward edge passed with the gate green. Measured before the fix: planting
        # `#include <cpp-pinyin2/ext/Dialect.h>` in dialects/ManTone.h left `check_layers` exiting 0.
        # The case stays planted so that coverage cannot disappear again without a red.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_root = Path(tmp)
            complete_fixture(tmp_root)
            core = tmp_root / "include" / "cpp-pinyin2" / "core"
            ext = tmp_root / "include" / "cpp-pinyin2" / "ext"
            dialects = tmp_root / "include" / "cpp-pinyin2" / "dialects"
            for tier_dir in (core, ext, dialects):
                tier_dir.mkdir(parents=True, exist_ok=True)
            (core / "Thing.h").write_text("#pragma once\n", encoding="utf-8")
            (ext / "Other.h").write_text("#pragma once\n", encoding="utf-8")
            (dialects / "Tone.h").write_text('#include <cpp-pinyin2/ext/Other.h>\n', encoding="utf-8")
            tier_bad = [f for f in check_tree(tmp_root)[0]
                        if "(public tier)" in f and "dialects" in f]
            cases.append(("dialects->ext caught (the tier used to be invisible)",
                          len(tier_bad) == 1, f"{tier_bad}"))

        # 10..14. the public-surface leak scan (added 2026-10-10). The layer fixture carries a src/ tree, so a
        # plant is one file under include/. Each case is a shape measured ABSENT on this tree, and each must
        # be caught, because "absent" is exactly what a later edit would change. Every header also declares a
        # type of its OWN (`Subject`), which the src file never does -- without that split, a fixture in which
        # both sides declare the same name makes the name public and the scan correctly stays quiet, which is
        # how case 10 first failed.
        leak_findings = {}
        for label, header_body in (
            # One leading space on purpose: namespace level in this tree, which is where a leak lives.
            ("declares a src-only type", "#pragma once\n struct InternalThing;\n"),
            ("declares a PUBLIC type", "#pragma once\n struct Subject;\n"),
            ("includes a src header", '#pragma once\n#include "../src/InternalThing.h"\n'),
            ("only MENTIONS a src type", "#pragma once\n/// built on InternalThing, which you cannot see\n"),
            # A forward declaration nested in a class body must NOT count: that is `class Impl;` inside a
            # public class, and treating it as a declaration made the real scan report the public `Registry`
            # as a leak (measured on this tree before the indentation was tightened).
            ("nested member forward decl", "#pragma once\n struct Subject {\n    class Impl;\n};\n"),
        ):
            with tempfile.TemporaryDirectory() as tmp:
                tmp_root = Path(tmp)
                complete_fixture(tmp_root)
                include = tmp_root / "include" / "cpp-pinyin2" / "core"
                include.mkdir(parents=True)
                body = header_body if header_body == "#pragma once\n struct Subject {\n    class Impl;\n};\n" \
                    else header_body + " struct Subject {\n    int y;\n};\n"
                (include / "Subject.h").write_text(body, encoding="utf-8")
                (tmp_root / "src" / "InternalThing.h").write_text("struct InternalThing { int x; };\n",
                                                                  encoding="utf-8")
                leak_findings[label] = [f for f in check_tree(tmp_root)[0] if "(public surface)" in f]
        cases.append(("a public header forward-declaring a src-only type is caught",
                      len(leak_findings["declares a src-only type"]) == 1,
                      f"{leak_findings['declares a src-only type']}"))
        cases.append(("a public header declaring a type it also owns is allowed",
                      not leak_findings["declares a PUBLIC type"],
                      f"{leak_findings['declares a PUBLIC type']}"))
        cases.append(("a public header including a src header is caught",
                      len(leak_findings["includes a src header"]) == 1,
                      f"{leak_findings['includes a src header']}"))
        cases.append(("naming a src-only type in a COMMENT is not a leak",
                      not leak_findings["only MENTIONS a src type"],
                      f"{leak_findings['only MENTIONS a src type']}"))
        cases.append(("a forward declaration nested in a class body is NOT a leak",
                      not leak_findings["nested member forward decl"],
                      f"{leak_findings['nested member forward decl']}"))

        for i, (name, passed, detail) in enumerate(cases, 1):
            print(f"self-test {i}/{len(cases)}: {name} -> {'ok' if passed else 'FAILED'} ({detail})")
            ok = ok and passed
        failed = [case for case in cases if not case[1]]
        print(f"SELF-TEST: {len(cases)} case(s), {len(failed)} failure(s)")
        if not cases:
            print("SELF-TEST: NO CASES RAN -- a self-test that checks nothing must not report success")
            return 1
        return 0 if ok else 1

    findings, scanned, per_layer = check_tree(root)
    for line in findings:
        print(line)
    violations = [f for f in findings if f.startswith("VIOLATION")]
    missing = [f for f in violations if f.startswith("VIOLATION (missing)")]
    print("AC-3 include check: layers = " +
          ", ".join(f"{k}:{v}" for k, v in per_layer.items()))
    print(f"AC-3 include check: {scanned} files scanned, {len(violations)} violation(s)")
    # Spell out what a zero scan means. Found while auditing this gate against an empty tree: it correctly
    # failed (every file its table names was missing) but the summary line read "0 files scanned, 28
    # violation(s)", which a reader can misread as "the rule was checked and holds". It did not check anything;
    # the only reason it is red is that its subject is gone.
    if scanned == 0:
        print(f"AC-3 include check: NOTHING WAS SCANNED -- all {len(missing)} file(s) the layer table names are "
              f"absent, so no rule could be applied. This is not a clean tree; it is an empty one.")
    elif missing:
        print(f"AC-3 include check: {len(missing)} of the violation(s) are files the layer table names but the "
              f"tree does not have, so those layers were not fully checked")
    if violations:
        return 1
    print("AC-3 OK: no lower-layer file includes an upper layer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
