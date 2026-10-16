# Contributing to the AZ-MODS examples

These mods show how to use the SDK. An example is small, uses only documented services,
follows the SDK's [Guidelines](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/GUIDELINES.md),
times anything with the monotonic clock rather than by counting ticks, and lists in its README
which services it shows and where.

## Build and test

With the [SDK](https://github.com/Kyle-Hosman/az-mods-sdk) checked out next to this
repository and Docker running:

```sh
for m in set-clock deck-overview library-browser level-meter surface-demo deck-events; do
    ../az-mods-sdk/tools/azmod-pack build $m
done
tests/run.sh
```

On Windows, build in PowerShell as the [README](README.md#build-and-test) shows and run
`tests/run.sh` in Git Bash.

`tests/run.sh` runs every mod in a host harness, under the sanitizers, beside broken copies
that must fail; `tests/sanitize.sh` runs the host sanitizer passes. [TESTING.md](TESTING.md)
describes both and how to try the mods on a deck or the emulator.

## Style

C11, formatted with clang-format 21.1.8 and the repository's `.clang-format`
(`pip install clang-format==21.1.8`, then `git ls-files '*.c' | xargs clang-format -i`); CI
checks it. Comments explain why the code is the way it is, not what it does line by line.

## Adding a mod

1. A folder named after the mod's id with its source, `azmod.json`, icon, `Makefile` and a
   README with the "what it shows" table.
2. A release workflow, `.github/workflows/release-<id>.yml`, copied from an existing one.
3. A scenario in `tests/scenarios.c` (compiled into the SDK's harness) and a broken copy
   that the scenario must catch.
4. A row in the README's tables.

## Hygiene

Every push and pull request runs `tools/public-hygiene`, which keeps personal names, dates,
private paths and em dashes out of code, docs and commit messages (rules in `.public-hygiene.json`). `tools/install-hooks.sh` installs the same check as a pre-push hook.

By contributing you agree that your contribution is licensed under the repository's
license (GPL-3.0-only).
