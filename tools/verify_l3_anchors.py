#!/usr/bin/env python3
"""Re-verify the L3 compression's DURABLE claim, and say plainly which claim has expired.

Written as a separate script rather than a patch to the session's throwaway `.tmp/l3_anchors.py`, because
the two answer different questions and the old tool's wording actively misleads now:

    .tmp/l3_anchors.py verify  ->  "TREE MISMATCH ... REAL CONTENT MISMATCH in 8 file(s)"

Every one of those words is true and none of them is a problem. The branch advanced by nine commits after
the compression, and the eight files named are exactly the ones this session edited. What the compression
promised has two parts with different lifetimes, and only one of them can still be checked against HEAD:

  DURABLE   `backup/v2-pre-squash2^{tree}` and `b587992^{tree}` both equal the pre-rewrite tree
            `6cb88cd039c1b3cb08d45843f6eb2f73292943ad`. Both commits are frozen objects, so this stays
            checkable forever and it does not care where the branch has been taken since.

            ⚠️ 2026-10-09: the object store no longer holds EITHER of them -- the repository's git repair
            pruned the unreachable history they lived in (see .tmp/git-repair-2026-10-09/README.md), and the
            three `v2-pre-*` branches were deleted under the user's authorization to clean up the backup
            branches. The evidence was re-homed: `.tmp/full-2026-10-09.bundle` contains every ref and
            resolves both `b587992` and `6cb88cd0…` (measured). So in this clone the run now ends in
            `VERDICT: UNVERIFIABLE HERE`, which is the honest reading -- not a pass, and not a regression.

  EXPIRED   "HEAD still holds that tree." True only until the next commit. It is information -- how far the
            branch has moved and what its tree is now -- and reporting it as a verdict is how a red light
            gets ignored, or worse, how someone "fixes" a history that is correct.

Exit code 0 iff no anchor present in this clone contradicts the durable claim (an empty anchor set is
reported as UNVERIFIABLE, not as a pass).
"""

import shutil
import subprocess
import sys
from pathlib import Path

TARGET_TREE = "6cb88cd039c1b3cb08d45843f6eb2f73292943ad"      # the pre-rewrite tree, from the L3 record
REWRITE_TIP = "b587992"                                       # the rewrite's last commit
EXPECTED_TRACKED_FILES = 464                                  # from .tmp/v2-presquash2-manifest.txt


GIT_CANDIDATES = (
    r"C:\Program Files\Git\cmd\git.exe",
    r"C:\Program Files\Git\bin\git.exe",
    r"C:\Program Files (x86)\Git\cmd\git.exe",
)


def find_git():
    """git from PATH, else from the places Git for Windows actually installs it.

    This exists because the first version assumed PATH and the pre-delivery checklist's PATH has
    `Git/usr/bin`, which does NOT contain git.exe -- so the tool failed with WinError 2 and the checklist
    reported it as "the anchors no longer hold", the opposite of the truth. Resolving here means the tool
    does not depend on somebody else's PATH being right.
    """
    found = shutil.which("git")
    if found:
        return found
    for candidate in GIT_CANDIDATES:
        if Path(candidate).is_file():
            return candidate
    return None


GIT = find_git()


def git(*args):
    if GIT is None:
        print("ERROR: git not found on PATH or in the usual Git for Windows locations; this check needs",
              file=sys.stderr)
        print("       git to read the frozen anchors, and cannot guess whether they hold without it.",
              file=sys.stderr)
        raise SystemExit(2)
    r = subprocess.run([GIT, *args], capture_output=True, text=True, cwd=r"D:\projects\cpp-pinyin2")
    return r.stdout.strip() if r.returncode == 0 else None


