"""Shared argv helpers for ngfx command construction."""
from __future__ import annotations

import ctypes
import shlex
import subprocess
import sys
from typing import Iterable, Optional, Sequence


def split_program_args(arg_string: Optional[str]) -> Optional[list[str]]:
    """Parse the wrapper's raw --args string into target-program argv tokens."""
    if not arg_string:
        return None
    text = arg_string.strip()
    if not text:
        return None
    if sys.platform == "win32":
        return _windows_command_line_to_argv(text)
    return shlex.split(text)


def _windows_command_line_to_argv(cmdline: str) -> list[str]:
    argc = ctypes.c_int()
    command_line_to_argv = ctypes.windll.shell32.CommandLineToArgvW
    command_line_to_argv.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    command_line_to_argv.restype = ctypes.POINTER(ctypes.c_wchar_p)

    local_free = ctypes.windll.kernel32.LocalFree
    local_free.argtypes = [ctypes.c_void_p]
    local_free.restype = ctypes.c_void_p

    argv = command_line_to_argv(cmdline, ctypes.byref(argc))
    if not argv:
        raise ValueError(f"failed to parse --args command line: {cmdline!r}")
    try:
        return [argv[i] for i in range(argc.value)]
    finally:
        local_free(argv)


def format_env(envs: Optional[Sequence[str]]) -> Optional[str]:
    """Format `KEY=VALUE` entries as the single string ngfx.exe expects.

    ngfx expects `--env "K=V; K2=V2;"` — a single argument with each entry
    separated by `; ` and terminated by `;`. We accept a list of entries
    (each `KEY=VALUE`) and join.
    """
    if not envs:
        return None
    cleaned = [item.strip() for item in envs if item and item.strip()]
    if not cleaned:
        return None
    joined = "; ".join(cleaned)
    if not joined.endswith(";"):
        joined += ";"
    return joined


def format_args(argv: Optional[Sequence[str]]) -> Optional[str]:
    """Format target program args as a single string ngfx forwards verbatim."""
    if not argv:
        return None
    cleaned = [item for item in argv if item]
    if not cleaned:
        return None
    return subprocess.list2cmdline(cleaned)


def append_optional(argv: list, flag: str, value: Optional[object]) -> None:
    """Append `[flag, str(value)]` only when `value is not None`."""
    if value is None:
        return
    argv.extend([flag, str(value)])


def append_flag(argv: list, flag: str, enabled: bool) -> None:
    """Append `flag` (no value) only when `enabled` is truthy."""
    if enabled:
        argv.append(flag)


def extend_envs(argv: list, envs: Optional[Sequence[str]]) -> None:
    formatted = format_env(envs)
    if formatted:
        argv.extend(["--env", formatted])


def extend_program_args(argv: list, program_args: Optional[Sequence[str]]) -> None:
    formatted = format_args(program_args)
    if formatted:
        argv.extend(["--args", formatted])


def join_iter(*chunks: Iterable[str]) -> list[str]:
    """Concatenate any iterable of str chunks into a single argv list."""
    out: list[str] = []
    for chunk in chunks:
        out.extend(chunk)
    return out
