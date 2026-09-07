"""Generate installer/assets/Glass76.ico.

The icon is not hand-drawn art -- it is generated so it stays in step with the
plug-in's palette (see source/ui/theme.h). Run it after changing colours here:

    python installer/assets/make_icon.py

Requires Pillow. The numerals are set in Inter Bold, the same face the plug-in
ships in its bundle.
"""

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FONT = os.path.join(ROOT, "resource", "Fonts", "InterDisplay-Bold.ttf")
OUT = os.path.join(HERE, "Glass76.ico")

# macOS icons sit on a squircle whose corner radius is 22.5% of the side, and
# the artwork is inset ~10% from the canvas so it lines up with system icons.
S = 1024
RADIUS = int(S * 0.225)
INSET = int(S * 0.09)

BODY_TOP = (46, 51, 60)      # #2E333C
BODY_BOT = (18, 20, 24)      # #121418
ACCENT = (0, 136, 255)       # theme.h accent, light appearance


def rounded_mask(size, radius, supersample=4):
    """Anti-aliased rounded-rect mask, drawn large and scaled down."""
    big = Image.new("L", (size * supersample, size * supersample), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        (0, 0, size * supersample - 1, size * supersample - 1),
        radius=radius * supersample, fill=255)
    return big.resize((size, size), Image.LANCZOS)


def vertical_gradient(size, top, bottom):
    grad = Image.new("RGB", (1, size))
    for y in range(size):
        t = y / (size - 1)
        grad.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return grad.resize((size, size), Image.BICUBIC)


def build():
    side = S - 2 * INSET
    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Accent bloom behind the body, so the glass has something to pick up.
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse(
        (INSET - side * 0.10, INSET + side * 0.30,
         INSET + side * 1.10, INSET + side * 1.35),
        fill=ACCENT + (150,))
    icon.alpha_composite(glow.filter(ImageFilter.GaussianBlur(side * 0.16)))

    body = vertical_gradient(side, BODY_TOP, BODY_BOT).convert("RGBA")

    # The accent wash bleeding up from the bottom edge of the body itself.
    wash = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    ImageDraw.Draw(wash).ellipse(
        (-side * 0.25, side * 0.55, side * 1.25, side * 1.7), fill=ACCENT + (90,))
    body.alpha_composite(wash.filter(ImageFilter.GaussianBlur(side * 0.12)))

    # Liquid Glass edge stack, cut down to what survives at 32px: a containment
    # ring around the whole shape, plus a specular hairline that follows the
    # same rounded-rect path but fades out by the time it reaches the waist.
    hairline = max(2, side // 180)
    ImageDraw.Draw(body).rounded_rectangle(
        (0, 0, side - 1, side - 1), radius=RADIUS,
        outline=(255, 255, 255, 40), width=hairline * 2)

    spec = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    ImageDraw.Draw(spec).rounded_rectangle(
        (hairline, hairline, side - 1 - hairline, side - 1 - hairline),
        radius=RADIUS - hairline, outline=(255, 255, 255, 190), width=hairline)
    falloff = Image.new("L", (1, side))
    for y in range(side):
        t = min(1.0, y / (side * 0.42))
        falloff.putpixel((0, y), int(255 * (1.0 - t) ** 2))
    spec.putalpha(Image.composite(spec.getchannel("A"),
                                  Image.new("L", (side, side), 0),
                                  falloff.resize((side, side))))
    body.alpha_composite(spec)

    font_size = int(side * 0.44)
    font = ImageFont.truetype(FONT, font_size) if os.path.exists(FONT) \
        else ImageFont.load_default(font_size)
    label = "76"
    d = ImageDraw.Draw(body)
    box = d.textbbox((0, 0), label, font=font)
    x = (side - (box[2] - box[0])) // 2 - box[0]
    y = (side - (box[3] - box[1])) // 2 - box[1]
    d.text((x, y + side * 0.02), label, font=font, fill=(0, 0, 0, 110))  # drop shadow
    d.text((x, y), label, font=font, fill=(255, 255, 255, 245))

    body.putalpha(rounded_mask(side, RADIUS))
    icon.alpha_composite(body, (INSET, INSET))

    sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (48, 48),
             (64, 64), (128, 128), (256, 256)]
    icon.save(OUT, format="ICO", sizes=sizes)
    print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))


if __name__ == "__main__":
    build()
