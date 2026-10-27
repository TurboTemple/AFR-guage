#!/usr/bin/env python3
"""Generate the gauge's LVGL 9 fonts (4 bpp, lv_font_fmt_txt).

The spec asks for a numerals-only large font made with the LVGL font
converter. lv_font_conv needs Node; this script writes the same format with
Pillow, and makes the digits tabular (one advance width) so the main number
does not shift sideways as it changes.

    pip install pillow
    python3 tools/make_fonts.py --inter-dir /usr/share/fonts/opentype/inter \
        --chakra-dir /path/to/chakrapetch

The main screen uses Chakra Petch (Google Fonts, github.com/google/fonts
ofl/chakrapetch: ChakraPetch-Medium/SemiBold/Bold.ttf); menus use Inter
(https://rsms.me/inter/). Both are under the SIL Open Font License 1.1.
Chakra Petch has no lambda, so that glyph comes from Inter at the same size.
Output goes to components/ui/fonts/.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

LABEL_CHARS = [chr(c) for c in range(0x20, 0x7F)] + ["°", "·", "λ"]
NUM_CHARS = list("-.0123456789")
FALLBACK = {"λ": ("inter", "Inter-Medium.otf")}

# name, (family, file), pixel size, characters, tabular digits
FONTS = [
    ("afr_font_num", ("chakra", "ChakraPetch-SemiBold.ttf"), 150, NUM_CHARS, True),
    ("afr_font_c40", ("chakra", "ChakraPetch-SemiBold.ttf"), 40, LABEL_CHARS, False),
    ("afr_font_c26", ("chakra", "ChakraPetch-SemiBold.ttf"), 26, LABEL_CHARS, True),
    ("afr_font_c24b", ("chakra", "ChakraPetch-Bold.ttf"), 24, LABEL_CHARS, False),
    ("afr_font_c22", ("chakra", "ChakraPetch-SemiBold.ttf"), 22, LABEL_CHARS, True),
    ("afr_font_c20", ("chakra", "ChakraPetch-Medium.ttf"), 20, LABEL_CHARS, False),
    ("afr_font_c18b", ("chakra", "ChakraPetch-Bold.ttf"), 18, LABEL_CHARS, False),
    ("afr_font_12", ("inter", "Inter-Medium.otf"), 12, LABEL_CHARS, False),
    ("afr_font_20", ("inter", "Inter-Medium.otf"), 20, LABEL_CHARS, False),
    ("afr_font_22", ("inter", "Inter-Medium.otf"), 22, LABEL_CHARS, False),
    ("afr_font_24", ("inter", "Inter-Medium.otf"), 24, LABEL_CHARS, False),
    ("afr_font_28", ("inter", "Inter-SemiBold.otf"), 28, LABEL_CHARS, False),
]


def render(font, ch):
    """Return (adv_px, box_w, box_h, ofs_x, ofs_y, 4-bit pixel rows)."""
    adv = font.getlength(ch)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return adv, 0, 0, 0, 0, []
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    px = img.load()
    rows = [[(px[x, y] * 15 + 127) // 255 for x in range(w)] for y in range(h)]
    # ofs_y: bottom of the box relative to the baseline, positive up.
    return adv, w, h, x0, -y1, rows


def pack4(rows):
    """Pack 4-bit pixels row after row with no row padding, high nibble first."""
    flat = [v for r in rows for v in r]
    if len(flat) % 2:
        flat.append(0)
    return bytes((flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2))


def cmaps_for(chars):
    """Contiguous runs become FORMAT0_TINY; the rest go in one SPARSE_TINY."""
    cps = [ord(c) for c in chars]
    out, i, gid = [], 0, 1
    sparse = []
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        if j - i + 1 >= 4:
            out.append(("tiny", cps[i], j - i + 1, gid, None))
        else:
            sparse.append((cps[i:j + 1], gid))
        gid += j - i + 1
        i = j + 1
    if sparse:
        first_gid = sparse[0][1]
        flat = [c for run, _ in sparse for c in run]
        start = flat[0]
        out.append(("sparse", start, flat[-1] - start + 1, first_gid, [c - start for c in flat]))
    return out


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def generate(name, path, size, chars, tabular, fallback_path=None):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    fb = ImageFont.truetype(fallback_path, size) if fallback_path else None
    glyphs = [render(fb if fb and c in FALLBACK else font, c) for c in chars]

    if tabular:
        digit_adv = max(g[0] for c, g in zip(chars, glyphs) if c.isdigit())
        fixed = []
        for c, g in zip(chars, glyphs):
            adv, w, h, ox, oy, rows = g
            if c.isdigit():
                ox = round((digit_adv - w) / 2)
                adv = digit_adv
            fixed.append((adv, w, h, ox, oy, rows))
        glyphs = fixed

    bitmap = bytearray()
    dsc = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,"]
    bmp_parts = []
    for c, (adv, w, h, ox, oy, rows) in zip(chars, glyphs):
        idx = len(bitmap)
        data = pack4(rows) if w else b""
        bitmap += data
        adv_w = round(adv * 16)
        assert adv_w < 4096 and w < 256 and h < 256 and -128 <= ox < 128 and -128 <= oy < 128, (name, c)
        assert idx < (1 << 20)
        dsc.append(f"    {{.bitmap_index = {idx}, .adv_w = {adv_w}, .box_w = {w}, .box_h = {h}, "
                   f".ofs_x = {ox}, .ofs_y = {oy}}},")
        label = c if c not in "\\\"" else "\\" + c
        bmp_parts.append(f"    /* U+{ord(c):04X} \"{label}\" */\n" + (c_bytes(data) if data else ""))

    cmaps = cmaps_for(chars)
    lists, cmap_src = [], []
    for k, (kind, start, length, gid, ul) in enumerate(cmaps):
        if kind == "tiny":
            cmap_src.append(f"    {{\n        .range_start = {start}, .range_length = {length}, .glyph_id_start = {gid},\n"
                            f"        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
                            f".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n    }}")
        else:
            lists.append(f"static const uint16_t unicode_list_{k}[] = {{\n    "
                         + ", ".join(f"0x{v:x}" for v in ul) + "\n};\n")
            cmap_src.append(f"    {{\n        .range_start = {start}, .range_length = {length}, .glyph_id_start = {gid},\n"
                            f"        .unicode_list = unicode_list_{k}, .glyph_id_ofs_list = NULL, "
                            f".list_length = {len(ul)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n    }}")

    chars_desc = "".join(chars).replace("*/", "* /")
    src = f"""/* Generated by tools/make_fonts.py; do not edit.
 * Font: {os.path.basename(path)} (SIL Open Font License 1.1), {size} px, 4 bpp.
 * Characters: {chars_desc!r}{"  (tabular digits)" if tabular else ""}
 */
