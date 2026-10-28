"""Minimal Skyrim Papyrus .pex reader/writer.

Only what Fire Pots needs: write a script with no code that just extends a parent
(our MCM script is `Scriptname FirePots_MCM extends MCM_ConfigBase`, empty body; MCM Helper
does all the work from config.json). The reader parses the header, string table and object
headers of any Skyrim .pex so the writer can be checked against real compiled scripts.

Skyrim .pex is big-endian. Layout (see UESP "Compiled Script File Format"):
  u32 magic 0xFA57C0DE, u8 major (3), u8 minor (2), u16 game id (1 = Skyrim), u64 compile time,
  wstr source, wstr user, wstr machine,
  u16 string count + wstr strings,
  u8 has debug info [+ debug block],
  u16 user flag count + (u16 name idx, u8 bit) entries,
  u16 object count + objects:
    u16 name idx, u32 size (counts itself), u16 parent idx, u16 doc idx, u32 user flags,
    u16 auto-state idx, u16 #variables, u16 #properties, u16 #states + states (u16 name, u16 #functions)
"""

from __future__ import annotations

import struct
import sys
import time
from dataclasses import dataclass, field

MAGIC = 0xFA57C0DE


class _W:
    def __init__(self) -> None:
        self.b = bytearray()

    def u8(self, v: int) -> None:
        self.b += struct.pack(">B", v)

    def u16(self, v: int) -> None:
        self.b += struct.pack(">H", v)

    def u32(self, v: int) -> None:
        self.b += struct.pack(">I", v)

    def u64(self, v: int) -> None:
        self.b += struct.pack(">Q", v)

    def wstr(self, s: str) -> None:
        data = s.encode("cp1252")
        self.u16(len(data))
        self.b += data


