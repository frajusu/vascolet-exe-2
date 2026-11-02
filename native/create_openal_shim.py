"""Deterministic PE32 OpenAL forwarder for the games' legacy device alias.

All exports forward to the unmodified soft_oal.dll. alcOpenDevice selects the
default device instead of a hard-coded physical device name. The generated
x86 stub is: mov dword ptr [esp+4],0; jmp dword ptr [import_address].
No game executable or implementation from OpenAL is modified.
"""
import struct


def align(value, size):
    return (value + size - 1) & -size


def exports(binary):
    pe = struct.unpack_from('<I', binary, 0x3C)[0]
    optional = pe + 24
    assert struct.unpack_from('<H', binary, optional)[0] == 0x10B, 'Requires PE32'
    count = struct.unpack_from('<H', binary, pe + 6)[0]
    size = struct.unpack_from('<H', binary, pe + 20)[0]
    sections = [struct.unpack_from('<8sIIII', binary, optional + size + n * 40) for n in range(count)]

    def offset(rva):
        for _, virtual_size, start, raw_size, raw in sections:
            if start <= rva < start + max(virtual_size, raw_size):
                return raw + rva - start
        raise ValueError(f'Unmapped RVA {rva:x}')

    def string(rva):
        start = offset(rva)
        return binary[start:binary.index(b'\0', start)].decode('ascii')

    rva = struct.unpack_from('<I', binary, optional + 96)[0]
    fields = struct.unpack_from('<IIHHIIIIIII', binary, offset(rva))
    base, function_count, name_count, functions, names, ordinals = fields[5:]
    result = []
    for index in range(name_count):
        name = string(struct.unpack_from('<I', binary, offset(names) + index * 4)[0])
        ordinal = struct.unpack_from('<H', binary, offset(ordinals) + index * 2)[0]
        result.append((name, ordinal))
    return base, function_count, result


def create_shim(original):
    base, function_count, names = exports(original)
    names.sort()
    data = bytearray(40 + function_count * 4 + len(names) * 6)
    eat, name_table, ordinal_table = 40, 40 + function_count * 4, 40 + function_count * 4 + len(names) * 4

    def add(value):
        where = len(data)
        data.extend(value)
        return 0x2000 + where

    dll_name = add(b'OpenAL32.dll\0')
    for index, (name, ordinal) in enumerate(names):
        name_rva = add(name.encode() + b'\0')
        address = 0x1000 if name == 'alcOpenDevice' else add(('soft_oal.' + name).encode() + b'\0')
        struct.pack_into('<I', data, eat + ordinal * 4, address)
        struct.pack_into('<I', data, name_table + index * 4, name_rva)
        struct.pack_into('<H', data, ordinal_table + index * 2, ordinal)
    export_size = len(data)
    struct.pack_into('<IIHHIIIIIII', data, 0, 0, 0, 0, 0, dll_name, base, function_count, len(names), 0x2000 + eat, 0x2000 + name_table, 0x2000 + ordinal_table)
    data.extend(b'\0' * (align(len(data), 4) - len(data)))
    imports = len(data)
    data.extend(b'\0' * 40)
    ilt = len(data)
    data.extend(b'\0' * 8)
    iat = len(data)
    data.extend(b'\0' * 8)
    hint_name = add(b'\0\0alcOpenDevice\0')
    real_name = add(b'soft_oal.dll\0')
    struct.pack_into('<IIIII', data, imports, 0x2000 + ilt, 0, 0, real_name, 0x2000 + iat)
    struct.pack_into('<I', data, ilt, hint_name)
    struct.pack_into('<I', data, iat, hint_name)
    image_base = 0x20000000
    code = b'\xc7\x44\x24\x04\0\0\0\0\xff\x25' + struct.pack('<I', image_base + 0x2000 + iat)
    reloc_rva = align(0x2000 + len(data), 4096)
    reloc = struct.pack('<IIHH', 0x1000, 12, 0x300A, 0)
    header = bytearray(512)
    header[:2] = b'MZ'
    struct.pack_into('<I', header, 0x3C, 0x80)
    header[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x84, 0x14C, 3, 0, 0, 0, 224, 0x2102)
    opt = 0x98
    struct.pack_into('<HBBIIIIII', header, opt, 0x10B, 0, 0, 512, align(len(data), 512) + 512, 0, 0, 0x1000, 0x2000)
    struct.pack_into('<III', header, opt + 28, image_base, 4096, 512)
    struct.pack_into('<HHHHHH', header, opt + 40, 4, 0, 0, 0, 4, 0)
    struct.pack_into('<IIIIHHIIIIII', header, opt + 52, 0, reloc_rva + 4096, 512, 0, 2, 0x140, 0x100000, 4096, 0x100000, 4096, 0, 16)
    struct.pack_into('<II', header, opt + 96, 0x2000, export_size)
    struct.pack_into('<II', header, opt + 104, 0x2000 + imports, 40)
    struct.pack_into('<II', header, opt + 96 + 5 * 8, reloc_rva, len(reloc))
    struct.pack_into('<II', header, opt + 96 + 12 * 8, 0x2000 + iat, 8)
    raw = 512
    payload = bytearray()
    for index, (name, rva, contents, flags) in enumerate([(b'.text', 0x1000, code, 0x60000020), (b'.data', 0x2000, data, 0xC0000040), (b'.reloc', reloc_rva, reloc, 0x42000040)]):
        size = align(len(contents), 512)
        struct.pack_into('<8sIIIIIIHHI', header, opt + 224 + index * 40, name, len(contents), rva, size, raw, 0, 0, 0, 0, flags)
        payload.extend(contents)
        payload.extend(b'\0' * (size - len(contents)))
        raw += size
    result = bytes(header + payload)
    assert exports(result) == (base, function_count, names)
    return result
