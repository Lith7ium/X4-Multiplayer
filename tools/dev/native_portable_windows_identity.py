"""Read-only identity evidence for an already owned Windows native participant.

No process is opened by PID, launched, stopped or written. Exact artifact files
are held without write/delete sharing from before launch until owned exit. The
runner supplies its existing process and kill-on-close job handles.
"""
import ctypes
from ctypes import wintypes as wt
import hashlib
from pathlib import Path, PureWindowsPath
import struct
import sys


class FileInfo(ctypes.Structure):
    _fields_ = [('attributes', wt.DWORD), ('created', wt.FILETIME), ('accessed', wt.FILETIME),
                ('written', wt.FILETIME), ('volume', wt.DWORD), ('size_hi', wt.DWORD),
                ('size_lo', wt.DWORD), ('links', wt.DWORD), ('index_hi', wt.DWORD), ('index_lo', wt.DWORD)]


class ModuleInfo(ctypes.Structure):
    _fields_ = [('base', ctypes.c_void_p), ('size', wt.DWORD), ('entry', ctypes.c_void_p)]


def stamp(value): return (value.dwHighDateTime << 32) | value.dwLowDateTime


def canonical(path):
    value = str(path)
    if value.startswith('\\\\?\\'): value = value[4:]
    return PureWindowsPath(value)


class WindowsAPI:
    def __init__(self):
        if sys.platform != 'win32': raise OSError('Windows native identity requires Windows')
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.psapi = ctypes.WinDLL('psapi', use_last_error=True)
        definitions = {
            'CreateFileW': ([wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE], wt.HANDLE),
            'CloseHandle': ([wt.HANDLE], wt.BOOL),
            'ReadFile': ([wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p], wt.BOOL),
            'GetFileInformationByHandle': ([wt.HANDLE, ctypes.POINTER(FileInfo)], wt.BOOL),
            'GetFinalPathNameByHandleW': ([wt.HANDLE, wt.LPWSTR, wt.DWORD, wt.DWORD], wt.DWORD),
            'GetCurrentProcess': ([], wt.HANDLE),
            'DuplicateHandle': ([wt.HANDLE, wt.HANDLE, wt.HANDLE, ctypes.POINTER(wt.HANDLE), wt.DWORD, wt.BOOL, wt.DWORD], wt.BOOL),
            'GetProcessId': ([wt.HANDLE], wt.DWORD),
            'GetProcessTimes': ([wt.HANDLE, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)], wt.BOOL),
            'GetSystemTimeAsFileTime': ([ctypes.POINTER(wt.FILETIME)], None),
            'QueryFullProcessImageNameW': ([wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)], wt.BOOL),
            'IsProcessInJob': ([wt.HANDLE, wt.HANDLE, ctypes.POINTER(wt.BOOL)], wt.BOOL),
            'WaitForSingleObject': ([wt.HANDLE, wt.DWORD], wt.DWORD),
        }
        for name, (arguments, returns) in definitions.items():
            function = getattr(self.kernel, name); function.argtypes = arguments; function.restype = returns
        for name, arguments, returns in (
            ('EnumProcessModulesEx', [wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD, ctypes.POINTER(wt.DWORD), wt.DWORD], wt.BOOL),
            ('GetModuleFileNameExW', [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD], wt.DWORD),
            ('GetModuleInformation', [wt.HANDLE, wt.HMODULE, ctypes.POINTER(ModuleInfo), wt.DWORD], wt.BOOL)):
            function = getattr(self.psapi, name); function.argtypes = arguments; function.restype = returns

    @staticmethod
    def require(result, label):
        if not result: raise OSError(ctypes.get_last_error(), label)
        return result


