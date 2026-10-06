# SPDX-License-Identifier: Apache-2.0
"""Read Windows' GPU dedicated-memory counter for one process, using system PDH."""
import ctypes
from ctypes import wintypes


class _Value(ctypes.Structure):
    # PDH_FMT_COUNTERVALUE layout when requesting PDH_FMT_LARGE.
    _fields_ = [('status', wintypes.DWORD), ('large', ctypes.c_int64)]


class _Item(ctypes.Structure):
    _fields_ = [('name', wintypes.LPWSTR), ('value', _Value)]


class WindowsGpuMemory:
    def __init__(self):
        self.pdh = ctypes.WinDLL('pdh.dll')
        self.query, self.counter = wintypes.HANDLE(), wintypes.HANDLE()
        declarations = {
            'PdhOpenQueryW': [wintypes.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(wintypes.HANDLE)],
            'PdhAddEnglishCounterW': [wintypes.HANDLE, wintypes.LPCWSTR, ctypes.c_size_t,
                                     ctypes.POINTER(wintypes.HANDLE)],
            'PdhCollectQueryData': [wintypes.HANDLE],
            'PdhGetFormattedCounterArrayW': [wintypes.HANDLE, wintypes.DWORD,
                                             ctypes.POINTER(wintypes.DWORD),
                                             ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p],
            'PdhCloseQuery': [wintypes.HANDLE],
        }
        for name, arguments in declarations.items():
            function = getattr(self.pdh, name)
            function.argtypes, function.restype = arguments, wintypes.DWORD
        self.check(self.pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)))
        try:
            self.check(self.pdh.PdhAddEnglishCounterW(
                self.query, r'\GPU Process Memory(*)\Dedicated Usage', 0,
                ctypes.byref(self.counter)))
        except Exception:
            self.close()
            raise

    @staticmethod
    def check(status):
        if status:
            raise OSError('Windows GPU memory counter status: 0x%08x' % status)

    def sample(self, pid):
        status = self.pdh.PdhCollectQueryData(self.query)
        if status == 0x800007D5:  # PDH_NO_DATA
            return None
        self.check(status)
        size, count = wintypes.DWORD(), wintypes.DWORD()
        status = self.pdh.PdhGetFormattedCounterArrayW(
            self.counter, 0x400, ctypes.byref(size), ctypes.byref(count), None)
        if status == 0 and count.value == 0:
            return None
        if status != 0x800007D2:  # PDH_MORE_DATA
            self.check(status)
        buffer = ctypes.create_string_buffer(size.value)
        self.check(self.pdh.PdhGetFormattedCounterArrayW(
            self.counter, 0x400, ctypes.byref(size), ctypes.byref(count), buffer))
        items = (_Item * count.value).from_buffer(buffer)
        values = [item.value.large for item in items
                  if (item.name or '').startswith('pid_%d_' % pid) and item.value.status in (0, 1)]
        # No instance yet is unavailable, not measured zero usage.
        return sum(values) if values else None

    def close(self):
        if self.query.value:
            self.pdh.PdhCloseQuery(self.query)
            self.query = wintypes.HANDLE()
