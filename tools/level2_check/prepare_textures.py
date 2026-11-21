# Конвертирует текстуры игры в сырой RGBA для офлайн-проверки (заглушка raylib читает их вместо LoadImage).
import os, struct, sys
from PIL import Image
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
tex = os.path.join(os.path.dirname(__file__), '..', '..', 'app', 'src', 'main', 'assets', 'level2', 'textures')
for n in ['Concrete046_Color.jpg', 'Concrete048_Color.jpg', 'Metal024_Color.jpg']:
    im = Image.open(os.path.join(tex, n)).convert('RGBA')
    if im.width > 1024: im = im.resize((1024, 1024))
    with open(os.path.join(out, n + '.rgba'), 'wb') as f: f.write(struct.pack('ii', im.width, im.height)); f.write(im.tobytes())
