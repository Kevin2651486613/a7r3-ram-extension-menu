"""Shared ARM32 ELF wrapping helper (MIT)."""
import struct

DEFAULT_ENTRY = 0x10100


def wrap_arm_elf(code: bytes, entry: int = DEFAULT_ENTRY) -> bytes:
    """Wrap freestanding ARM machine code into a minimal static ELF32 LE executable."""
    header = bytearray(256)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        header,
        16,
        2,          # ET_EXEC
        40,         # EM_ARM
        1,          # EV_CURRENT
        entry,      # e_entry
        52,         # e_phoff
        0,          # e_shoff
        0x05000000, # EF_ARM_EABI_VER5
        52,         # e_ehsize
        32,         # e_phentsize
        1,          # e_phnum
        0,
        0,
        0,
    )
    struct.pack_into(
        "<8I",
        header,
        52,
        1,          # PT_LOAD
        256,        # p_offset
        entry,      # p_vaddr
        entry,      # p_paddr
        len(code),  # p_filesz
        len(code),  # p_memsz
        7,          # PF_R | PF_W | PF_X
        4096,       # p_align
    )
    return bytes(header) + code