def main():
    print("== durable claims (these do not expire) ==")
    ok = True
    waived = []
    for label, rev in (("backup/v2-pre-squash2 (frozen pre-rewrite)", "backup/v2-pre-squash2"),
                       ("the rewrite's tip", REWRITE_TIP)):
        tree = git("rev-parse", f"{rev}^{{tree}}")
        if tree is None:
            # WAIVED, not failed (decision B, 2026-10-06; the ruling was recorded in the commit-compression
            # ledger, since retired -- docs/HISTORY.md). `backup/v2-pre-squash2` was removed
            # from this clone by the compression work itself, so this clone cannot check that half of
            # the durable claim -- and reporting a clone's inventory as a repository regression is the
            # kind of red that stops being read. The other half is still checked, and any anchor that
            # IS present is still compared below: a present anchor holding a different tree is a FAIL.
            print(f"  WAIVED   {label}: {rev} is not in this clone -- nothing to compare (not a failure)")
            waived.append(rev)
            continue
        same = tree == TARGET_TREE
        print(f"  {'SAME ' if same else 'DIFF '} {label}: {tree}")
        ok = ok and same

    count = git("ls-tree", "-r", "--name-only", "backup/v2-pre-squash2")
    files = len(count.splitlines()) if count else None
    if files is not None:
        same = files == EXPECTED_TRACKED_FILES
        print(f"  {'SAME ' if same else 'DIFF '} tracked files in the frozen anchor: {files} "
              f"(expected {EXPECTED_TRACKED_FILES})")
        ok = ok and same

    if ok and not waived:
        print(f"  => both frozen commits hold the pre-rewrite tree {TARGET_TREE} with {EXPECTED_TRACKED_FILES}")
        print("     tracked files: the compression preserved the tree, and this still holds.")
    elif ok and len(waived) == 2:
        # Nothing was compared, so do NOT print "every anchor present holds the tree": with an empty set
        # that sentence is vacuously true and reads as a clean bill of health. It is the failure mode this
        # script was written to avoid (a green light nobody can act on), one level deeper.
        print("  => NO anchor to compare: both frozen anchors are absent from this clone, so this run says")
        print("     NOTHING about the claim either way. See the VERDICT below for where its evidence is.")
    elif ok:
        print(f"  => every anchor present in this clone holds the pre-rewrite tree {TARGET_TREE}")
        print(f"     WAIVED (absent from this clone): {', '.join(waived)}")
    else:
        print("  => the frozen anchors no longer describe the pre-rewrite tree. That WOULD contradict")
        print("     see docs/HISTORY.md -- investigate, do not ignore.")

    print()
    print("== the claim that expired with the next commit (information, not a verdict) ==")
    head_short = git("rev-parse", "--short", "HEAD")
    head_tree = git("rev-parse", "HEAD^{tree}")
    ahead = git("rev-list", "--count", f"{REWRITE_TIP}..HEAD")
    if ahead is None:
        # The rewrite tip is not in this clone either (both frozen anchors are pruned), so "how far past
        # it" has no answer here. Say that instead of printing `None`.
        print(f"  HEAD is {head_short} (tree {head_tree}); {REWRITE_TIP} is not in this clone, so the")
        print("  distance to the rewrite tip cannot be counted here -- that is information, not a failure.")
    else:
        print(f"  HEAD is {ahead} commit(s) past the rewrite tip ({head_short}, tree {head_tree})")
    print("  Any difference between that tree and the pre-rewrite one is the work committed since, so it is")
    print("  EXPECTED. `.tmp/l3_anchors.py verify` reports such differences as 'REAL CONTENT MISMATCH',")
    print("  which is why this script exists: that wording reads as a regression and is not one.")
    print()
    if not ok:
        print("VERDICT: FAIL")
        return 1
    if len(waived) == 2:
        # The honest case, and the one this clone is in since 2026-10-09: BOTH frozen anchors were
        # pruned, so nothing was compared. Reporting "PASS (…; 2 waived)" here would be a green light
        # that asks the reader to notice a parenthetical, which is how a gate stops meaning anything.
        print(f"VERDICT: UNVERIFIABLE HERE -- both frozen anchors ({TARGET_TREE} / the commits holding it)")
        print("  are absent from this clone, so nothing was compared. The claim itself is NOT")
        print("  contradicted; its evidence now lives outside the object store:")
        print("  .tmp/full-2026-10-09.bundle holds every ref -- fetch it into a scratch repo to check.")
        return 0
    print(f"VERDICT: PASS (durable claim holds for the anchors present; {len(waived)} waived)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
