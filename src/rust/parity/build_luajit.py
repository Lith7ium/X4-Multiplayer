"""Build a LuaJIT 2.1 DLL named lua51_64.dll (Windows x64, MSVC) for native_parity.py.

X4 ships its LuaJIT runtime as lua51_64.dll; net.c and the Rust port find it by that
module name. The sources come from the luajit-src crate (vendored LuaJIT 2.1) in the
local cargo registry, so no extra download tool is involved:

  cargo fetch is enough when the crate is already cached; otherwise run once:
    cargo install --list >NUL & cargo search luajit-src   (or add it to any scratch crate)
  python src/rust/parity/build_luajit.py

Output: src/rust/target/luajit/lua51_64.dll (ignored build output).
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

RUST = Path(__file__).resolve().parents[1]
OUT = RUST / "target/luajit"


def find_sources() -> Path:
    registry = Path(os.environ.get("CARGO_HOME", Path.home() / ".cargo")) / "registry/src"
    candidates = sorted(registry.glob("*/luajit-src-*/luajit2/src/msvcbuild.bat"))
    if not candidates:
        sys.exit("luajit-src not found in the cargo registry; fetch the crate first (see docstring)")
    return candidates[-1].parents[1]


def vcvars() -> Path:
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    root = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                          capture_output=True, text=True, check=True).stdout.strip()
    return Path(root) / "VC/Auxiliary/Build/vcvars64.bat"


def main():
    source = find_sources()
    work = OUT / "luajit2"
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(source, work)
    script = OUT / "build.bat"
    # Some shells set NoDefaultCurrentDirectoryInExePath, which hides minilua/buildvm.
    script.write_text("@echo off\r\nset NoDefaultCurrentDirectoryInExePath=\r\n"
                      f'call "{vcvars()}" >nul\r\ncd /d "{work / "src"}"\r\ncall .\\msvcbuild.bat\r\n')
    subprocess.run(["cmd", "/c", str(script)], check=True)
    shutil.copy(work / "src/lua51.dll", OUT / "lua51_64.dll")
    print("Built", OUT / "lua51_64.dll", "from", source)


if __name__ == "__main__":
    main()