class _R:
    def __init__(self, data: bytes) -> None:
        self.d = data
        self.p = 0

    def _take(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.p + size > len(self.d):
            raise ValueError(f"truncated .pex at offset {self.p}")
        (v,) = struct.unpack_from(fmt, self.d, self.p)
        self.p += size
        return v

    def u8(self) -> int:
        return self._take(">B")

    def u16(self) -> int:
        return self._take(">H")

    def u32(self) -> int:
        return self._take(">I")

    def u64(self) -> int:
        return self._take(">Q")

    def wstr(self) -> str:
        n = self.u16()
        s = self.d[self.p : self.p + n]
        if len(s) != n:
            raise ValueError("truncated string")
        self.p += n
        return s.decode("cp1252")


@dataclass
class PexObject:
    name: str
    parent: str
    size: int = 0
    user_flags: int = 0
    auto_state: str = ""
    n_variables: int = 0
    n_properties: int = 0
    states: list[tuple[str, int]] = field(default_factory=list)  # (name, #functions)


@dataclass
class PexFile:
    major: int
    minor: int
    game_id: int
    compile_time: int
    source: str
    user: str
    machine: str
    strings: list[str]
    has_debug: bool
    user_flags: list[tuple[str, int]]
    objects: list[PexObject]


def write_empty_script(name: str, parent: str, source: str | None = None) -> bytes:
    """A script with no variables, properties or functions that extends `parent`."""
    strings: list[str] = []

    def idx(s: str) -> int:
        if s not in strings:
            strings.append(s)
        return strings.index(s)

    # Same ordering the Papyrus compiler uses: object name, parent, then the empty string.
    i_name, i_parent, i_empty = idx(name), idx(parent), idx("")
    i_hidden, i_cond = idx("hidden"), idx("conditional")

    obj = _W()
    obj.u16(i_parent)
    obj.u16(i_empty)  # doc string
    obj.u32(0)  # user flags
    obj.u16(i_empty)  # auto state name
    obj.u16(0)  # variables
    obj.u16(0)  # properties
    obj.u16(1)  # states: the default "" state, no functions
    obj.u16(i_empty)
    obj.u16(0)

    w = _W()
    w.u32(MAGIC)
    w.u8(3)
    w.u8(2)
    w.u16(1)  # Skyrim
    w.u64(int(time.time()))
    w.wstr(source or f"{name}.psc")
    w.wstr("FirePots")
    w.wstr("tools/pex.py")
    w.u16(len(strings))
    for s in strings:
        w.wstr(s)
    w.u8(0)  # no debug info
    w.u16(2)  # user flags, as the compiler always emits them
    w.u16(i_hidden)
    w.u8(0)
    w.u16(i_cond)
    w.u8(1)
    w.u16(1)  # objects
    w.u16(i_name)
    w.u32(len(obj.b) + 4)  # size includes the size field itself
    w.b += obj.b
    return bytes(w.b)


def _skip_debug(r: _R) -> None:
    r.u64()  # modification time
    for _ in range(r.u16()):  # function infos
        r.u16(); r.u16(); r.u16(); r.u8()
        for _ in range(r.u16()):
            r.u16()


def read(data: bytes) -> PexFile:
    r = _R(data)
    if r.u32() != MAGIC:
        raise ValueError("not a Skyrim .pex (bad magic / endianness)")
    major, minor, game_id = r.u8(), r.u8(), r.u16()
    compile_time = r.u64()
    source, user, machine = r.wstr(), r.wstr(), r.wstr()
    strings = [r.wstr() for _ in range(r.u16())]
    has_debug = r.u8() != 0
    if has_debug:
        _skip_debug(r)
    user_flags = []
    for _ in range(r.u16()):
        user_flags.append((strings[r.u16()], r.u8()))
    objects = []
    for _ in range(r.u16()):
        name = strings[r.u16()]
        start = r.p
        size = r.u32()
        parent = strings[r.u16()]
        r.u16()  # doc
        flags = r.u32()
        auto_state = strings[r.u16()]
        obj = PexObject(name=name, parent=parent, size=size, user_flags=flags, auto_state=auto_state)
        obj.n_variables = r.u16()
        # Variables/properties/functions are skipped by size: we only need headers here.
        r.p = start + size
        objects.append(obj)
    if r.p != len(data):
        raise ValueError(f"{len(data) - r.p} trailing bytes after objects")
    return PexFile(major, minor, game_id, compile_time, source, user, machine, strings, has_debug,
                   user_flags, objects)


def read_empty_script_states(data: bytes) -> PexObject:
    """Full parse of a script written by write_empty_script (checks every field)."""
    f = read(data)
    r = _R(data)
    # Re-walk to the object body (header was validated above).
    obj = f.objects[0]
    body_start = data.rfind(struct.pack(">I", obj.size)) + 4
    r.p = body_start
    r.u16(); r.u16(); r.u32(); r.u16()
    obj.n_variables, obj.n_properties = r.u16(), r.u16()
    for _ in range(r.u16()):
        name = f.strings[r.u16()]
        obj.states.append((name, r.u16()))
    if r.p != len(data):
        raise ValueError("object body size mismatch")
    return obj


if __name__ == "__main__":
    # Usage: pex.py write <Name> <Parent> <out.pex>   |   pex.py dump <file.pex>
    if sys.argv[1] == "write":
        _, _, name, parent, out = sys.argv
        data = write_empty_script(name, parent)
        read_empty_script_states(data)  # self-check before writing
        with open(out, "wb") as fh:
            fh.write(data)
        print(f"wrote {out} ({len(data)} bytes): {name} extends {parent}")
    elif sys.argv[1] == "dump":
        with open(sys.argv[2], "rb") as fh:
            f = read(fh.read())
        print(f"v{f.major}.{f.minor} game {f.game_id} source={f.source!r} debug={f.has_debug}")
        print(f"strings[{len(f.strings)}]: {f.strings[:8]}{' ...' if len(f.strings) > 8 else ''}")
        print(f"user flags: {f.user_flags}")
        for o in f.objects:
            print(f"object {o.name} extends {o.parent!r} size={o.size} flags={o.user_flags:#x} autoState={o.auto_state!r}")
