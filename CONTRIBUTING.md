# Contributing

Thanks for looking. This is the short version of what a change has to satisfy; the long version lives in the
documents it points at, and nothing here restates their numbers.

## Before you open a pull request

```bash
# 1. The canonical build configuration -- the one the measured numbers in the docs refer to.
cmake -S . -B .tmp/mk-default -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
      -DCPP_PINYIN2_BUILD_TESTS=ON -DCPP_PINYIN2_INSTALL=ON -DCPP_PINYIN2_BUILD_EXAMPLES=OFF
cmake --build .tmp/mk-default
ctest --test-dir .tmp/mk-default

# 2. Every repository gate, including the ones that need that build tree.
ctest --test-dir .tmp/mk-default --show-only=json-v1 > .tmp/mk-default-inventory.json
python tools/run_all_gates.py --with-build

# 3. Formatting (the gate in step 2 fails on drift).
python tools/check_format.py --fix

# 4. The Python shell's own suite (needs the staged native payload first).
python python/tools/build_native.py
python -m pytest python/tests
```

On **Windows PowerShell**, `>` writes UTF-16 and the gate cannot read the inventory: use
`ctest --test-dir .tmp/mk-default --show-only=json-v1 | Set-Content -Encoding utf8 .tmp/mk-default-inventory.json`
instead.

Step 2 must be green except for findings that belong to a **different repository** (the doc-token gate scans a
wider tree). If you cannot run one of the steps on your machine, say so in the pull request rather than
skipping it silently: a gate nobody ran is not a gate that passed.

## What a change has to keep true

- **Three frozen surfaces.** `tests/abi/expected_public_headers.txt`, `expected_c_api_symbols.txt` and
  `expected_plugin_surface.txt` are the authority for what a consumer may rely on. A change to one of them is
  made in the **same commit** as the change it describes; the gates refuse the alternative.
- **One place per fact.** Counts, version strings and measured numbers live in exactly one document (the
  pointer map is `docs/README.md`), and every other mention is a pointer. If you need a number twice, you are
  probably looking at a missing pointer.
- **The C ABI grows, it does not get reshaped.** `docs/CONTRACT.md` section 1 has the rules: symbols may be
  added inside a major line, option structs grow through `struct_size`, output structs do not grow at all.
- **Dialect facts belong to the dialect.** Styles, capabilities and the tone-sandhi pass are fields on `Dialect`
  (`core/Types.h`, `ext/Dialect.h`), not assumptions inside the engine or the C layer.
- **Comments explain why.** A comment that restates the code is noise; one that records a measurement, a
  rejected alternative or a trap is the reason this codebase is readable. `docs/DECISIONS.md` §2.1
  is the record of the incidents those comments come from.

## Commits and pull requests

One batch, one commit, and the message says **what changed and why** -- `git log` is the project's memory.
Include the evidence you ran (command and exit code) for anything you claim. Do not reformat unrelated files in
a behavioural change, and do not mix a format pass into a fix.

## Adding a dialect

`docs/NEW_DIALECT_PLAYBOOK.md` is the executable procedure (dictionaries, styles, the gates that will tell you
what you forgot). A new dialect does **not** require touching the engine.

## Reporting a security issue

See [`SECURITY.md`](SECURITY.md) -- please do not open a public issue for a vulnerability.
