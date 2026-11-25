#!/usr/bin/env python3
"""MIT clean-room BusyBox shell helper over SonySenserDevice USB terminal."""
from __future__ import annotations

import re
import secrets
import time

_WRITE_CHUNK = 48


def _write_terminal_chunked(dev, payload: bytes) -> None:
    if len(payload) <= _WRITE_CHUNK:
        dev.writeTerminal(payload)
        return
    for off in range(0, len(payload), _WRITE_CHUNK):
        dev.writeTerminal(payload[off : off + _WRITE_CHUNK])
        if off + _WRITE_CHUNK < len(payload):
            time.sleep(0.002)


def shell_command(dev, cmd: str, timeout: float = 15) -> str:
    """Execute `cmd` on Sony Service Terminal and return captured stdout."""
    if not isinstance(cmd, str):
        raise TypeError("cmd must be str")
    if "\x00" in cmd:
        raise ValueError("cmd must not contain NUL bytes")
    cmd_clean = cmd.strip().rstrip(";")
    if not cmd_clean:
        return ""

    tok = secrets.token_hex(6)
    start_tag = f"__A7B_{tok}__"
    end_tag = f"__A7E_{tok}__"

    # Split tags with shell quotes so TTY command echo never matches the actual marker lines.
    b_cmd = f'echo "{start_tag[:6]}""{start_tag[6:]}"'
    e_cmd = f'echo "{end_tag[:6]}""{end_tag[6:]}"'
    sep = "\n" if "\n" in cmd_clean else "; "
    wire = f"{b_cmd}{sep}{cmd_clean}{sep}{e_cmd}\n"

    _write_terminal_chunked(dev, wire.encode("utf-8"))

    start_re = re.compile(rf"(?:^|\n){re.escape(start_tag)}[^\n]*\n")
    end_re = re.compile(rf"{re.escape(end_tag)}[^\n]*\n")

    deadline = time.monotonic() + max(0.2, float(timeout))
    buf = bytearray()
    while time.monotonic() < deadline:
        chunk = dev.readTerminal()
        if not chunk:
            time.sleep(0.01)
            continue
        buf.extend(chunk)
        text = buf.decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")
        m_start = start_re.search(text)
        if m_start is None:
            continue
        body_start = m_start.end()
        m_end = end_re.search(text, body_start)
        if m_end is not None:
            return text[body_start : m_end.start()]

    tail = buf[-160:].decode("utf-8", errors="replace")
    raise TimeoutError(f"Service terminal command timed out after {timeout}s (cmd={cmd_clean!r}, tail={tail!r})")
