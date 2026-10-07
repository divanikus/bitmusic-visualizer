"""Generate original tiny test tunes, never download copyrighted soundtracks."""
from pathlib import Path
import gzip
import struct


def make_fixtures(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    # A small NES initialization routine enables pulse 1, triangle and noise.
    # The play routine returns: the hardware oscillators continue sounding.
    code = bytearray()
    def write(address, value):
        code.extend((0xA9, value, 0x8D, address & 255, address >> 8))
    write(0x4015, 0x0D)
    for address, value in [(0x4000, 0xBF), (0x4002, 0xFD), (0x4003, 0xF8),
                           (0x4008, 0xFF), (0x400A, 0xFE), (0x400B, 0xF8),
                           (0x400C, 0x38), (0x400E, 0x06), (0x400F, 0xF8)]:
        write(address, value)
    code.append(0x60)
    header = bytearray(128)
    header[:5] = b'NESM\x1a'
    header[5:8] = bytes([1, 2, 1])
    struct.pack_into('<HHH', header, 8, 0x8000, 0x8000, 0x8000 + len(code) - 1)
    header[14:14 + 16] = b'Prototype tones\0'
    struct.pack_into('<H', header, 0x6E, 16639)
    (directory / 'demo.nsf').write_bytes(header + code)

    # Named NSFE subsongs with real per-track time metadata for playlist/end tests.
    def chunk(tag, data):
        return struct.pack('<I', len(data)) + tag + data
    nsfe = b'NSFE' + chunk(b'INFO', struct.pack('<HHHBBBB', 0x8000, 0x8000, 0x8000 + len(code) - 1, 0, 0, 3, 0))
    nsfe += chunk(b'tlbl', b'Pulse Theme\0Triangle Interlude\0Noise Finale\0')
    nsfe += chunk(b'time', struct.pack('<iii', 4000, 3000, 2000))
    nsfe += chunk(b'DATA', code) + chunk(b'NEND', b'')
    (directory / 'named.nsfe').write_bytes(nsfe)

    # Mega Drive VGM: YM2612 FM channel 1 plus PSG square 1.
    commands = bytearray()
    def fm(reg, value):
        commands.extend((0x52, reg, value))
    fm(0x22, 0)
    fm(0x27, 0)
    fm(0x2B, 0)
    for slot in (0, 4, 8, 12):
        for base, value in [(0x30, 1), (0x40, 0x18), (0x50, 0x1F),
                            (0x60, 0), (0x70, 0), (0x80, 0x0F), (0x90, 0)]:
            fm(base + slot, value)
    fm(0xB0, 7)
    fm(0xB4, 0xC0)
    fm(0xA4, 0x22)
    fm(0xA0, 0x69)
    fm(0x28, 0xF0)
    commands.extend((0x50, 0x8D, 0x50, 0x0F, 0x50, 0x94))
    loop = len(commands)
    for _ in range(6):
        commands.extend((0x61, 0x44, 0xAC))
    commands.append(0x66)
    header = bytearray(0x40)
    header[:4] = b'Vgm '
    struct.pack_into('<I', header, 4, len(header) + len(commands) - 4)
    struct.pack_into('<I', header, 8, 0x150)
    struct.pack_into('<I', header, 0x0C, 3579545)
    struct.pack_into('<I', header, 0x18, 6 * 44100)
    struct.pack_into('<I', header, 0x1C, 0x40 + loop - 0x1C)
    struct.pack_into('<I', header, 0x20, 6 * 44100)
    struct.pack_into('<I', header, 0x2C, 7670454)
    struct.pack_into('<I', header, 0x34, 0x0C)
    (directory / 'demo.vgm').write_bytes(header + commands)
    (directory / 'demo.vgz').write_bytes(gzip.compress(header + commands, mtime=0))

    # A separate two-second VGM: one-second intro and one-second looping section.
    short_commands = commands[:loop] + bytes((0x61, 0x44, 0xAC)) * 2 + b'\x66'
    short_header = bytearray(header)
    struct.pack_into('<I', short_header, 4, len(short_header) + len(short_commands) - 4)
    struct.pack_into('<I', short_header, 0x18, 2 * 44100)
    struct.pack_into('<I', short_header, 0x1C, 0x40 + loop + 3 - 0x1C)
    struct.pack_into('<I', short_header, 0x20, 44100)
    (directory / 'intro-loop.vgm').write_bytes(short_header + short_commands)

    # SPC700 sets KON for one voice and spins. DSP plays our looping BRR waveform.
    spc = bytearray(0x10200)
    signature = b'SNES-SPC700 Sound File Data v0.30'
    spc[:len(signature)] = signature
    spc[0x21:0x25] = bytes((0x1A, 0x1A, 0x1B, 30))
    struct.pack_into('<H', spc, 0x25, 0x0200)
    spc[0x2B] = 0xEF
    ram = memoryview(spc)[0x100:0x10100]
    ram[0x200:0x208] = bytes((0x8F, 0x4C, 0xF2, 0x8F, 1, 0xF3, 0x2F, 0xFE))
    ram[0xF1] = 0
    ram[0x1000:0x1009] = bytes((0xB3, 0x01, 0x23, 0x45, 0x67, 0xFE, 0xDC, 0xBA, 0x98))
    struct.pack_into('<HH', ram, 0x2000, 0x1000, 0x1000)
    dsp = memoryview(spc)[0x10100:0x10180]
    for address, value in {0: 100, 1: 100, 2: 0, 3: 0x10, 4: 0, 5: 0,
                           7: 0x7F, 0x0C: 100, 0x1C: 100, 0x5D: 0x20, 0x6C: 0x20}.items():
        dsp[address] = value
    (directory / 'demo.spc').write_bytes(spc)
    return [directory / ('demo.' + extension) for extension in ('nsf', 'vgm', 'spc')]


if __name__ == '__main__':
    for path in make_fixtures(Path(__file__).parent / 'test-output'):
        print(path)
