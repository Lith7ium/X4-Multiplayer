# Repository layout

Source code lives under `src/`, development and packaging tools under `tools/`.
Packages are not affected: a built package still contains `extension/x4_lan_lab`,
`x4lan/`, `launcher.py` and, for dedicated sets, `x4lan-dedicated` at its root.
Only the source paths moved.

| Folder | Contents | Former path |
| --- | --- | --- |
| `src/extension/` | the X4 extension `x4_lan_lab` (Lua UI, MD scripts, libraries, prebuilt `native/` modules) | `extension/` |
| `src/x4lan/` | Python relay and server runtime, copied into every package | `x4lan/` |
| `src/rust/` | Rust workspace: `x4lan-net`, `x4lan-relay` (incl. `x4lan-dedicated`), `x4mp-agent`, `x4mp-save`, `parity/` | `rust/` |
| `src/native/` | C sources: `net.c`, the frame-diet engine module, `vk-discard` | `native/` |
| `tools/dev/` | `bootstrap.py`, `check.py`, validators, native probes and harnesses | `dev/` |
| `tools/distribution/` | package builders, launchers and setup scripts, package documents | `distribution/` |
| `tests/` | Python test suite, run by `tools/dev/check.py` | unchanged |
| `docs/`, `reports/`, `onboarding/` | documentation, evidence, contributor guides | unchanged |

Archived reports keep the paths that were valid when they were written.

## Everyday commands

```sh
python3 tools/dev/bootstrap.py
.venv/bin/python tools/dev/check.py
cd src/rust && cargo build --release && cargo test --release
python3 tools/distribution/build_packages.py <new-folder> --runtime-cache <cache>
```

`tools/dev/check.py` puts `src/` and `tools/` on `sys.path`, so tests keep importing
`x4lan.*`, `dev.*` and `distribution.*`. For a bare `unittest` run, use
`PYTHONPATH=tests:src:tools:.`. Build `src/rust` in release mode before
`check.py`: the native-transport tests need `src/rust/target/release/libx4lan_net.so`.

## Converting a patch series written for the old layout

Rewrite the file headers of `git format-patch` output, then apply it as usual:

```sh
sed -E -i \
  -e '/^(diff --git |--- a\/|\+\+\+ b\/)/ s#( [ab])/(extension|x4lan|rust|native)/#\1/src/\2/#g' \
  -e '/^(diff --git |--- a\/|\+\+\+ b\/)/ s#( [ab])/(dev|distribution)/#\1/tools/\2/#g' \
  -e 's#^(rename|copy) (from|to) (extension|x4lan|rust|native)/#\1 \2 src/\3/#' \
  -e 's#^(rename|copy) (from|to) (dev|distribution)/#\1 \2 tools/\3/#' \
  *.patch
git am -3 *.patch
```

Only header lines change; patch content is left alone. Code inside the patch that
builds a source path itself, such as `ROOT / 'extension/...'` or
`Path(__file__).resolve().parents[1]` in a moved file, still needs a manual edit:
add `src/` or `tools/` to the path, and add one to `parents[N]` for files that moved
one level deeper. Paths inside a package (`root / 'extension/x4_lan_lab'`) stay as
they are.
