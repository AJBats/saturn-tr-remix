"""The builder's pictures, drawn here (Pillow, three times over and scaled down for smooth edges): the banner's
dusk and pyramid, the disc slots' discs, and the program's icon. Nothing from the game."""
import base64
import io

from PIL import Image, ImageDraw, ImageFilter

SS = 3                  # drawn at three times the size
BG = (21, 18, 15)       # the window's ground (builder.py COLOURS["bg"])
GOLD = (217, 164, 65)


def _png(im):
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue()


def tk_data(im):
    """A picture as Tk's PhotoImage takes it (data=)."""
    return base64.b64encode(_png(im)).decode("ascii")


def _pyramid(d, cx, base, width, height, steps, fill, edge):
    step_h = height / steps
    for i in range(steps):
        w = width * (1 - i / steps)
        top = base - (i + 1) * step_h
        d.rectangle([cx - w / 2, top, cx + w / 2, top + step_h], fill=fill)
        d.line([cx - w / 2, top, cx + w / 2, top], fill=edge, width=SS)
    # the stair up its face
    sw = width * 0.09
    d.rectangle([cx - sw / 2, base - height, cx + sw / 2, base], fill=tuple(max(0, c - 6) for c in fill))
    for i in range(int(steps * 3)):
        y = base - (i + 1) * height / (steps * 3)
        d.line([cx - sw / 2, y, cx + sw / 2, y], fill=edge, width=1)


def banner(w, h):
    """The banner behind the title: a dusk sky, the sun low behind a stepped pyramid."""
    W, H = w * SS, h * SS
    top, bottom = (78, 52, 32), BG
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = (y / (H - 1)) ** 0.8
        d.line([0, y, W, y], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    # the sun's glow, blurred, then the sun
    glow = Image.new("L", (W, H), 0)
    gd = ImageDraw.Draw(glow)
    sx, sy, r = W * 0.91, H * 0.34, H * 0.36
    gd.ellipse([sx - r, sy - r, sx + r, sy + r], fill=190)
    glow = glow.filter(ImageFilter.GaussianBlur(H * 0.16))
    im = Image.composite(Image.new("RGB", (W, H), (243, 170, 88)), im, glow)
    d = ImageDraw.Draw(im)
    r = H * 0.11
    d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=(252, 212, 140))
    # a far pyramid, then the near one, then the ground
    _pyramid(d, W * 0.64, H * 0.94, W * 0.20, H * 0.42, 5, (52, 38, 28), (110, 80, 50))
    _pyramid(d, W * 0.80, H * 1.0, W * 0.40, H * 0.78, 7, (24, 19, 15), (122, 90, 52))
    d.rectangle([0, H * 0.96, W, H], fill=BG)
    return im.resize((w, h), Image.LANCZOS)


def disc(size, label, ring, lit=True):
    """A compact disc for a slot: its label's colour and the ring round it; faded when not chosen."""
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    o = SS
    d.ellipse([o, o, S - o, S - o], fill=(200, 194, 184), outline=(120, 114, 106), width=SS)
    # a faint sheen across the playing side
    for i, c in enumerate(((222, 216, 206), (186, 192, 204), (210, 196, 186))):
        a = S * (0.12 + 0.05 * i)
        d.arc([a, a, S - a, S - a], 200 + 25 * i, 250 + 25 * i, fill=c, width=S // 16)
    lr = S * 0.30
    c = S / 2
    d.ellipse([c - lr, c - lr, c + lr, c + lr], fill=label, outline=ring, width=SS * 2)
    hr = S * 0.075
    d.ellipse([c - hr, c - hr, c + hr, c + hr], fill=BG + (255,))
    im = im.resize((size, size), Image.LANCZOS)
    if not lit:
        alpha = im.getchannel("A").point(lambda v: v * 2 // 5)
        im.putalpha(alpha)
    return im


def icon(size):
    """The program's icon: the pyramid in gold on the window's ground."""
    S = size * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=S * 0.18, fill=BG + (255,))
    steps = 4
    base, height, width = S * 0.80, S * 0.58, S * 0.76
    for i in range(steps):
        w = width * (1 - i / steps)
        t = base - (i + 1) * height / steps
        d.rectangle([S / 2 - w / 2, t, S / 2 + w / 2, t + height / steps - S * 0.025], fill=GOLD + (255,))
    return im.resize((size, size), Image.LANCZOS)


def save_icon(path):
    icon(256).save(path, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