class ArtifactLease:
    def __init__(self, path, expected, api):
        self.api, self.handle = api, None
        self.path = Path(path).resolve(strict=True)
        try:
            # GENERIC_READ, FILE_SHARE_READ only, OPEN_EXISTING. No file write.
            handle = api.kernel.CreateFileW(str(self.path), 0x80000000, 1, None, 3, 0x80, None)
            if handle == ctypes.c_void_p(-1).value: raise OSError(ctypes.get_last_error(), 'artifact read lease refused')
            self.handle = handle
            before = self.identity()
            buffer = ctypes.create_string_buffer(1 << 20); count = wt.DWORD(); digest = hashlib.sha256(); header = b''
            total = 0
            while True:
                api.require(api.kernel.ReadFile(handle, buffer, len(buffer), ctypes.byref(count), None), 'artifact read failed')
                if not count.value: break
                data = buffer.raw[:count.value]; digest.update(data); total += len(data)
                if len(header) < 65536: header += data[:65536 - len(header)]
            self.sha256 = digest.hexdigest()
            if self.sha256 != expected or before != self.identity() or total != before['bytes']:
                raise ValueError('leased artifact digest or identity differs')
            if header[:2] != b'MZ' or len(header) < 64: raise ValueError('artifact is not PE')
            offset = struct.unpack_from('<I', header, 60)[0]
            if (offset > len(header) - 84 or header[offset:offset + 4] != b'PE\0\0'
                    or struct.unpack_from('<H', header, offset + 4)[0] != 0x8664
                    or struct.unpack_from('<H', header, offset + 24)[0] != 0x20b):
                raise ValueError('artifact is not bounded x64 PE')
            self.image_size = struct.unpack_from('<I', header, offset + 80)[0]
            if not self.image_size: raise ValueError('artifact has empty image')
            self.evidence = {**before, 'path': str(self.path), 'sha256': self.sha256,
                             'image_size': self.image_size, 'write_delete_sharing': False}
        except Exception:
            self.close(); raise

    def identity(self):
        info = FileInfo(); self.api.require(self.api.kernel.GetFileInformationByHandle(self.handle, ctypes.byref(info)), 'artifact identity failed')
        name = ctypes.create_unicode_buffer(32768)
        length = self.api.kernel.GetFinalPathNameByHandleW(self.handle, name, len(name), 0)
        if not 0 < length < len(name) or canonical(name.value) != canonical(self.path):
            raise ValueError('artifact handle path differs')
        return {'volume': info.volume, 'file_index': (info.index_hi << 32) | info.index_lo,
                'bytes': (info.size_hi << 32) | info.size_lo, 'written': stamp(info.written)}

    def close(self):
        if self.handle is not None:
            self.api.kernel.CloseHandle(self.handle); self.handle = None


