#!/usr/bin/env python3
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
"""Extract declared glyphs and metrics from frozen desktop font tables."""
import argparse
import hashlib
import json
from pathlib import Path
import re

VERSION = 1


def face_data(text, face):
    """Read integer glyph records, coverage and face metrics."""
    def body(kind):
        match = re.search(r'\b' + re.escape(face + '_' + kind) + r'\[\] = \{(.*?)\};', text, re.S)
        if not match:
            raise ValueError('Missing frozen table: ' + face + '_' + kind)
        return match[1]
    glyphs = [list(map(int, row.split(','))) for row in re.findall(r'\{([^{}]+)\}', body('glyphs'))]
    bits = bytes(int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', body('bits')))
    metrics = re.search(r'\b' + face + r'_font\s*=\s*\{\s*' + face + r'_glyphs,\s*' + face + r'_bits,\s*(\d+),\s*(\d+),\s*(\d+),', text)
    if not metrics or int(metrics[1]) != len(glyphs):
        raise ValueError('Invalid face descriptor: ' + face)
    if any(len(g) != 7 or min(g[2], g[3], g[6]) < 0 or g[6] + g[2] * g[3] > len(bits) for g in glyphs):
        raise ValueError('Invalid glyph coverage: ' + face)
    if [g[0] for g in glyphs] != sorted(set(g[0] for g in glyphs)):
        raise ValueError('Unordered glyphs: ' + face)
    return glyphs, bits, int(metrics[2]), int(metrics[3])


def generate(root, manifest):
    """Return C data and provenance with original metrics and coverage bytes."""
    spec = json.loads(manifest.read_text())
    source = root / spec['source']
    raw = source.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != spec['source_sha256']:
        raise ValueError('Frozen font source hash mismatch')
    if (spec['format'] != 1 or spec['faces'] != ['plain', 'bold'] or spec['pixels'] != 12
            or spec['ranges'] != [[32, 126], [160, 255]] or spec['fallback'] != 63):
        raise ValueError('Unsupported font selection')
    codepoints = sorted({cp for lo, hi in spec['ranges'] for cp in range(lo, hi + 1)})
    if not codepoints or codepoints[0] < 0 or codepoints[-1] > 32767 or spec['fallback'] not in codepoints:
        raise ValueError('Invalid glyph range or replacement glyph')
    lines = ['/*', ' * Frozen DejaVu UI coverage subset.',
             ' * Authors: Ambuj Varshney <ambuj@tiku-os.org>',
             ' * Font redistribution terms: licenses/LICENSE.DejaVu.',
             ' * SPDX-License-Identifier: Apache-2.0', ' */',
             '#include "tiku_font_subset.h"', '']
    faces = []
    for index, face in enumerate(spec['faces']):
        glyphs, bits, height, ascent = face_data(raw.decode(), face)
        by_cp = {g[0]: g for g in glyphs}
        packed = bytearray()
        rows = []
        for cp in codepoints:
            if cp not in by_cp:
                raise ValueError('Missing codepoint: ' + str(cp))
            g = by_cp[cp]
            rows.append(g[:6] + [len(packed)])
            packed.extend(bits[g[6]:g[6] + g[2] * g[3]])
        if len(packed) != spec['expected_coverage_bytes'][index]:
            raise ValueError('Coverage budget mismatch: ' + face)
        lines.append('static const tiku_glyph_t ' + face + '_glyphs[] = {')
        lines.extend('    {' + ','.join(map(str, row)) + '},' for row in rows)
        lines += ['};', 'static const unsigned char ' + face + '_bits[] = {']
        lines.extend('    ' + ','.join(str(v) for v in packed[i:i+24]) + ',' for i in range(0, len(packed), 24))
        lines += ['};', '']
        faces.append(dict(name=face, glyphs=len(rows), coverage_bytes=len(packed), record_bytes=16*len(rows), height=height, ascent=ascent))
    if sum(f['record_bytes'] for f in faces) != spec['expected_record_bytes']:
        raise ValueError('Glyph record budget mismatch')
    lines.append('const tiku_font_t tiku_font_subset_fonts[2] = {')
    for f in faces:
        name = f['name']
        lines.append('    {' + f'{name}_glyphs, {name}_bits, {f["glyphs"]}, {f["height"]}, {f["ascent"]}, {codepoints[0]}, NULL, NULL' + '},')
    lines += ['};', '_Static_assert(sizeof(tiku_glyph_t) == 16, "glyph record ABI");', '']
    output = '\n'.join(lines).encode()
    report = dict(format=1, tool_version=VERSION, source_sha256=digest,
                  manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                  generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  output_sha256=hashlib.sha256(output).hexdigest(), faces=faces,
                  fallback=spec['fallback'], license=spec['license'])
    return output, report


def main():
    """Write deterministic font data, provenance and the font license."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output, report = generate(root, args.manifest)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'tiku_font_subset_data.c').write_bytes(output)
    (args.output / 'fonts.json').write_text(json.dumps(report, indent=2) + '\n')
    (args.output / 'LICENSE.DejaVu').write_bytes((root / report['license']).read_bytes())


if __name__ == '__main__':
    main()
