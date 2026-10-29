#!/usr/bin/env python3
"""Check the numbers this repository's documentation claims against the tree they describe.

Design rules, because a doc checker that guesses is worse than none -- a false positive gets ignored, and an
ignored gate protects nothing:

  * only claims this tool can MEASURE are in the table, and each names what measures it;
  * patterns are anchored on the exact surrounding words used in this repository. The first attempt --
    "three numbers separated by slashes" -- also matched dates, line numbers, bucket sizes and commit
    lists; nine of its twelve hits were wrong;
  * a deliberately HISTORICAL line carries `<!-- doc-claims: historical -->`; silence is not an exemption,
    and the marker shows up in review;
  * fenced code blocks are skipped -- they are commands, not claims;
  * the report always prints COVERAGE, so "0 problems" is never mistaken for "everything verified".

Usage:
    python tools/check_doc_claims.py --ctest-inventory <build-dir-or-json> [--root .] [--verbose]
    python tools/check_doc_claims.py --ctest-inventory <dir> --tools-root <repo> --root <sandbox>
    python tools/check_doc_claims.py --self-test --ctest-inventory <dir>

`--root` is where the DOCUMENTS are read from and `--tools-root` is where the MEASUREMENT comes from.
Separate on purpose: the negative control must scan a document with a planted wrong number while the tools
measure the real tree, and one path for both made that control return a setup error instead of the mismatch
it exists to produce.

Exit codes: 0 = every recognised claim matches; 1 = at least one mismatch, or nothing was recognised;
2 = setup failure.
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from pathlib import Path

HISTORICAL = "doc-claims: historical"

# ---------------------------------------------------------------------------- measurement


def measure_ctest(build_or_json, root):
    path = Path(build_or_json)
    if path.is_dir():
        out = subprocess.run(["ctest", "--test-dir", str(path), "--show-only=json-v1"],
                             capture_output=True, text=True, cwd=root)
        text = out.stdout
    else:
        raw = path.read_bytes()
        # Say WHICH problem it is: the two that actually happened on 2026-10-21 were a UTF-16 file (PowerShell's
        # `>` writes UTF-16) and an empty one (a redirect that ate ctest's output), and both looked identical.
        if not raw.strip():
            raise SystemExit(f"check_doc_claims: {path} is empty -- write ctest's JSON into it "
                             f"(ctest --test-dir <build> --show-only=json-v1 > file), or pass the build DIRECTORY "
                             f"instead and let this tool run ctest")
        if raw[:2] in (b"\xff\xfe", b"\xfe\xff") or (raw[:1] == b"\x00"):
            raise SystemExit(f"check_doc_claims: {path} is UTF-16, not UTF-8 -- a PowerShell `>` redirect writes "
                             f"UTF-16. Re-create it with `--show-only=json-v1 | Out-File -Encoding utf8`, or pass "
                             f"the build DIRECTORY instead")
        try:
            text = raw.decode("utf-8-sig")
        except UnicodeDecodeError as error:
            raise SystemExit(f"check_doc_claims: {path} is not UTF-8 ({error})") from None
    try:
        data = json.loads(text.lstrip("\ufeff"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"check_doc_claims: {path} is not ctest JSON ({error})") from None
    tests = data.get("tests", [])
    labels = {}
    for test in tests:
        for prop in test.get("properties", []):
            if prop.get("name") == "LABELS":
                for value in prop.get("value", []):
                    labels[value] = labels.get(value, 0) + 1
    return {"registered": len(tests), "slow": labels.get("slow", 0), "labels": labels}


def measure_bindings(root):
    out = subprocess.run([sys.executable, "tools/check_bindings.py"],
                         capture_output=True, text=True, cwd=root)
    blob = out.stdout + out.stderr
    found = {}
    for key, pattern in (("headers", r"frozen list \((\d+) headers\)"),
                         ("symbols", r"frozen symbol list \((\d+) symbols\)"),
                         ("declarations", r"frozen list \((\d+) declarations\)")):
        m = re.search(pattern, blob)
        if m:
            found[key] = int(m.group(1))
    if len(found) != 3:
        raise SystemExit(f"cannot measure the frozen surfaces from check_bindings.py:\n{blob}")
    return found


def measure_layers(root):
    out = subprocess.run([sys.executable, "tools/check_layers.py"],
                         capture_output=True, text=True, cwd=root)
    m = re.search(r"(\d+) files scanned", out.stdout + out.stderr)
    if not m:
        raise SystemExit("cannot measure the AC-3 scan surface from check_layers.py")
    return {"files_scanned": int(m.group(1))}


# The configuration the documents describe when they state a single number. It is a fixed path rather
# than "the build directory the caller passed", because the caller's build is the thing under test -- using
# it as the reference made the verdict depend on which build was handed in.
#
# OVERRIDABLE since 2026-10-27, because the error message below has told the reader to "point CANONICAL_BUILD
# at the configuration you want" since the message was written, and there was no way to do it: the constant
# is not a CLI flag and nothing read an environment variable. A message that offers an action the code does
# not have is worse than no message -- it is what a person reaches for when they are already stuck.
CANONICAL_BUILD = os.environ.get("CPP_PINYIN2_CANONICAL_BUILD", ".tmp/mk-default")


def measure_static(root):
    """Everything that can be measured from the tree alone: the frozen surfaces and the layer scan.

    Split out because the self-test must run with no build directory (it is in the always-on CTest suite, and
    a fresh checkout has no build), while the documents' counts do need one.
    """
    return {"bindings": measure_bindings(root), "layers": measure_layers(root)}


def measure_selftests(root):
    """Each gate's self-test CASE count, by running that gate's `--self-test` and reading its own output.

    Why this class exists: a self-test count drifts with no signal. The binding gate grew from 52 to 55
    cases and three places in the documents still said 52/52 (found 2026-10-10; the tool printed 55/55 the
    whole time).

    Why by RUNNING it rather than by parsing the source: two source-based versions of this measurement both
    over-counted by one -- `def expect(` is itself a match for `expect("` -- and the error was invisible
    until the numbers were compared against the tools' output. The tool's output is the authority, so the
    instrument reads it. Cost: a few seconds (measured 2.6 s for the four).
    """
    measured = {}
    unmeasurable: dict[str, str] = {}
    # TEMP and TMP are removed from the child environment: a self-test builds its fixtures with `tempfile`, and
    # when TEMP points inside this repository's `.tmp/` those fixtures land under a directory the tools SKIP as
    # a build artefact -- making them invisible, so cases that assert "this must fail" pass vacuously. The gate
    # suite exports exactly that TEMP, so measuring without this would report the environment's shape as a tool
    # defect. (Same reasoning as tools/run_all_gates.py, which learned it first.)
    environment = {key: value for key, value in os.environ.items() if key not in ("TEMP", "TMP")}
    for name, relative in (("check_bindings", "tools/check_bindings.py"),
                           ("merge_nupkgs", "csharp/tools/merge_nupkgs.py"),
                           ("check_wheel", "python/tools/check_wheel.py"),
                           ("check_links", "tools/check_links.py"),
                           ("check_public_surface", "tools/check_public_surface.py"),
                           ("check_layers", "tools/check_layers.py"),
                           ("audit_doc_anchors", "tools/audit_doc_anchors.py"),
                           ("check_gate_inventory", "tools/check_gate_inventory.py"),
                           # R11 (2026-10-20): named in STABILITY §0 with its case count, and a count that
                           # nothing runs is the drift this function exists to catch.
                           ("check_conformance_provenance", "tools/check_conformance_provenance.py"),
                           # 2026-10-20 (redundancy sweep): these three were ALSO named in STABILITY §0 with a
                           # case count but were absent from this list, so their claims were judged against
                           # nothing and passed. That is not hypothetical: check_doc_refs printed "6 controls"
                           # while its self-test ran 7, and both the tool and the document said 6 -- a copy
                           # error with no gate able to see it. Measured now: 5 / 7 / 5.
                           ("check_source_list", "tools/check_source_list.py"),
                           ("check_doc_refs", "tools/check_doc_refs.py"),
                           ("check_doc_claims", "tools/check_doc_claims.py")):
        path = root / relative
        # A tool that cannot be measured is REPORTED, not skipped. Skipping was the first behaviour, and it is
        # the same false-green shape the coverage table had: with the tool dropped from the measurement, a
        # document claiming a stale count for it is judged against nothing and passes. Measured 2026-10-10 by
        # pointing this function at a tree with no tools/: it returned {} and said nothing.
        if not path.is_file():
            unmeasurable[name] = f"no such file: {relative}"
            continue
        completed = subprocess.run([sys.executable, "-B", str(path), "--self-test"],
                                   cwd=root, capture_output=True, text=True, timeout=600,
                                   env=environment)
        if completed.returncode != 0:
            # Carry the tool's OWN last words. "exits 1" is a fact that names nothing, and on the linux job it
            # cost two rounds: the count that could not be measured belonged to `check_wheel`, whose self-test
            # was failing for a reason only it knew (measured 2026-10-22).
            tail = [line for line in (completed.stdout + completed.stderr).splitlines() if line.strip()]
            reason = f"its --self-test exits {completed.returncode}"
            if tail:
                reason += " -- last line: " + tail[-1].strip()[:160]
            unmeasurable[name] = reason
        cases = len(re.findall(r"^(?:PASS|FAIL)\s+self-test:", completed.stdout, flags=re.M))
        if cases:
            measured[name] = cases
            continue
        # check_bindings prints one summary line instead of per-case lines.
        summary = re.search(r"self-test:\s*(\d+)\s*/\s*(\d+)", completed.stdout)
        if summary:
            measured[name] = int(summary.group(2))
            continue
        # The other gates state the count without a slash, as `self-test: N case(s), M failure(s)`. Before
        # 2026-10-10 they printed `0 failure(s)` alone, a shape that reads the same whether nine cases passed or
        # none ran; both forms are accepted here so a document may cite either while the tools move.
        counted = re.search(r"self-test:\s*(\d+)\s*case\(s\)", completed.stdout)
        if counted:
            measured[name] = int(counted.group(1))
            continue
        # check_layers shouts its summary (`SELF-TEST: N case(s)`), which the case-insensitive patterns above
        # deliberately do not catch: its per-case lines are lowercase, so a case-insensitive search would count
        # the summary as a case.
        shouted = re.search(r"^SELF-TEST:\s*(\d+)\s*case\(s\)", completed.stdout, flags=re.M)
        if shouted:
            measured[name] = int(shouted.group(1))
            continue
        # check_source_list states its count after the verdict (`SELFTEST PASS -- 5 case(s) reported above`).
        # Deliberately requires the verdict word: a tool that skipped could print a count of zero cases.
        shout_case = re.search(r"^SELFTEST (?:PASS|FAIL) -- (\d+) case\(s\)", completed.stdout, flags=re.M)
        if shout_case:
            measured[name] = int(shout_case.group(1))
            continue
        # check_doc_refs counts CONTROLS, splitting them into green and red (`PASS (7 controls: 3 green, 4 red)`).
        # The split is not parsed: what a document quotes is the total, and a green/red imbalance is the gate's
        # own business, not a number to freeze in prose.
        controls = re.search(r"--self-test: (?:PASS|FAIL) \((\d+) controls", completed.stdout)
        if controls:
            measured[name] = int(controls.group(1))
            continue
        # Reached only when a tool accepted --self-test and printed no shape this function reads. That is a
        # measurement failure, not an absence of information.
        unmeasurable[name] = "it accepts --self-test but prints no count this function reads"
    return measured, sorted(unmeasurable.items())


def measure_gates(root: Path) -> "dict[str, int]":
    """How many gates `tools/run_all_gates.py` actually runs.

    Added 2026-10-27 after a poison test: the checklist said "all 21 gates pass" while the runner had 20,
    and this gate stayed green -- the number was stated in prose and measured by nothing. The runner is
    IMPORTED, not parsed: it has a `__main__` guard, so importing it runs nothing, and an import cannot
    disagree with the list it is counting.
    """
    tools_dir = str(root / "tools")
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    import run_all_gates                                        # noqa: PLC0415 -- deliberate, see docstring

    return {"total": len(run_all_gates.GATES)}


def measure_all(inventory, root):
    """`inventory` is measured for reference (labels/registry shape); the canonical counts come from the
    configuration named above, so a run against a static build is still judged against the default."""
    selftests, unmeasurable = measure_selftests(root)
    measured = {"ctest": measure_ctest(inventory, root), **measure_static(root),
                "selftests": selftests, "unmeasurable_selftests": unmeasurable,
                "gates": measure_gates(root)}
    canonical = root / CANONICAL_BUILD
    if (canonical / "CMakeCache.txt").is_file():
        try:
            measured["canonical"] = measure_ctest(str(canonical), root)
        except Exception as exc:                          # noqa: BLE001
            raise SystemExit(f"the canonical build at {canonical} exists but could not be read: {exc}")
    else:
        raise SystemExit(
            f"the canonical build is missing at {canonical}. It is what the documents' headline numbers "
            f"describe, so the check cannot run without it -- run the pre-delivery checklist first "
            f"(bash tools/board/run_local_gate.sh --checklist .tmp/mk-default), or set "
            f"CPP_PINYIN2_CANONICAL_BUILD to the configuration you want the documents judged against.")
    return measured


def measure_siblings(inventory, root, measured):
    """The same counts for the configurations the documentation also quotes.

    Measured in round 10, and it is the reason this function exists: the registered count is a function of
    the configuration, not a property of the tree.

        min (no C ABI export gate)        28
        default                           30
        default + examples                31

    Whole directories are used rather than `ctest -N` on new ones, so this costs no build. A sibling that
    is not present is simply not offered, and the checker's error message says what it compared against --
    guessing a configuration from the tree's absence would be worse than a slightly weaker check.
    """
    siblings = {}
    for label, rel in (("min", ".tmp/mk-static"), ("default+examples", ".tmp/mk-examples")):
        build = Path(rel) if Path(rel).is_absolute() else (root / rel)
        if (build / "CMakeCache.txt").is_file():
            try:
                siblings[label] = measure_ctest(str(build), root)
            except Exception as exc:                      # noqa: BLE001 -- a sibling is best-effort
                print(f"note: could not measure sibling {label} at {build}: {exc}")
        else:
            print(f"note: sibling '{label}' not present at {build}; values it covers are not checked")
    return siblings


# ---------------------------------------------------------------------------- claims
#
# A claim is (id, description, pattern, groups, key, name_groups).
#   groups      : 1-based indices of the captured groups holding the stated number(s), in the order the
#                 measured vector uses. None means "all captured groups".
#   key         : what measures it -- ("bindings",) for the vector, or (section, field).
#   name_groups : 1-based index of a group that names the thing being counted (per-label claims), else ().

def claims():
    # `groups` maps a pattern's captures into the ORDER of the measured vector (`expected_for`), and it is
    # spelled out even where it is plain 1,2,3: the three frozen-surface phrasings below do NOT share one
    # text order, and leaving that implicit is what made a true sentence a mismatch (S17).
    return [
        # The frozen surfaces, in the three phrasings this repository actually uses.
        # Text order is (headers, declarations, symbols); the measured vector is (headers, symbols,
        # declarations). So the captures are reordered -- "15 headers / 18 declarations / 20 symbols" IS the
        # gate's own output and must pass. Until S17 this claim compared the two orders slot by slot, which
        # agreed only while declarations == symbols: the gate was rejecting the very wording it prints.
        ("frozen-printed", "the gate's own output: 'N headers ... M declarations ... K symbols'",
         r"(\d+)\s*headers.*?(\d+)\s*declarations.*?(\d+)\s*symbols", (1, 3, 2), ("bindings",), ()),
        ("frozen-plus", "the frozen surfaces as 'N 公开头 + M C ABI [+符号] + K 插件面声明'",
         r"(\d+)\s*公开头\s*\+\s*(\d+)\s*C\s*ABI(?:\s*符号)?\s*\+\s*(\d+)\s*插件面声明", (1, 2, 3), ("bindings",), ()),
        ("frozen-slash", "the frozen surfaces as 'N 公开头 / M C ABI [+符号] / K 插件面声明'",
         r"(\d+)\s*公开头\s*/\s*(\d+)\s*C\s*ABI(?:\s*符号)?\s*/\s*(\d+)\s*插件面声明", (1, 2, 3), ("bindings",), ()),
        # AC-3's scan surface, in the phrasings used here.
        ("ac3-scan-floor", "AC-3 scan surface: '从 10 个文件补到全 src/ 的 25 个'",
         r"从\s*10\s*个文件补到全\s*src/\s*的\s*(\d+)\s*个", (1,), ("layers", "files_scanned"), ()),
        ("ac3-scan", "AC-3 scan surface: 'N 文件 0 违例'",
         r"(\d+)\s*文件\s*0\s*违例", (1,), ("layers", "files_scanned"), ()),
        ("ac3-scanned", "AC-3 scan surface: 'N files scanned'",
         r"(\d+)\s*files scanned", (1,), ("layers", "files_scanned"), ()),
        # Registered entries. "N 注册" alone is too weak (it also matches "23 行 / 44 文件 的注册"), so the
        # two canonical phrasings are listed instead.
        ("gate-registered", "registered CTest entries: '注册用例数：N'",
         r"注册用例数[：:]\s*\**\s*(\d+)", (1,), ("ctest", "registered"), ()),
        ("gate-registered-en", "registered CTest entries: 'N registered'",
         r"(\d+)\s*registered\b", (1,), ("ctest", "registered"), ()),
        # How many gates the runner runs (2026-10-27). The phrasing is the runner's own output line, which
        # is what a document quotes: `RESULT: all N gates pass, and each gate's own self-test passes`.
        # Poisoned before this existed: the document said 20, the runner had 21, and nothing noticed.
        ("gate-total", "the runner's gate count, as 'all N gates pass'",
         r"all\s+(\d+)\s+gates\s+pass", (1,), ("gates", "total"), ()),
        # Per-label counts as stated in prose: "`abi` 6 条". A stale label NAME is caught too, because the
        # name is looked up in the same inventory the counts come from.
        ("label-counts", "per-label selection counts, as '`<label>` N 条'",
         r"`([a-z0-9]+)`\s*(\d+)\s*条", (2,), ("ctest-label",), (1,)),
        # A gate's own `--self-test` count. Only the TOTAL is taken: the tools print `N/M` with both sides
        # equal today, and the measurement reads the case count. The claim's number capture must not sit to
        # the left of the separator with a plain `(\d+)`, because the tool's own name carries digits
        # (`check_bindings` -> "5") and `\b` does not stop that greed in front of CJK text -- both mistakes
        # were made and measured while authoring this entry.
        # NOT included: a bare "N/M" with no tool named (the documents use 22/22 and 14/14 for ctest runs too).
        ("selftest-slash", "a gate's self-test count, as '`<tool>` … --self-test N/M'",
         r"([a-z_]+)(?:\.py)?`?[^\d]{0,6}(?:--)?self-test[^\n]{0,6}?(\d+)\s*/\s*(\d+)", (3,), ("selftest",), (1,)),
        ("selftest-cases-zh", "a gate's self-test count, as '`<tool>` 自检 N 条'",
         # `\**` before the digits (2026-10-20): the document bolds these counts (`自检 **7** 条`), and without
         # this the whole line was unmatchable -- measured by poisoning one count from 7 to 8 and watching the
         # gate stay green. A claim pattern that cannot match the document's own formatting is not a claim.
         r"`([a-z_]+)(?:\.py)?`[^\n]{0,20}自检[^\n]{0,3}?\**\s*(\d+)\s*条", (2,), ("selftest",), (1,)),
        # The binding gate is also referred to by its ROLE in prose ("绑定门禁 **52/52**") without naming the
        # file, so the file-name pattern above misses it. Measured 2026-10-10: three such places still said
        # 52/52 while the tool printed 55/55. Restricted to that one role name on purpose -- "自检 N/N" alone
        # is what the documents use for ctest runs too.
        ("selftest-role", "the binding gate's count, as '绑定门禁 N/M'",
         r"绑定门禁[^\n]{0,12}\**(\d{1,3})\s*/\s*(\d{1,3})", (2,), ("selftest",), ("check_bindings",)),
        # NOT included: "N 运行". That phrase means both "entries the gate runs" (registered minus slow)
        # and "tests a given ctest invocation ran", and the tool cannot tell which, so it does not guess.
    ]


def expected_for(key, measured):
    if key == ("selftest",):
        # Per-tool, so the claim's name group selects the row; supplied here as a callable's input.
        return None
    if key == ("bindings",):
        # The authoritative order of the frozen-surface vector -- headers, symbols, declarations, i.e. the
        # order `tools/check_bindings.py` names them in. Every claim's `groups` maps its own text order into
        # THIS order; a claim that forgets still compares numbers slot by slot, which silently "works"
        # whenever two of the three are equal (measured: this hid a false positive until S17, when the tree
        # read 15 headers / 18 declarations / 20 symbols).
        return [measured["bindings"]["headers"], measured["bindings"]["symbols"],
                measured["bindings"]["declarations"]]
    section, field = key
    if section == "ctest" and "canonical" in measured:
        return measured["canonical"][field]
    return measured[section][field]


def check_one(claim, match, measured, siblings=None):
    """(ok, measured_value_or_list, stated_value_or_list) for one matched claim."""
    _cid, _desc, _rx, groups, key, name_groups = claim
    indices = groups if groups is not None else tuple(range(1, match.re.groups + 1))
    stated = [int(match.group(i)) for i in indices if match.group(i) is not None]
    if key == ("selftest",):
        # A claim may name its tool in the text (name_groups) or fix it in the table above (a literal name,
        # which is what the role-name phrasing needs: "绑定门禁" does not contain the file name).
        first = name_groups[0] if name_groups else None
        name = match.group(first) if isinstance(first, int) else first
        if not isinstance(name, str):
            return False, ["this claim names no tool"], stated
        actual = measured["selftests"].get(name)
        if actual is None:
            # A tool this measurement does not run is not judged: the class covers the gates whose
            # `--self-test` output is machine-readable, not every tool in tools/.
            return True, [f"{name}: not measured"], stated
        if actual == stated[0]:
            return True, [f"{name}: {actual}"], stated
        return False, [f"{name}: {actual} today"], stated
    if name_groups:
        name = match.group(name_groups[0])
        actual = measured["ctest"]["labels"].get(name)
        candidates = {"current": actual}
        for label, sib in (siblings or {}).items():
            candidates[label] = sib.get("labels", {}).get(name)
        if actual is None and not any(v is not None for v in candidates.values()):
            return False, [f"label '{name}' not in the registry"], stated
        # A label's size is a function of the configuration too (measured: `abi` is 6 by default and 5 in
        # the min build, because ExportGate is not registered without shared libraries), so a document
        # quoting either value is describing a real configuration. The report names which one matched.
        for label, value in candidates.items():
            if value == stated[0]:
                where = "current" if label == "current" else f"{value} ({label} configuration)"
                return True, [where], stated
        return False, [f"{k}: {v}" for k, v in candidates.items() if v is not None], stated
    want = expected_for(key, measured)
    if isinstance(want, list):
        ok = stated == want
    else:
        # A count that is a function of the configuration: accept it if it matches the configuration under
        # test OR one of its measured siblings, and name the sibling in the message so the report cannot be
        # read as "this number is configuration-independent". Measured in round 10: registered is 28 / 30 /
        # 31 for min / default / default+examples, so a document quoting 31 is not wrong -- it is describing
        # another configuration, and the report should say which.
        ok = stated[0] == want
        alternatives = {"current": want}
        for label, sib in (siblings or {}).items():
            alternatives[label] = sib.get(key[1]) if key[0] == "ctest" else None
        if not ok and key[0] == "ctest":
            for label, value in alternatives.items():
                if label != "current" and value == stated[0]:
                    ok = True
                    want = f"{value} ({label} configuration)"
                    break
            if not ok:
                want = ", ".join(f"{v} ({k})" for k, v in alternatives.items() if v is not None)
        return ok, want, stated
    return ok, want, stated


# ---------------------------------------------------------------------------- configuration fingerprints
# The playbook table quotes, per build configuration, the entry count and a fingerprint of the entry SET; a new
# test entry moves both, and nothing checked them (hand-measured once, then "待重算" for weeks). This reads the
# table back and recomputes each row from the build directory it names.
# Pitfall: read back by the FINGERPRINT column, not by the row label -- matching labels saw only 3 of the 4 rows
# and still reported "all four agree".

CONFIG_FINGERPRINTS = (
    ("默认", ".tmp/mk-default"),
    ("+ `-DCPP_PINYIN2_BUILD_EXAMPLES=ON`", ".tmp/mk-examples"),
    ("+ `-DBUILD_SHARED_LIBS=OFF`", ".tmp/mk-static"),
    ("+ 两者同时", ".tmp/mk-min"),
)
CONFIG_FINGERPRINT_DOC = "docs/NEW_DIALECT_PLAYBOOK.md"


def measured_fingerprint(build):
    """(total, fast, fingerprint) from one build, or None when that build directory does not exist."""
    directory = Path(build)
    if not (directory / "CMakeCache.txt").is_file():
        return None

    def entry_names(extra=()):
        completed = subprocess.run(["ctest", "--test-dir", build, "-N", *extra],
                                   capture_output=True, text=True, timeout=600)
        text = completed.stdout + completed.stderr
        return re.findall(r"^\s*Test\s+#\d+:\s*(\S+)\s*$", text, flags=re.M)

    names = entry_names()
    if not names:
        return None
    fast = entry_names(("-LE", "slow"))
    digest = hashlib.sha256("\n".join(sorted(names)).encode("utf-8")).hexdigest()[:16]
    return len(names), len(fast), digest


def check_config_fingerprints(root, build_root=None):
    """(checked, findings, skipped) for the playbook's configuration table.

    `root` is where the DOCUMENTS live; `build_root` (default: the same) is where the BUILDS live. The two are
    separate so a self-test can poison a copy of the document in a sandbox and still have the measurement read
    the real builds -- without that, the only way to test this check would be to corrupt a real build tree.

    A configuration whose build directory is absent is SKIPPED, and the caller prints that: this measurement
    needs four configured trees, and two of them (examples, static) are not part of a normal checkout. Saying
    "skipped" is the honest answer; failing would make the check unusable, and passing silently would make it
    a lie of the kind this whole file exists to prevent.
    """
    build_root = Path(build_root) if build_root is not None else root
    document = root / CONFIG_FINGERPRINT_DOC
    if not document.is_file():
        return 0, [], [], []
    body = document.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(r"^\| ([^|]+) \| \*\*(\d+)\*\* \| \*\*(\d+)\*\* \| `([0-9a-f]{16})`", body, flags=re.M)
    stated = {}
    for label, total, fast, digest in rows:
        for prefix, build in CONFIG_FINGERPRINTS:
            if label.strip().startswith(prefix) or (prefix == "默认" and label.strip().startswith("默认")):
                stated[build] = (label.strip(), int(total), int(fast), digest)
    findings = []
    skipped = []
    unverifiable = []
    checked = 0
    for _prefix, build in CONFIG_FINGERPRINTS:
        if build not in stated:
            findings.append(f"{CONFIG_FINGERPRINT_DOC}: the table has no row this check can read for {build}")
            continue
        label, total, fast, digest = stated[build]
        live = measured_fingerprint(build_root / build)
        if live is None:
            skipped.append(f"{build} (not configured)")
            # A row the document STATES whose build is absent has been verified against nothing. That is not a
            # pass, and it must not read like one: reported separately so the run says out loud which rows are
            # unverified. `rows` counts what the table claims; this counts what nothing could confirm.
            unverifiable.append(f"{label} (no build at {build})")
            continue
        checked += 1
        if live[2] != digest:
            findings.append(f"{label}: fingerprint says {digest}, the build measures {live[2]}")
        if live[0] != total:
            findings.append(f"{label}: entry count says {total}, the build has {live[0]}")
        if live[1] != fast:
            findings.append(f"{label}: `-LE slow` count says {fast}, the build has {live[1]}")
    return checked, findings, skipped, unverifiable


# ---------------------------------------------------------------------------- tool inventory
#
# A tool that exists but is named nowhere is invisible to everyone who did not write it. The check is coarse
# on purpose -- "does the filename appear in one of these documents" -- because finer judgement is what
# produces false positives by the hundred.
#
# The list holds TRACKED documents only. A local file (the board harness README, say) is not part of a clone,
# so a tool "named" only there is still a tool nobody else can find.

DOCUMENTS_FOR_TOOLS = (
    "docs/NEW_DIALECT_PLAYBOOK.md",
    "`docs/DECISIONS.md` §2.1",
    "docs/DECISIONS.md",
    "README.md",
    # Round 133/136: added because this list was missing the one document whose job IS describing what lives in this
    # repository -- it already named check_bindings.py twenty times and two other gates besides. The five generation
    # gates landed in tools/ (3501d42) were named in none of the six, so the rule could not see them; the fix belongs to
    # the document list, and this comment exists so nobody has to wonder whether it was widened to silence a failure.
    # Round 2 (2026-10-20): the gate list with each tool's `--self-test` count lives in STABILITY §0 --
    # that is where a new gate is documented, and it was the one document this list did not read, so a
    # gate documented there still counted as "named nowhere". Widened for the document, not to silence
    # a failure: `check_doc_refs.py` is named in STABILITY §0 and in `tests/CMakeLists.txt`.
    "docs/STABILITY.md",
)


def iter_tools(root):
    """(filename, directory) for every TRACKED tool, falling back to the filesystem outside a checkout.

    TRACKED, because the question is "can someone who clones this repository find this tool". A script that only
    exists on the author's machine (the board harness) is not part of that question -- and walking the filesystem
    made it part of it, which is how six tracked tools ended up documented only in an untracked README.

    The fallback matters: `--self-test` builds its cases in a sandbox directory that is not a git checkout, and it
    must still measure something rather than pass on an empty subject.
    """
    completed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--", "tools"],
                              capture_output=True, text=True)
    names = []
    if completed.returncode == 0 and completed.stdout:
        for entry in completed.stdout.split("\0"):
            if not entry:
                continue
            path = pathlib.Path(entry)
            if path.suffix not in (".py", ".sh") or path.name.startswith(("_", ".")) or path.name == "README.md":
                continue
            names.append((path.name, path.parent.as_posix()))
        if names:
            return sorted(names)
    for directory in (root / "tools", root / "tools" / "board"):
        if not directory.is_dir():
            continue
        for path in sorted(directory.iterdir()):
            if not path.is_file() or path.name == "README.md":
                continue
            if path.suffix not in (".py", ".sh") or path.name.startswith(("_", ".")):
                continue
            names.append((path.name, directory.relative_to(root).as_posix()))
    return names


def check_tool_inventory(root):
    """(ok, [(name, directory), ...], examined) for tools whose filename appears in none of the documents.

    `examined` is the number of tool files actually looked at, and a run that examined ZERO is not ok. The
    first version returned (True, []) whenever `tools/` was absent or held nothing to check -- measured
    2026-10-10 against a tree with empty `tools/`: "every tool under tools/ is named in at least one document"
    about zero tools. A check that passes on an empty subject is the failure mode this file exists to remove.
    """
    bodies = []
    for rel in DOCUMENTS_FOR_TOOLS:
        path = root / rel
        bodies.append(path.read_text(encoding="utf-8", errors="replace") if path.is_file() else "")

    undocumented = []
    examined = 0
    for name, directory in iter_tools(root):
        examined += 1
        if not any(name in body for body in bodies):
            undocumented.append((name, directory))
    if examined == 0:
        return False, [("(nothing)", "tools/")], 0
    return (not undocumented), undocumented, examined


# ---------------------------------------------------------------------------- scan + report


def scan(root):
    # `docs/HISTORY.md` is EXCLUDED (user ruling, decision Q12, 2026-10-06): a ledger of historical readings
    # ("16 headers / 16 C ABI" and older) that were true when written and are deliberately not rewritten, so
    # scanning it red-flagged documents that are not claims about the current tree. The exclusion is about the
    # ledger's nature, not its tracking status (it became TRACKED on 2026-10-09); every other TRACKED document
    # outside that directory is still scanned, and `--self-test` proves the check can go red.
    # `plans/` holds the working ledgers and quote measurements of their own day; `history/` is the archive
    # beside them and gets the same treatment (2026-10-20, docs/history migration design §3.3 / ruling D1).
    targets = sorted(p for p in root.glob("docs/**/*.md")
                     if not ({"plans", "history"} & set(p.parts)))
    for extra in (root / "README.md", root / "tools" / "board" / "README.md"):
        if extra.is_file():
            targets.append(extra)
    # `tools/board/README.md` joins the scan because it QUOTES numbers (it is where the gate suite's own
    # readings are written down) while it used to be read only for tool NAMES -- and that difference hid a
    # live contradiction: it said the canonical configuration registers 32 entries, a few lines above a command
    # that already passed `--expect-total 36`. Measured 2026-10-10: running the existing patterns over that one
    # document fires on exactly two lines and both were stale, so adding it costs no false positives.

    compiled = [(tuple(claim[:5]), claim[5], re.compile(claim[2])) for claim in claims()]
    found = []
    for path in targets:
        in_fence = False
        for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if line.strip().startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence or HISTORICAL in line:
                continue
            for head, name_groups, rx in compiled:
                m = rx.search(line)
                if m:
                    found.append((path.relative_to(root).as_posix(), line_no, line.strip(),
                                  head + (name_groups,), m))
    return found


#: A fenced block whose symbols are deliberately historical (the block shows what a name USED to be).
#: Same discipline as `doc-claims: historical`: a visible, reviewable marker rather than a silent pass.
STALE_SYMBOLS = "doc-symbols: stale"


def declared_cppp_names(root):
    """Every `cppp_*` / `CPPP_*` name a public header declares.

    The C ABI's own namespace, so this needs no judgement about which names are functions, structs or
    typedefs: a name in that namespace that no header declares is a name a reader cannot use.
    """
    declared = set()
    for path in sorted((root / "include").rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        declared |= set(re.findall(r"\b(cppp\d*_[a-z_0-9]+)\s*\(", text))
        declared |= set(re.findall(r"\b(cppp\d*_[a-z_0-9]+)\s*[;,)]", text))
        declared |= set(re.findall(r"\b(?:struct|typedef[^;]*?)\s(cppp\d*_[a-z_0-9]+)", text))
        declared |= set(re.findall(r"\b(CPPP\d*_[A-Z0-9_]+)\b", text))
    return declared


def scan_names(root):
    """`cppp_*` names used inside a fenced block that no public header declares.

    Why fenced blocks specifically: a doc's prose is allowed to DISCUSS a name that no longer exists
    ("`cppp_create` became `cppp2_open`"), while a code block is what a reader copies. Measured
    2026-10-10: 36 such names across the docs, of which exactly one was undeclared -- and it was in
    prose (`docs/BACKLOG.md`), i.e. the opposite of what this check reports. That is why the check
    is scoped to fenced blocks rather than to every mention.

    Returns (findings, checked, declared). `checked` is reported even when it is zero, because a scan
    that read nothing must not look like a scan that found nothing.
    """
    declared = declared_cppp_names(root)
    if not declared:
        return ["the public headers declare no `cppp_*` name at all -- the scan is not reading them"], 0, declared
    # `plans/` holds the working ledgers and quote measurements of their own day; `history/` is the archive
    # beside them and gets the same treatment (2026-10-20, docs/history migration design §3.3 / ruling D1).
    targets = sorted(p for p in root.glob("docs/**/*.md")
                     if not ({"plans", "history"} & set(p.parts)))
    targets += [path for path in (root / "README.md",) if path.is_file()]
    findings = []
    checked = 0
    for path in targets:
        fence_line = ""
        in_fence = False
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if line.strip().startswith("```"):
                in_fence = not in_fence
                fence_line = line if in_fence else ""
                continue
            if not in_fence or STALE_SYMBOLS in fence_line:
                continue
            for name in re.findall(r"\b(cppp\d*_[a-z_0-9]+|CPPP\d*_[A-Z0-9_]+)\b", line):
                checked += 1
                if name not in declared:
                    findings.append(
                        f"{path.relative_to(root).as_posix()}:{number} uses '{name}', which no public "
                        f"header declares (mark the block `<!-- {STALE_SYMBOLS} -->` if it deliberately "
                        f"shows an old name)")
    return findings, checked, declared


def coverage(measured, found, judge=True):
    """Every fact this gate measures -> who claims it. Returns the unclaimed ones as FINDINGS, not a note.

    `judge=False` is for the self-test's sandbox, which is a deliberately minimal fixture: it holds one document
    and a zero-entry inventory, so most facts are zero and unclaimable by construction. Judging there reports the
    fixture's shape rather than a gap in the tree -- the first version of this did exactly that and turned the
    gate's own self-test red with four coverage mismatches, which is why the flag exists.

    Why findings at all: a measurement nobody claims is a measurement nothing protects -- a new fact can be added
    to this table and stay uncovered indefinitely, because the table only announced gaps and announcing is not
    checking. Measured 2026-10-10: two rows sat unclaimed (the registered entry count, and AC-3's file count)
    until a human read the table twice and wrote claims for them. Returning them as problems makes the next one
    fail the run instead.

    NOT DONE, and measured before rejecting: a general "this line looks like a claim but no pattern matched"
    warning. Two versions were prototyped against the real document set -- keyword-plus-digit anywhere on the
    line raised 157 candidates, and requiring the keyword to touch the digits raised **254**, nearly all of them
    ordinary prose ("resolved by the library", "37 条短语"). The corpus states numbers too freely for that rule
    to be anything but noise, and a checker that cries wolf gets turned off. This table is the rule that works:
    it is closed (six measured facts), so every row can be checked and every gap fails.

    A row is claimed when some recognised claim's ID starts with the row's prefix. The label row is special: it
    counts how many labels the inventory knows about are actually NAMED in a recognised prose claim.
    """
    claimed = {rec[3][0] for rec in found}
    stated_labels = len({rec[4].group(1) for rec in found if rec[3][0] == 'label-counts'})
    rows = [("registered CTest entries", measured["ctest"]["registered"], "gate-registered"),
            # How many of the labels the inventory knows about are actually NAMED in a recognised prose
            # claim. The first version counted label names among the claim IDs, which read 0 by
            # construction -- a coverage line that cannot be right is worse than no coverage line.
            ("labels named in a claim", stated_labels, "label-counts"),
            ("frozen header count", measured["bindings"]["headers"], "frozen-"),
            ("frozen symbol count", measured["bindings"]["symbols"], "frozen-"),
            ("frozen declaration count", measured["bindings"]["declarations"], "frozen-"),
            ("AC-3 files scanned", measured["layers"]["files_scanned"], "ac3-")]
    unclaimed = []
    print("coverage (fact -> measured value -> claimed by):")
    for name, value, prefix in rows:
        hit = next((cid for cid in sorted(claimed) if cid.startswith(prefix)), None)
        print(f"  {name:<28} {str(value):>6}   {hit if hit else '** NOT CLAIMED ANYWHERE **'}")
        if hit is None and judge:
            unclaimed.append((name, value, prefix))
    return unclaimed


def report(root, measured, verbose, siblings=None, build_root=None, judge_coverage=True):
    """Scan `root`, compare every recognised claim against `measured`, print, and return an exit code.

    Extracted from main() so that the self-test can run the same code path against a sandbox copy of the
    documents while the measurements still come from the real tree. Keeping one copy of this logic is the
    point: a self-test that re-implements the check would prove the wrong thing.

    `judge_coverage` is passed straight through to `coverage()`: the self-test runs a deliberately minimal
    fixture (one document, a zero-entry inventory), where most facts are zero and unclaimable by construction,
    so judging coverage there reports the fixture's shape instead of a gap in the tree.
    """
    found = scan(root)
    problems = []
    # A tool this run could not measure is reported before anything is judged: claims about its self-test
    # count would otherwise be compared against nothing and pass. See measure_selftests().
    for name, why in measured.get("unmeasurable_selftests", []):
        # THE REASON COMES FIRST. Readers truncate this line (the gate runner keeps 160 characters) and the
        # boilerplate that used to lead it was 130 of them, so the only part that names the problem never
        # reached the log (measured 2026-10-23).
        problems.append((f"measurement/{name}", 0, "unmeasurable",
                         f"{why} (so a document claim about {name}'s self-test count is judged against "
                         f"nothing)", ["a number"], ["nothing"], ""))
    for rel, line_no, line, claim, match in found:
        ok, want, stated = check_one(claim, match, measured, siblings)
        if ok:
            if verbose:
                print(f"  ok   {rel}:{line_no}  {claim[0]}: {stated}")
        else:
            problems.append((rel, line_no, claim[0], claim[1], stated, want, line))

    print(f"claims recognised and checked: {len(found)}")
    print()
    for name, value, prefix in coverage(measured, found, judge=judge_coverage):
        problems.append((f"coverage/{name}", 0, "coverage",
                         f"this gate measures {name} = {value} and no document claims it (expected a claim "
                         f"whose id starts with {prefix!r}); write a claim or drop the measurement",
                         [value], ["nothing"], ""))

    # Configuration fingerprints: the playbook's table of entry counts + entry-set fingerprints, recomputed
    # from the build directories it names. Rows whose build is absent are SKIPPED and said out loud, because
    # two of the four configurations are not part of a normal checkout.
    checked_cfg, cfg_findings, cfg_skipped, cfg_unverifiable = check_config_fingerprints(root, build_root)
    print()
    print(f"configuration fingerprints: {checked_cfg} row(s) recomputed from their build"
          + (f", {len(cfg_skipped)} skipped: {', '.join(cfg_skipped)}" if cfg_skipped else ""))
    for finding in cfg_findings:
        print(f"  MISMATCH {finding}")
        problems.append((CONFIG_FINGERPRINT_DOC, 0, "config-fingerprint",
                         "a build configuration's count or fingerprint drifted", [-1], [-1], ""))
    for entry in cfg_unverifiable:
        # NOT a failure: the two extra configurations are absent from a normal checkout by design, and failing
        # would make the gate unusable. But it is printed as UNVERIFIED so a reader cannot mistake a skipped row
        # for a checked one -- the whole point of the round-14 exercise.
        print(f"  UNVERIFIED  {entry}: the document states this row and no build here can confirm it")
    if not cfg_findings and not cfg_skipped:
        print("  every configuration row agrees with its build")

    # Tool inventory: every tool under tools/ must be named in a document a reader would look in. The gate is
    # skipped when the root has no tools/ at all (a sandbox fixture); when the directory EXISTS it must yield
    # something to examine, which check_tool_inventory reports as not-ok on its own.
    if (root / "tools").is_dir():
        ok_tools, undocumented, examined = check_tool_inventory(root)
        print()
        if ok_tools:
            print(f"tool inventory: all {examined} tool(s) under tools/ are named in at least one document")
        else:
            print(f"tool inventory: {len(undocumented)} of {examined} examined tool(s) are named in no document:")
            for name, where in undocumented:
                print(f"  UNDOCUMENTED  {where}/{name}")
                problems.append((f"{where}/{name}", 0, "tool-inventory",
                                 "this tool exists but no document names it", [-1], [-1], ""))

    # The second signal, and a different KIND of claim: not a number but a NAME a reader would copy out
    # of a code block. Scoped to fenced blocks because prose is allowed to discuss retired names.
    if (root / "include").is_dir():
        name_findings, names_checked, declared = scan_names(root)
        print()
        for finding in name_findings:
            print(f"  UNDECLARED  {finding}")
        problems.extend((finding.split(":")[0], 0, "undeclared-name", finding, [-1], [-1], "")
                        for finding in name_findings)
        print(f"fenced-block names: {names_checked} cppp_* uses checked against "
              f"{len(declared)} declared names")

    if not found and not problems:
        # "no numeric claim matched" is only a vacuity warning when the NAME signal found nothing either:
        # a fixture (or a document set) whose only defect is an undeclared name must report that finding,
        # not a message about claims it never had. Measured: the name-class self-test printed this warning
        # while correctly returning red, which read as if the gate were confused about what it had seen.
        print()
        print("WARNING: no claims matched at all, so this is a FAILURE and not a pass -- a checker that")
        print("matches nothing reports zero problems forever. Check the patterns against how the")
        print("documents phrase things.")
        return 1

    if problems:
        print()
        for rel, line_no, cid, desc, stated, want, line in problems:
            print(f"  MISMATCH {rel}:{line_no}  [{cid}] {desc}")
            print(f"           document says {stated}, measured {want}")
            print(f"           line: {line[:110]}")
        print()
        print(f"RESULT: {len(problems)} claim(s) disagree with the tree. Fix the number, or -- if the line is")
        print(f"        a historical record -- mark it with <!-- {HISTORICAL} --> on that line.")
        return 1

    print()
    print("RESULT: every recognised claim matches what the tree measures")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ctest-inventory", default=None,
                        help="a build dir (preferred) or a JSON file from --show-only=json-v1; required "
                             "unless --self-test is given, which needs no build")
    parser.add_argument("--tools-root", default=None,
                        help="where the measurement commands run (default: --root)")
    parser.add_argument("--root", default=".", help="where the documents are read from")
    parser.add_argument("--verbose", action="store_true", help="list every recognised claim")
    parser.add_argument("--self-test", action="store_true",
                        help="plant a wrong number in a sandbox and require the checker to fail")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    tools_root = Path(args.tools_root).resolve() if args.tools_root else root

    if args.self_test:
        return self_test(args.ctest_inventory, tools_root)

    if not args.ctest_inventory:
        parser.error("--ctest-inventory is required unless --self-test is given")

    try:
        measured = measure_all(args.ctest_inventory, tools_root)
    except (SystemExit, json.JSONDecodeError, FileNotFoundError) as exc:
        print(f"ERROR: could not measure the tree: {exc}", file=sys.stderr)
        return 2

    print(f"canonical ({CANONICAL_BUILD}): {measured['canonical']['registered']} registered, "
          f"{measured['canonical']['slow']} labelled slow")
    # Say it out loud when the reference configuration is not the documented one. Overriding it is a
    # deliberate act ("judge the documents against THIS"), and the outcome is that a document stating the
    # DEFAULT configuration's number disagrees -- which is the override working, not a stale document.
    # Without this line the two look identical in the report (measured 2026-10-27 by pointing the
    # canonical at the examples-on tree: one registered-count mismatch and nothing explaining why).
    if os.environ.get("CPP_PINYIN2_CANONICAL_BUILD"):
        print("note: CPP_PINYIN2_CANONICAL_BUILD is set, so the documents are judged against "
              f"{CANONICAL_BUILD} rather than the documented default. A claim that states the default "
              "configuration's number will therefore disagree, and that disagreement is this override "
              "doing its job.")
    print(f"under test    ({args.ctest_inventory}): {measured['ctest']['registered']} registered")
    print(f"frozen surfaces {measured['bindings']['headers']}/{measured['bindings']['symbols']}/"
          f"{measured['bindings']['declarations']}; AC-3 scans {measured['layers']['files_scanned']} files")
    print()

    siblings = measure_siblings(args.ctest_inventory, tools_root, measured)
    return report(root, measured, args.verbose, siblings, tools_root)


def self_test(inventory, tools_root):
    """Require BOTH directions: a planted wrong number must be reported, the gate's own wording must pass.

    Four synthetic fixtures, none copied from the repository, all built under the system temporary directory
    so a self-test never leaves anything behind in the tree it is checking:

      * `verbatim`      -- the gate's own output, built from the numbers just measured -> must be recognised
                           and must PASS (a false positive is what gets a checker switched off);
      * `wrong-printed` -- the same phrasing with one number changed                   -> must be reported;
      * `wrong-plus`    -- the '公开头 + C ABI + 插件面声明' phrasing, wrong           -> must be reported.

    The first control is not decorative: until S17 the `frozen-printed` claim captured its numbers in text
    order and compared them against the measured vector in a different order, so a document quoting the gate
    verbatim was reported as a mismatch -- a gate rejecting its own output, invisible while two of the three
    counts happened to be equal.
    """
    import tempfile

    # No `measure_all` here on purpose: that would demand the canonical build. The self-test answers "can the
    # checker catch a planted wrong number", which needs the tree's frozen surfaces and nothing else.
    measured = measure_static(tools_root)
    try:
        measured["ctest"] = measure_ctest(inventory, tools_root) if inventory else \
            {"registered": 0, "slow": 0, "labels": {}}
    except Exception as exc:                                  # noqa: BLE001 -- a stub is enough here
        print(f"note: no usable CTest inventory passed ({exc}); using a stub for the self-test")
        measured["ctest"] = {"registered": 0, "slow": 0, "labels": {}}

    with tempfile.TemporaryDirectory() as tmp:
        sandbox = Path(tmp)

        def fixture(name, body):
            """One sandbox holding a single synthetic document."""
            docs = sandbox / name / "docs"
            docs.mkdir(parents=True)
            (docs / "PROBE.md").write_text(
                "# synthetic fixture for the doc-claims self-test\n\n" + body, encoding="utf-8")
            return sandbox / name

        # A fixture written here, not copied from the repository: an earlier version copied a session ledger
        # out of a document that `git archive` skips -- it passed in the working tree and failed on the
        # board with "No such file or directory". Copying a real document also makes the self-test break
        # whenever that document's wording changes. Still valid after that ledger became TRACKED (commit
        # df35c2a, 2026-10-09): the reason is archive membership, not tracking. Layout: the retired plan's §7 (docs/REPO_LAYOUT.md, deleted 2026-10-23).
        header = measured["bindings"]["headers"]
        symbols = measured["bindings"]["symbols"]
        declarations = measured["bindings"]["declarations"]
        verbatim = fixture("verbatim",
                           f"the C++ surface gate prints {header} headers / {declarations} declarations / "
                           f"{symbols} symbols today.\n")
        wrong_printed = fixture("wrong-printed",
                                f"the C++ surface gate prints {header + 1} headers / {declarations} "
                                f"declarations / {symbols} symbols today.\n")
        wrong_plus = fixture("wrong-plus", "15 公开头 + 16 C ABI + 17 插件面声明\n")
        wrong_slash_symbol = fixture(
            "wrong-slash-symbol", "15 公开头 / 16 C ABI 符号 / 17 插件面声明\n")

        # Guard against a fixture silently no longer being recognised: if the checker would not match this
        # phrasing at all, the control would "pass" by finding nothing, which is the failure mode this whole
        # tool exists to avoid. The claim is looked up by id through the real `scan`, so this guard cannot
        # drift away from the claim it guards.
        for name, cid in (("verbatim", "frozen-printed"), ("wrong-printed", "frozen-printed"),
                          ("wrong-plus", "frozen-plus"),
                          ("wrong-slash-symbol", "frozen-slash")):
            seen = sorted({rec[3][0] for rec in scan(sandbox / name)})
            if cid not in seen:
                print(f"SELF-TEST FAILED: fixture '{name}' is not recognised as '{cid}' "
                      f"(recognised: {seen or 'nothing'}), so it proves nothing")
                return 1

        print("== self-test: the gate's own output, quoted verbatim, must be accepted ==")
        code = report(verbatim, measured, verbose=True, judge_coverage=False)
        if code != 0:
            print()
            print(f"SELF-TEST: FAILED -- a verbatim quote of the gate's own output was rejected "
                  f"(exit {code}); the S17 false positive is back")
            return 1
        print()
        print("SELF-TEST: ok -- the verbatim line was recognised and passed")

        for name, note in (("wrong-printed", "the same phrasing, one number changed"),
                           ("wrong-plus", "the '公开头 + C ABI + 插件面声明' phrasing, wrong"),
                           ("wrong-slash-symbol", "the '公开头 / C ABI 符号 / 插件面声明' phrasing, wrong")):
            print()
            print(f"== self-test: a planted wrong number must be reported ({note}) ==")
            code = report(sandbox / name, measured, verbose=True, judge_coverage=False)
            if code != 1:
                print()
                print(f"SELF-TEST: FAILED -- the planted mismatch in '{name}' produced exit {code}, expected "
                      f"1 (its claim was recognised above, so this is not a 'found nothing' pass)")
                return 1
            print()
            print(f"SELF-TEST: ok -- the planted mismatch in '{name}' was reported")

        # The self-test-count class, with a synthetic measurement so this control stays hermetic: it must
        # catch a stale count (the real tree had three of those the day the class was added) and must accept
        # the current one, without this self-test depending on how many cases some tool happens to have.
        measured["selftests"] = {"check_bindings": 55, "merge_nupkgs": 6}
        stale_count = fixture("stale-count", "`check_bindings.py --self-test` 52/52\n")
        fresh_count = fixture("fresh-count", "`check_bindings.py --self-test` 55/55\n")
        role_phrase = fixture("role-phrase", "绑定门禁 **52/52** ✓\n")
        for name, cid in (("stale-count", "selftest-slash"), ("fresh-count", "selftest-slash"),
                          ("role-phrase", "selftest-role")):
            seen = sorted({rec[3][0] for rec in scan(sandbox / name)})
            if cid not in seen:
                print(f"SELF-TEST FAILED: fixture '{name}' is not recognised as '{cid}' "
                      f"(recognised: {seen or 'nothing'}), so it proves nothing")
                return 1
        if report(fresh_count, measured, verbose=False, judge_coverage=False) != 0:
            print("SELF-TEST: FAILED -- the CURRENT self-test count was rejected")
            return 1
        for name in ("stale-count", "role-phrase"):
            if report(sandbox / name, measured, verbose=False, judge_coverage=False) != 1:
                print(f"SELF-TEST: FAILED -- the stale self-test count in '{name}' was not reported")
                return 1
        print()
        print("SELF-TEST: ok -- a stale self-test count is reported, in both the file-name and the "
              "role-name phrasing, and the current one passes")

        # The runner's gate count (2026-10-27). Same shape as the block above and for the same reason: this
        # axis was added because a poison test showed the number was measured by nothing, so the control
        # that has to exist is the one that would have caught it.
        measured["gates"] = {"total": 21}
        fresh_gates = fixture("fresh-gates", "RESULT: all 21 gates pass, and each gate's own self-test passes\n")
        stale_gates = fixture("stale-gates", "RESULT: all 20 gates pass, and each gate's own self-test passes\n")
        if "gate-total" not in sorted({rec[3][0] for rec in scan(sandbox / "stale-gates")}):
            print("SELF-TEST FAILED: the 'all N gates pass' line is not recognised as 'gate-total', so the "
                  "control proves nothing")
            return 1
        if report(fresh_gates, measured, verbose=False, judge_coverage=False) != 0:
            print("SELF-TEST: FAILED -- the CURRENT gate count was rejected")
            return 1
        if report(stale_gates, measured, verbose=False, judge_coverage=False) != 1:
            print("SELF-TEST: FAILED -- a stale gate count was not reported")
            return 1
        print()
        print("SELF-TEST: ok -- a stale gate count is reported and the current one passes")

        # The name class. Its fixtures need an `include/` tree, because `report` only runs the name scan
        # when one is present (a sandbox without headers is not a tree that can declare anything).
        name_root = sandbox / "names"
        (name_root / "docs").mkdir(parents=True)
        (name_root / "include" / "cpp-pinyin2" / "ext").mkdir(parents=True)
        (name_root / "include" / "cpp-pinyin2" / "ext" / "c_api.h").write_text(
            "#pragma once\nextern \"C\" {\nint cppp_real(int);\n}\n", encoding="utf-8")
        (name_root / "docs" / "PROBE.md").write_text(
            "# synthetic\n\n"
            "```c\nint x = cppp_real(1);\n```\n\n"
            "```c\nint x = cppp_gone(1);\n```\n\n"
            "```c <!-- doc-symbols: stale -->\nint x = cppp_gone(1);\n```\n\n"
            "prose may discuss `cppp_gone` freely.\n",
            encoding="utf-8")
        name_findings, names_checked, name_declared = scan_names(name_root)
        gone_findings = [finding for finding in name_findings if "cppp_gone" in finding]
        cases = [
            ("an undeclared name in a block is caught",
             len(gone_findings) == 1, f"{name_findings}"),
            ("a declared name in a block is accepted",
             not any("cppp_real" in finding for finding in name_findings), f"{name_findings}"),
            # Two blocks use `cppp_gone`; only the unmarked one is a finding, which is what the marker is for.
            ("a block marked stale is exempt",
             len(gone_findings) == 1, f"{gone_findings}"),
            ("prose may name a retired symbol",
             not any(":12" in finding for finding in name_findings), f"{name_findings}"),
            ("the scan reports how many names it read",
             names_checked > 0 and len(name_declared) >= 1,
             f"checked={names_checked} declared={sorted(name_declared)}"),
        ]
        for label, passed, detail in cases:
            print(f"{'PASS' if passed else 'FAIL'}  self-test: {label}" + ("" if passed else f"  -> {detail}"))
            if not passed:
                print("SELF-TEST: FAILED on the name class")
                return 1
        if report(name_root, measured, verbose=False, judge_coverage=False) != 1:
            print("SELF-TEST: FAILED -- the planted undeclared name did not make the gate red")
            return 1
        print("SELF-TEST: ok -- an undeclared name in a fenced block is reported, a stale-marked block "
              "and prose are not")

        print()
        print("SELF-TEST: ok -- verbatim gate output passes; planted wrong numbers are reported in both "
              "phrasings. The real tree is checked separately, by the run that passes --ctest-inventory.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
