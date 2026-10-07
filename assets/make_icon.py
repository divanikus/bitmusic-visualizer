"""Original vector player mark and PNG/Windows ICO exports. Dev-only: Pillow."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent
root.joinpath('player.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <rect x="2" y="2" width="60" height="60" rx="15" fill="#a2e5d5"/>
  <path d="M24 17 L46 32 L24 47 Z" fill="#133c38"/>
</svg>
''', encoding='utf-8')
image = Image.new('RGBA', (1024, 1024))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((32, 32, 992, 992), radius=240, fill='#a2e5d5')
draw.polygon([(384, 272), (736, 512), (384, 752)], fill='#133c38')
image = image.resize((256, 256), Image.Resampling.LANCZOS)
image.save(root / 'player.png')
image.save(root / 'player.ico', sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
# A PNG-backed 256px macOS icon, using the same original artwork.
png = (root / 'player.png').read_bytes()
(root / 'player.icns').write_bytes(b'icns' + struct.pack('>I', 16 + len(png)) +
                                 b'ic08' + struct.pack('>I', 8 + len(png)) + png)
