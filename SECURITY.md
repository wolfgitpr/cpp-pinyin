# Security policy

## Supported versions

The **2.x line** is the current one and receives fixes. The **1.x line is frozen**: it is a different package
(`cpp-pinyin`), a different C ABI (`cppp_*`) and its sources are not developed here any more. If you are on 1.x,
the first thing a fix will ask you to do is move to 2.x.

The exact version of any build is available at run time: `cppp2_version()` from C, `CppPinyin2.Engine.Version`
from C#, `cpp_pinyin2.__version__` from Python. The C++ face has **no** version accessor on purpose -- the
version is not an ABI version, and `include/` deliberately carries no version constant (see
`docs/API_zh.md` section 10). Please quote the version in a report.

## Reporting a vulnerability

**Please do not open a public issue.** Use GitHub's [private security
advisory](https://github.com/wolfgitpr/cpp-pinyin/security/advisories/new) for this repository, which keeps the
report private until a fix ships.

A useful report contains:

- the version (`cppp2_version()` output, or the tag), the platform and triplet, and whether the build was shared
  or static;
- the smallest input that triggers it -- a string, a dictionary file, or a few lines against the C ABI;
- what happened versus what you expected (a crash, an uninitialised read, a wrong reading, a leak).

We will reply on the advisory thread, agree a disclosure date with you, and land the fix with a `CHANGELOG`
entry. Fixed advisories are credited unless you ask otherwise.

## What the attack surface actually is

Worth knowing before you spend time on a report, and worth checking when you change these areas:

- **Dictionary loading.** The library reads tables from a directory the caller supplies. A crafted or truncated
  table must fail as a classified error (`Code::Load`), never as a crash or an out-of-bounds read -- size and
  format checks live in `src/dict/` and `src/EngineImplLoad.cpp`, and the format is versioned by
  `format.txt`.
- **The C ABI boundary.** Handles, UTF-8 validation, `struct_size` gating and the library-owned frees are the
  contract (`include/cpp-pinyin2/ext/c_api.h`); every entry point catches exceptions instead of letting them
  cross the boundary. A double free, a use after `cppp2_destroy`, or a buffer the library does not own being
  freed by a caller is a bug worth reporting.
- **User dictionaries.** `user_dict.txt` is parsed at load time; a malformed line is a row-level failure, not a
  process failure.

Not part of the surface, so a report about them is a design discussion rather than a vulnerability: the library
makes **no network calls**, writes **no temporary files**, and reads **no environment variables** to decide
behaviour (the dictionary root is always an explicit argument).
