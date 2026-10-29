"""Build the 5.0.2 in-place Windows launcher update; contains no pairing keys."""

from pathlib import Path
import argparse
import hashlib
import shutil
import zipfile

SOURCE = Path(__file__).resolve().parents[2]
DIST = SOURCE / 'tools/distribution'


def build(destination: Path):
    release = (SOURCE / 'VERSION.txt').read_text().strip()
    if release != '5.0.2':
        raise ValueError(
            'This updater is specific to 5.0.2; review its compatibility before advancing it.'
        )
    root = destination / f'X4-LAN-{release}-Windows-Update'
    root.mkdir(parents=True, exist_ok=False)
    for source, target in [
        ('windows_setup.ps1', 'setup-5.0.2.ps1'),
        ('update_windows_launcher.ps1', 'update-windows.ps1'),
        ('WINDOWS-UPDATE.txt', 'UPDATE-README.txt'),
    ]:
        shutil.copy2(DIST / source, root / target)
    (root / 'UPDATE-WINDOWS.cmd').write_bytes(
        b'@echo off\r\nsetlocal\r\n'
        b'powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-windows.ps1"\r\n'
        b'set "X4LAN_RESULT=%ERRORLEVEL%"\r\necho.\r\necho Press any key to close...\r\n'
        b'pause >nul\r\nexit /b %X4LAN_RESULT%\r\n'
    )
    # Unique filename so copying the update cannot replace the package manifest.
    (root / 'UPDATE-SHA256.txt').write_text(
        ''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n'
            for p in sorted(root.iterdir())
            if p.is_file()
        ),
        encoding='utf-8',
    )
    archive = destination / (root.name + '.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(root.iterdir()):
            z.write(p, p.name)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print('Windows launcher update created:', archive)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    build(parser.parse_args().destination)