#include "lvgl.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{
{chr(10).join(bmp_parts)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(dsc)}
}};

{"".join(lists)}
static const lv_font_fmt_txt_cmap_t cmaps[] = {{
{("," + chr(10)).join(cmap_src)}
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = {len(cmaps)},
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {ascent + descent},
    .base_line = {descent},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = {-max(1, size // 12)},
    .underline_thickness = {max(1, size // 24)},
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};
"""
    return src, len(bitmap)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inter-dir", default="/usr/share/fonts/opentype/inter")
    ap.add_argument("--chakra-dir", required=True)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "components", "ui", "fonts"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    header = ["/* Generated by tools/make_fonts.py; do not edit. */", "#pragma once", "", '#include "lvgl.h"', ""]
    dirs = {"inter": a.inter_dir, "chakra": a.chakra_dir}
    for name, (family, file), size, chars, tab in FONTS:
        path = os.path.join(dirs[family], file)
        if not os.path.exists(path):
            sys.exit(f"missing {path}")
        fallback = None
        if family != "inter" and any(c in FALLBACK for c in chars):
            fallback = os.path.join(dirs["inter"], FALLBACK["λ"][1])
        src, nbytes = generate(name, path, size, chars, tab, fallback)
        with open(os.path.join(a.out, name + ".c"), "w") as f:
            f.write(src)
        header.append(f"extern const lv_font_t {name};")
        print(f"{name}: {len(chars)} glyphs, {nbytes} bitmap bytes")
    with open(os.path.join(a.out, "afr_fonts.h"), "w") as f:
        f.write("\n".join(header) + "\n")


if __name__ == "__main__":
    main()
