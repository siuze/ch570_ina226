#!/usr/bin/env python3
"""Regenerate the embedded CJK subset font header (src/ui/g_cjk_font_data.hpp).

The Windows monitor renders Chinese by merging a small subsetted TrueType font
into the Latin UI fonts at runtime. This keeps Chinese text working on machines
without Microsoft YaHei / SimHei installed, while adding only a few hundred KB
to the executable instead of a full multi-megabyte CJK font.

Requirements:
    pip install fonttools
    A TrueType CJK font. The script auto-detects one from FONT_CANDIDATES
    below (Linux WenQuanYi Zen Hei, Windows Microsoft YaHei / SimHei); pass an
    explicit path to override:  python3 generate_cjk_font.py <font.ttf/ttc>
    NOTE: the font must have TrueType (glyf) outlines - stb_truetype cannot
    read CFF/OTF outlines (e.g. Noto Sans CJK is unusable for this purpose).

License: the generated header inherits the source font's embedding terms
(WenQuanYi Zen Hei: GPL-2 with font embedding exception; Microsoft YaHei and
SimHei ship with Windows and are licensed for use as system fonts). The header
records which source font was actually used - keep it accurate when
redistributing.

Run from anywhere:  python3 generate_cjk_font.py
"""

import glob
import os
import sys

SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src")
OUT_HEADER = os.path.join(SRC_DIR, "ui", "g_cjk_font_data.hpp")
FONT_NUMBER = 0  # face inside the .ttc ("WenQuanYi Zen Hei" / "Microsoft YaHei")

# Candidate TrueType CJK fonts, searched in order; the first existing file
# wins. All entries must have TrueType (glyf) outlines (see note above).
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",  # Linux (fonts-wqy-zenhei)
    "C:\\Windows\\Fonts\\msyh.ttc",                  # Windows Microsoft YaHei
    "C:\\Windows\\Fonts\\simhei.ttf",                # Windows SimHei
]

# Codepoints already covered by the embedded Iosevka ranges must NOT be merged
# from the CJK font, otherwise duplicate glyph entries race in the atlas.
IOSEVKA_RANGES = [(0x0020, 0x00FF), (0x0370, 0x03FF), (0x2000, 0x206F),
                  (0x2190, 0x21FF), (0x25A0, 0x25FF)]

# Small safety margin for future UI strings (fullwidth forms, book-title marks,
# ideographic space, etc.). Entries inside Iosevka ranges are ignored.
MARGIN_CHARS = "「」『』《》？！～％￥０１２３４５６７８９　〇℃"


def resolve_font_path():
    """Explicit CLI path wins, then the first existing FONT_CANDIDATES entry."""
    if len(sys.argv) > 1:
        path = sys.argv[1]
        if not os.path.exists(path):
            sys.exit(f"font not found: {path}")
        return path
    for candidate in FONT_CANDIDATES:
        if os.path.exists(candidate):
            return candidate
    sys.exit("no CJK TrueType font found in FONT_CANDIDATES; pass an explicit "
             "path: python3 generate_cjk_font.py <font.ttf/ttc>")


def collect_chars():
    chars = set()
    files = [f for f in glob.glob(os.path.join(SRC_DIR, "**", "*"), recursive=True)
             if f.endswith((".cpp", ".hpp", ".h", ".rc"))]
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for ch in fh.read():
                if ord(ch) < 0x80:
                    continue
                cp = ord(ch)
                if any(lo <= cp <= hi for lo, hi in IOSEVKA_RANGES):
                    continue
                chars.add(cp)
    for ch in MARGIN_CHARS:
        cp = ord(ch)
        if not any(lo <= cp <= hi for lo, hi in IOSEVKA_RANGES):
            chars.add(cp)
    return sorted(chars)


def main():
    from fontTools import subset
    from fontTools.ttLib import TTFont

    font_path = resolve_font_path()
    print(f"source font: {font_path}")

    codepoints = collect_chars()
    text = "".join(chr(cp) for cp in codepoints)
    print(f"collected {len(codepoints)} unique codepoints from {SRC_DIR}")

    opts = subset.Options()
    opts.name_IDs = []           # drop name records (not needed by stb_truetype)
    opts.hinting = False         # drop bytecode to shrink size
    opts.drop_tables += ["GSUB", "GPOS", "GDEF"]
    opts.notdef_outline = True
    opts.recalc_bounds = True
    opts.prune_unicode_ranges = True

    font = TTFont(font_path, fontNumber=FONT_NUMBER)
    # Record the actual source in the header comment (ID 4 = full font name);
    # name records are read before subsetting drops them (name_IDs=[])
    src_name = font["name"].getDebugName(4) or font["name"].getDebugName(1) or "unknown"
    src_name = "".join(c if ord(c) < 128 else "?" for c in src_name)
    missing = [cp for cp in codepoints if cp not in font.getBestCmap()]
    if missing:
        print(f"WARNING: {len(missing)} codepoints missing in source font: "
              f"{', '.join(hex(c) for c in missing[:10])}{'...' if len(missing) > 10 else ''}")
    ss = subset.Subsetter(opts)
    ss.populate(text=text)
    ss.subset(font)

    ttf_path = os.path.join(os.path.dirname(OUT_HEADER), "_cjk_subset_tmp.ttf")
    font.save(ttf_path)
    data = open(ttf_path, "rb").read()
    os.remove(ttf_path)
    print(f"subset size: {len(data)} bytes ({len(data) // 1024} KB)")

    # Validate the produced font
    check = TTFont(ttf_path + "", lazy=True) if False else None
    font2 = TTFont(font_path, fontNumber=FONT_NUMBER)
    ss2 = subset.Subsetter(opts)
    ss2.populate(text=text)
    ss2.subset(font2)
    cmap = font2.getBestCmap()
    absent = [cp for cp in codepoints if cp not in cmap]
    if absent:
        sys.exit(f"subset validation failed, missing: {[hex(c) for c in absent[:20]]}")
    print("subset validation OK: all codepoints present")

    ranges = []
    for cp in codepoints:
        if ranges and ranges[-1][1] == cp - 1:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp])

    with open(OUT_HEADER, "w", encoding="utf-8") as out:
        out.write(f"// Autogenerated embedded CJK subset font data (source font: {src_name}).\n")
        out.write("// Regenerate with generate_cjk_font.py after adding/removing UI Chinese text.\n")
        out.write("// Redistribute under the source font's embedding license.\n")
        out.write("#pragma once\n\n")
        out.write(f"const unsigned int g_CjkFontSize = {len(data)};\n")
        out.write("alignas(4) const unsigned char g_CjkFontData[] = {\n")
        for i in range(0, len(data), 18):
            chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 18])
            out.write(f"    {chunk},\n")
        out.write("};\n\n")
        # ImWchar-compatible glyph ranges (pairs, zero-terminated).
        # ImWchar is 16-bit (unsigned short) unless IMGUI_USE_WCHAR32 is set.
        pairs = ", ".join(f"0x{a:04x}, 0x{b:04x}" for a, b in ranges)
        out.write(f"const unsigned short g_CjkFontRanges[] = {{ {pairs}, 0x0000 }};\n")
    print(f"wrote {OUT_HEADER} ({sum(1 for _ in open(OUT_HEADER))} lines)")


if __name__ == "__main__":
    main()
