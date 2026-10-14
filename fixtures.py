"""Generate original tiny test tunes, never download copyrighted soundtracks."""
from pathlib import Path
import gzip
import struct


def make_fixtures(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    make_midi_fixtures(directory)
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


def make_midi_fixtures(directory):
    """Original sine SoundFont and MIDI, including tempo, pan, sustain and drums."""
    import math
    directory = Path(directory)
    def chunk(tag, data):
        return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
    def name(s):
        return s.encode().ljust(20, b'\0')
    def gen(op, value):
        return struct.pack('<HH', op, value & 65535)
    info = chunk(b'LIST', b'INFO' + chunk(b'ifil', struct.pack('<HH', 2, 1))
                 + chunk(b'INAM', b'Original test sine\0\0'))
    samples = [int(12000 * math.sin(2 * math.pi * i / 100)) for i in range(100)] + [0] * 46
    sdta = chunk(b'LIST', b'sdta' + chunk(b'smpl', struct.pack('<146h', *samples)))
    phdr = b''.join(name(n) + struct.pack('<HHHIII', p, b, i, 0, 0, 0)
                    for i, (n, p, b) in enumerate([('Sine', 0, 0), ('Lead', 80, 0), ('Drums', 0, 128), ('EOP', 0, 0)]))
    pbag = b''.join(struct.pack('<HH', i, 0) for i in range(4))
    pgen = gen(41, 0) * 3 + gen(0, 0)
    inst = name('Sine oscillator') + struct.pack('<H', 0) + name('EOI') + struct.pack('<H', 1)
    igen = gen(38, -2400) + gen(54, 1) + gen(53, 0) + gen(0, 0)
    shdr = name('Sine 440 Hz') + struct.pack('<IIIIIBbHH', 0, 100, 0, 100, 44000, 69, 0, 0, 1)
    shdr += name('EOS') + bytes(26)
    pdta = chunk(b'LIST', b'pdta' + b''.join(chunk(t, b) for t, b in [
        (b'phdr', phdr), (b'pbag', pbag), (b'pmod', bytes(10)), (b'pgen', pgen),
        (b'inst', inst), (b'ibag', struct.pack('<HHHH', 0, 0, 3, 0)),
        (b'imod', bytes(10)), (b'igen', igen), (b'shdr', shdr)]))
    (directory / 'fixture-gm.sf2').write_bytes(chunk(b'RIFF', b'sfbk' + info + sdta + pdta))
    def vlq(n):
        out = [n & 127]
        while n >> 7:
            n >>= 7
            out.insert(0, (n & 127) | 128)
        return bytes(out)
    def track(events):
        data = b''; previous = 0
        for tick, msg in events:
            data += vlq(tick - previous) + msg; previous = tick
        return b'MTrk' + struct.pack('>I', len(data)) + data
    conductor = track([(0, b'\xff\x03\x0eMIDI test song'), (0, b'\xff\x51\x03\x07\xa1\x20'),
                       (480, b'\xff\x51\x03\x0f\x42\x40'), (960, b'\xff\x2f\0')])
    events = []
    for c in range(16):
        key = 69 + c % 8
        events += [(0, bytes([0xb0+c, 10, 0 if c == 0 else 127 if c == 1 else 64])),
                   (0, bytes([0xb0+c, 91, 70])), (0, bytes([0xb0+c, 93, 40])),
                   (0, bytes([0x90+c, key, 65])), (720, bytes([0x80+c, key, 0]))]
    events += [(120, b'\xb0\x40\x7f'), (240, b'\x80\x45\0'), (600, b'\xe1\0\x50'),
               (720, b'\xb0\x40\0'), (960, b'\xff\x2f\0')]
    events += [(0, bytes([0x90, key, 55])) for key in (72, 76)]
    events += [(720, bytes([0x80, key, 0])) for key in (72, 76)]
    events.sort(key=lambda e: e[0])
    midi = b'MThd' + struct.pack('>IHHH', 6, 1, 2, 480) + conductor + track(events)
    (directory / 'demo.mid').write_bytes(midi)


if __name__ == '__main__':
    for path in make_fixtures(Path(__file__).parent / 'test-output'):
        print(path)