class NativeIdentity:
    def __init__(self, executable, module, executable_sha256, module_sha256):
        self.api = WindowsAPI(); self.executable = self.module = None
        self.handle = None; self.job = None; self.pid = self.created = None
        self.evidence = {'module_observed': False, 'owned_process_exited': False}
        try:
            self.executable = ArtifactLease(executable, executable_sha256, self.api)
            self.module = ArtifactLease(module, module_sha256, self.api)
            opened = wt.FILETIME(); self.api.kernel.GetSystemTimeAsFileTime(ctypes.byref(opened))
            self.evidence['leases_ready_filetime'] = stamp(opened)
        except Exception:
            self.close(); raise

    def attach(self, process_handle, pid, job_handle):
        if self.handle is not None or not job_handle or type(pid) is not int or pid <= 0:
            raise ValueError('fresh owned process and job handles required')
        duplicate = wt.HANDLE(); current = self.api.kernel.GetCurrentProcess()
        self.api.require(self.api.kernel.DuplicateHandle(current, process_handle, current, ctypes.byref(duplicate), 0, False, 2), 'owned process handle duplication failed')
        self.handle, self.job, self.pid = duplicate.value, job_handle, pid
        self.created = self.verify()
        self.evidence.update(pid=pid, created=self.created, executable=self.executable.evidence,
                             module=self.module.evidence, owned_job_verified=True)
        return dict(self.evidence)

    def verify(self):
        if not self.handle or self.api.kernel.WaitForSingleObject(self.handle, 0) != 258:
            raise ValueError('owned process is not running')
        if self.api.kernel.GetProcessId(self.handle) != self.pid:
            raise ValueError('owned PID differs')
        values = [wt.FILETIME() for _ in range(4)]
        self.api.require(self.api.kernel.GetProcessTimes(self.handle, *(ctypes.byref(value) for value in values)), 'owned process times unavailable')
        created = stamp(values[0])
        if not created or (self.created is not None and created != self.created):
            raise ValueError('owned process creation identity differs')
        if created < self.evidence['leases_ready_filetime']:
            raise ValueError('owned process predates artifact leases')
        name = ctypes.create_unicode_buffer(32768); length = wt.DWORD(len(name))
        self.api.require(self.api.kernel.QueryFullProcessImageNameW(self.handle, 0, name, ctypes.byref(length)), 'owned process image unavailable')
        if canonical(name.value) != canonical(self.executable.path):
            raise ValueError('owned executable path differs')
        member = wt.BOOL()
        self.api.require(self.api.kernel.IsProcessInJob(self.handle, self.job, ctypes.byref(member)), 'owned job query failed')
        if not member.value: raise ValueError('native process outside owning job')
        for lease in (self.executable, self.module):
            if lease.identity() != {k: lease.evidence[k] for k in ('volume', 'file_index', 'bytes', 'written')}:
                raise ValueError('leased artifact identity changed')
        return created

    def observe(self):
        self.verify()
        modules = (wt.HMODULE * 4096)(); needed = wt.DWORD()
        self.api.require(self.api.psapi.EnumProcessModulesEx(self.handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 3), 'owned module enumeration failed')
        width = ctypes.sizeof(wt.HMODULE)
        if not needed.value or needed.value > ctypes.sizeof(modules) or needed.value % width:
            raise ValueError('owned module list is empty or unbounded')
        matches = []
        for module in modules[:needed.value // width]:
            name = ctypes.create_unicode_buffer(32768)
            length = self.api.psapi.GetModuleFileNameExW(self.handle, module, name, len(name))
            if not 0 < length < len(name): raise ValueError('owned module path unavailable')
            path = canonical(name.value)
            if path.name.lower() == self.module.path.name.lower():
                if path != canonical(self.module.path): raise ValueError('same-name module loaded from foreign path')
                info = ModuleInfo()
                self.api.require(self.api.psapi.GetModuleInformation(self.handle, module, ctypes.byref(info), ctypes.sizeof(info)), 'owned module information unavailable')
                if not info.base or info.size != self.module.image_size:
                    raise ValueError('owned module mapped image size differs')
                matches.append({'path': str(path), 'base': info.base, 'image_size': info.size})
        self.verify()
        if len(matches) > 1 or (self.evidence['module_observed'] and not matches):
            raise ValueError('owned module disappeared or became ambiguous')
        if matches:
            if 'mapped_module' in self.evidence and self.evidence['mapped_module'] != matches[0]:
                raise ValueError('owned mapped module identity changed')
            self.evidence.update(module_observed=True, mapped_module=matches[0])
        return dict(self.evidence)

    def finished(self):
        if not self.handle or self.api.kernel.WaitForSingleObject(self.handle, 0) != 0:
            raise ValueError('owned process has not exited')
        self.evidence['owned_process_exited'] = True
        return dict(self.evidence)

    def observe_udp_sources(self, endpoint_reader):
        """Join released read-only UDP ownership rows to this retained handle.

        Windows' owner-PID UDP table exposes a local address, not a socket inode
        or connected destination. Keep that limit explicit; the bridge supplies
        the received source and the independent server transport audit remains.
        """
        def scan():
            self.verify()
            rows = endpoint_reader({self.pid})
            if not isinstance(rows, list) or len(rows) > 4096:
                raise ValueError('owned endpoint table is unbounded')
            result = []
            for row in rows:
                if row.get('proto') != 'udp' or row.get('local') not in ('127.0.0.1', '0.0.0.0'): continue
                if row.get('pid') != self.pid or type(row.get('lport')) is not int or not 1024 <= row['lport'] <= 65535:
                    raise ValueError('owned loopback UDP attribution differs')
                result.append((row['local'], row['lport']))
            self.verify()
            if len(result) > 64 or len(set(result)) != len(result):
                raise ValueError('owned loopback UDP sources ambiguous or unbounded')
            return sorted(result)
        before = scan()
        if scan() != before: raise ValueError('owned UDP table changed during observation')
        observed = wt.FILETIME(); self.api.kernel.GetSystemTimeAsFileTime(ctypes.byref(observed))
        when = stamp(observed)
        evidence = self.evidence.setdefault('udp_source_ownership', {'scope': 'Windows owner-PID bound endpoint; wildcard binding does not identify remote destination or socket inode',
            'pid': self.pid, 'created': self.created, 'sources': []})
        existing = {tuple(row['address']): row for row in evidence['sources']}
        for address in before:
            row = existing.get(address)
            if row is None:
                if len(evidence['sources']) >= 64: raise ValueError('owned UDP source history is unbounded')
                row = {'address': list(address), 'first_filetime': when, 'last_filetime': when, 'observations': 0}
                evidence['sources'].append(row)
            row['last_filetime'] = when; row['observations'] += 1
        return dict(self.evidence)

    def close(self):
        if self.handle is not None:
            self.api.kernel.CloseHandle(self.handle); self.handle = None
        for lease in (self.module, self.executable):
            if lease is not None: lease.close()
