#!/usr/bin/env python3
"""
Generate the embedded UI font from a system TTF.

BeOS drew its interface in Swiss721 at 12 pt, antialiased.  DejaVu Sans at
12 px is the closest metric match available here; the glyphs are baked to
8-bit coverage so the runtime needs no font library and the wasm build
carries the same bytes.

The header is committed, and it lives in the interface kit next to the
tiku_font.c that includes it, not in the samples under src/ here, so the
redirect reaches across into the kits tree.

Run from the applications tree:
    kits/interface/tools/genfont.py > kits/interface/tiku_font_data.h
--preserve OLD copies every face OLD already holds, byte for byte, and
renders only the faces it lacks, so coverage does not move with the Pillow
and FreeType versions.  Write to another file than OLD.  A fresh mono face
must come from the same font file as its cut's preserved faces.

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""

import sys
from PIL import Image, ImageDraw, ImageFont
import hashlib
import argparse
import re

PRESERVED = {}
# The font file each preserved face was rendered from, by face name.
PRESERVED_FROM = {}


def generator_stamp():
    """The SHA-256 of this file: written into the bake, so check_font.py
    can tell a header made by an earlier generator from one made by this."""
    with open(__file__, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
]
BOLD = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
]
# The grid views -- the terminal, the code view, hex and diff -- need a
# face whose advance is the same for every glyph.
MONO = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
]
MONO_BOLD = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Bold.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
]
# The slanted cuts of the same families, in the same order: the one baked
# is the one whose regular cut MONO found, so the letters of a run in
# italic are the same design as the letters around it.  Noto Sans Mono
# has no italic, and None says so.
MONO_ITALIC = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Oblique.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Italic.ttf",
    None,
    "/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf",
]
MONO_BOLD_ITALIC = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-BoldOblique.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-BoldOblique.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-BoldItalic.ttf",
    None,
    "/usr/share/fonts/truetype/liberation/LiberationMono-BoldItalic.ttf",
]
# The other interface families the picker offers.  Both were chosen for
# their metrics: at 10/12/14/16 px each sits within a pixel of DejaVu Sans,
# so the layout computed against the reference face survives the switch.
SERIF = [
    "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
]
SERIF_BOLD = [
    "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
]
UIMONO = [
    "/usr/share/fonts/source-foundry-hack-fonts/Hack-Regular.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
]
UIMONO_BOLD = [
    "/usr/share/fonts/source-foundry-hack-fonts/Hack-Bold.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
]

MONO_SIZES = (9, 11, 13, 15, 17, 19, 21, 23)
SIZE = 12
# The 1x faces carry Latin-1, so a file called "caf\u00e9" draws its own name;
# the 2x faces stay ASCII and the renderer falls back to the 1x glyph
# replicated for the rest, which is legible where a blank was not.
# The 1x faces reach past Latin-1 to the scripts a device is most likely
# to name a file in: Latin Extended-A (European accents), Greek, and
# Cyrillic.  The stretch between -- Latin Extended-B, the phonetic
# blocks, the combining marks nothing here composes -- is baked blank, which
# costs a table entry and one byte rather than a glyph.
FIRST, LAST = 32, 0x4FF
ASCII_LAST = 126
def is_blank(cp):
    return (0x7F <= cp <= 0x9F or       # the C1 controls, which are not
            0x180 <= cp <= 0x36F)       # letters; and the unbaked stretch


BLANK = is_blank


# name, regular, bold, and whether to bake the 2x faces.  All of them do:
# a family is chosen to be looked at, and on a doubled screen replication
# is visibly coarser than the real thing.  The 2x faces carry ASCII only
# -- the rest falls back per glyph to the 1x face, which is what keeps
# this from being four times the coverage of everything.
FAMILIES = [
    ("Sans",  CANDIDATES, BOLD,        True),
    ("Serif", SERIF,      SERIF_BOLD,  True),
    ("Mono",  UIMONO,     UIMONO_BOLD, True),
]


def face_name(family, kind, tag):
    """The C identifier for one face.  Family 0 keeps the original names."""
    if family == 0:
        return "%s%s" % (kind, tag)
    return "f%d_%s%s" % (family, kind, tag)


def pick(paths):
    import os
    for p in paths:
        if os.path.exists(p):
            return p
    raise SystemExit("genfont: no candidate font found")


def pick_cut(regular, cut):
    """The entry of @p cut beside the first of @p regular that exists."""
    import os
    for i, p in enumerate(regular):
        if os.path.exists(p):
            if cut[i] is None or not os.path.exists(cut[i]):
                raise SystemExit("genfont: no such cut of %s" % p)
            return cut[i]
    raise SystemExit("genfont: no candidate font found")


def same_family(cut, px, path):
    """Refuse to render @p cut at @p px from another file than the cut's
    preserved faces came from, so one size ladder never mixes designs."""
    name = "%s%d" % (cut, px)
    if name in PRESERVED:
        return
    base = path.rsplit("/", 1)[-1]
    for other, source in sorted(PRESERVED_FROM.items()):
        if re.fullmatch(cut + r"\d+", other) and source != base:
            raise SystemExit("genfont: %s would come from %s, but the "
                             "preserved %s came from %s"
                             % (name, base, other, source))


def render(path, name, out, size=SIZE, forced_adv=None, hi=None,
           last=LAST, metrics=None, cell=None):
    """Write face @p name from @p path; return its advances.

    forced_adv holds the 1x face's advances, doubled for its 2x face; cell
    gives every glyph one advance, for a grid face; metrics sets the height
    and ascent, so a cut sits on its regular face's lines."""
    if name in PRESERVED:
        block = PRESERVED[name]
        # Older generated headers omit the trailing dynamic-source field.
        initializer = re.search(r"_font = \{\n([^\n]*)", block).group(1)
        if initializer.count(",") == 6:
            block = re.sub(r"(_font = \{\n[^\n]*)(\n\};)", r"\1, NULL\2",
                           block)
        out.write(block + "\n\n")
        return [int(advance) for advance in
                re.findall(r"^    \{\d+,(-?\d+),", block, re.M)]
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    height = asc + desc
    glyphs = []
    blob = []

    for cp in range(FIRST, last + 1):
        ch = chr(cp)
        if BLANK(cp):
            glyphs.append({"cp": cp, "adv": 0, "w": 1, "h": 1,
                           "ox": 0, "oy": 0, "off": len(blob)})
            blob.append(0)
            continue
        if cell is not None:
            adv = cell
        elif forced_adv is not None:
            # The 2x face advances exactly twice the 1x face, so text at
            # scale 2 fills precisely the layout the 1x metrics promised.
            adv = forced_adv[cp - FIRST] * 2
        else:
            adv = int(round(font.getlength(ch)))
        box = font.getbbox(ch)
        w = max(1, box[2] - box[0])
        h = max(1, box[3] - box[1])
        img = Image.new("L", (w + 2, h + 2), 0)
        d = ImageDraw.Draw(img)
        d.text((-box[0] + 1, -box[1] + 1), ch, font=font, fill=255)
        px = list(img.getdata())
        # PIL measures from the ascender line; the renderer draws from the
        # baseline, so rebase here rather than in every draw call.
        glyphs.append({
            "cp": cp, "adv": adv,
            "w": img.width, "h": img.height,
            "ox": box[0] - 1, "oy": box[1] - 1 - asc,
            "off": len(blob),
        })
        blob.extend(px)

    out.write("/* %s: %s at %d px, %d glyphs, %d bytes of coverage. */\n"
              % (name, path.rsplit("/", 1)[-1], size, len(glyphs), len(blob)))
    out.write("static const tiku_glyph_t %s_glyphs[] = {\n" % name)
    for g in glyphs:
        out.write("    {%d,%d,%d,%d,%d,%d,%d},\n"
                  % (g["cp"], g["adv"], g["w"], g["h"], g["ox"], g["oy"],
                     g["off"]))
    out.write("};\n")
    out.write("static const unsigned char %s_bits[] = {\n" % name)
    for i in range(0, len(blob), 24):
        out.write("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",\n")
    out.write("};\n")
    if metrics is not None:
        height, asc = metrics
    out.write("static const tiku_font_t %s_font = {\n"
              "    %s_glyphs, %s_bits, %d, %d, %d, %d, %s, NULL\n};\n\n"
              % (name, name, name, len(glyphs), height, asc, FIRST,
                 ("&%s_font" % hi) if hi else "NULL"))
    return [g["adv"] for g in glyphs]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preserve", metavar="OLD",
                        help="copy the faces OLD holds; render the rest")
    args = parser.parse_args()
    if args.preserve:
        with open(args.preserve) as source:
            previous = source.read()
        pattern = (r"/\* (\w+): (\S+) at [^\n]* \*/\n"
                   r"static const tiku_glyph_t .*?"
                   r"static const tiku_font_t \1_font = \{\n[^\n]*\n\};")
        for match in re.finditer(pattern, previous, re.S):
            PRESERVED[match.group(1)] = match.group(0)
            PRESERVED_FROM[match.group(1)] = match.group(2)
        if not PRESERVED:
            parser.error("--preserve contains no generated faces")
    out = sys.stdout
    out.write("/*\n"
              " * TikuOS kits -- the foundation TikuDesktop and TikuTracker\n"
              " * both stand on.\n"
              " *\n"
              " * Authors: Ambuj Varshney <ambuj@tiku-os.org>\n"
              " *\n"
              " * tiku_font_data.h - baked UI font coverage maps.\n"
              " *\n"
              " * GENERATED by kits/interface/tools/genfont.py -- do not edit"
              " by hand.\n"
              " * GENERATOR sha256 %s\n"
              " *\n"
              " * SPDX-License-Identifier: Apache-2.0\n"
              " */\n"
              "#ifndef TIKU_FONT_DATA_H_\n"
              "#define TIKU_FONT_DATA_H_\n\n" % generator_stamp())
    sizes = (10, 12, 14, 16)
    null = open("/dev/null", "w")
    for family, (_, regular, bold, twice) in enumerate(FAMILIES):
        for px in sizes:
            tag = "" if px == SIZE else str(px)
            plain_name = face_name(family, "plain", tag)
            bold_name = face_name(family, "bold", tag)
            hi_plain = hi_bold = None

            if twice:
                plain_adv = render(pick(regular), "probe", null, size=px,
                                   last=ASCII_LAST)
                bold_adv = render(pick(bold), "probe", null, size=px,
                                  last=ASCII_LAST)
                hi_plain, hi_bold = plain_name + "2x", bold_name + "2x"
                render(pick(regular), hi_plain, out, size=px * 2,
                       forced_adv=plain_adv, last=ASCII_LAST)
                render(pick(bold), hi_bold, out, size=px * 2,
                       forced_adv=bold_adv, last=ASCII_LAST)
            render(pick(regular), plain_name, out, size=px, hi=hi_plain)
            render(pick(bold), bold_name, out, size=px, hi=hi_bold)
    for px in MONO_SIZES:
        # One advance for every glyph, taken from a character that has
        # nothing narrow about it, so the grid is exact.  A size already
        # baked keeps the cell and the lines of its regular face.
        probe = ImageFont.truetype(pick(MONO), px)
        cell = int(round(probe.getlength("M")))
        metrics = (sum(probe.getmetrics()), probe.getmetrics()[0])
        preserved = PRESERVED.get("mono%d" % px)
        if preserved:
            fields = re.search(r"_font = \{\n([^\n]*)",
                               preserved).group(1).split(",")
            metrics = (int(fields[3]), int(fields[4]))
            cell = int(re.search(r"^    \{77,(\d+),", preserved,
                                 re.M).group(1))
        # The terminal's grid stays at Latin-1: it is a fixed cell per
        # letter, and a person reads a device's files through the
        # PROPORTIONAL face, which is the one that reaches wider.
        mono_last = 0xFF
        for cut, path in (("mono", pick(MONO)),
                          ("monobold", pick(MONO_BOLD))):
            same_family(cut, px, path)
            render(path, "%s%d" % (cut, px), out, size=px, last=mono_last,
                   cell=cell)
        # On the same grid and lines: a style changes how a letter is
        # drawn, never where it stands.
        for cut, path in (("monoitalic", pick_cut(MONO, MONO_ITALIC)),
                          ("monobolditalic",
                           pick_cut(MONO, MONO_BOLD_ITALIC))):
            same_family(cut, px, path)
            render(path, "%s%d" % (cut, px), out, size=px, last=mono_last,
                   metrics=metrics, cell=cell)
    out.write("/* The fixed-advance faces, smallest first. */\n")
    out.write("static const int mono_sizes[] = { %s };\n"
              % ", ".join(str(px) for px in MONO_SIZES))
    out.write("static const tiku_font_t *const mono_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&mono%d_font" % px for px in MONO_SIZES))
    out.write("static const tiku_font_t *const monobold_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&monobold%d_font" % px for px in MONO_SIZES))
    out.write("static const tiku_font_t *const monoitalic_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&monoitalic%d_font" % px for px in MONO_SIZES))
    out.write("static const tiku_font_t *const monobolditalic_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&monobolditalic%d_font" % px
                          for px in MONO_SIZES))

    out.write("/* Every size, smallest first, for the runtime picker. */\n")
    out.write("static const int face_sizes[] = { %s };\n"
              % ", ".join(str(px) for px in sizes))
    out.write("/* The families, in the order the picker offers them. */\n")
    out.write("static const char *const family_names[] = { %s };\n"
              % ", ".join('"%s"' % f[0] for f in FAMILIES))
    for face in ("plain", "bold"):
        out.write("static const tiku_font_t *const %s_faces[][%d] = {\n"
                  % (face, len(sizes)))
        for family in range(len(FAMILIES)):
            out.write("    { %s },\n" % ", ".join(
                "&%s_font" % face_name(family, face,
                                       "" if px == SIZE else str(px))
                for px in sizes))
        out.write("};\n")
    out.write("#endif /* TIKU_FONT_DATA_H_ */\n")


if __name__ == "__main__":
    main()
