#!/usr/bin/env python3
"""
The style rules that make lint cannot check by reading comments alone:
size and documentation, the two things the kernel's coding style is
strict about and docs/plans/PLAN-split.md adopted.

For C, Objective-C and implementation fragments under the requested trees
(the shells and Tracker core for the strict default gate):

  * no function over FUNC_MAX lines, and none with more than LOCALS_MAX
    locals declared at its head;
  * no file with more than one section marker, counting both the
    three-line banner and the one-line /* ---- label */ divider (a banner
    is a directory waiting to happen);
  * a file header of at most HEADER_MAX lines;
  * no `//` comment, no line over COLS columns;
  * every function documented exactly once: an exported one at its
    prototype in a header, a static one at its definition, neither in
    both places.  A family-internal function, whose prototype lives in
    the generated block of a `*_private.h`, counts as exported only
    within the family and is documented at its definition.  A comment
    documents the declaration right below it only, and a one-word group
    label such as /* App. */ documents nothing;
  * every field of a struct in a `*_private.h` header carrying a comment.

Each rule can carry exemptions, listed below with the reason.  A rule
fails the build; the counts are printed either way, so a pass says how
much room is left. `--report` includes apps and kits by default, prints the
counts and exits 0; `--all` prints every finding on a failing strict run.

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREES = ['system/desktop', 'system/tracker', 'system/shared', 'system/deskbar']
REPORT_TREES = TREES + ['apps', 'kits']

FUNC_MAX = 200
LOCALS_MAX = 10
HEADER_MAX = 20
COLS = 80

# Exemptions, each with its reason.  A name here is a function or a file.
EXEMPT_FUNC = {
    # (none yet: the seventeen came down under the ceiling in milestone 4)
}
EXEMPT_LOCALS = {
    # Drawing and layout routines whose locals are the measurements they
    # work from: the kernel's ten is a smell test, and these read as a
    # table of rectangles rather than as several jobs.  Cut when next
    # worked on.
    'reflow_poses': 'the desktop view\'s rectangles',
    'build_pickers': 'the file panel\'s parts',
    'browser_draw': 'the browser window\'s rectangles',
    'paste': 'source, destination and the names in flight',
    'map_icons': 'the icon grid\'s measurements',
    'tiku_trk_poseview_pose_rect': 'one pose\'s rectangles',
    'pose_footprint': 'one pose\'s rectangles',
    'tiku_trk_folder_window_draw': 'the folder window\'s rectangles',
    'draw': 'the panel window\'s rectangles',
}
EXEMPT_BANNERS = {
    # Sectioned files outside the seventeen: each under the file ceiling and
    # one job read start to end.  Split when next worked on, not before.
    'system/shared/core/tiku_trk_info.c': 'the Info model, five stages',
    'system/shared/core/tiku_trk_match.c': 'matching, two stages',
    'system/shared/core/tiku_trk_panel.c': 'the file panel, two stages',
    'system/shared/ui/tiku_trk_find.c': 'the Find window, five stages',
    # Headers whose sections group one API and mirror the sections of the
    # exempt .c file behind it.
    'system/shared/core/tiku_trk_info.h': 'the Info model\'s API, as info.c',
    'system/shared/core/tiku_trk_panel.h': 'the file panel\'s API, as panel.c',
    'system/shared/ui/tiku_trk_poseview.h': 'the pose view family\'s one '
                                           'public header, by concern',
}
EXEMPT_HEADER = {
}
# Lines over 80 columns that predate the rule: the count may only fall.
# The number is the tree's at the day the rule landed; a change that adds
# one fails, a change that removes some may lower it.
LONG_LINES_CEILING = 82

TYPE_LINE = re.compile(r'^(static\s+)?(inline\s+)?[A-Za-z_][A-Za-z0-9_ \t*]*\**$')
ONELINE = re.compile(r'^(static\s+)?((?:inline\s+)?[A-Za-z_][\w \t*]*?[ *])([a-z_][a-z0-9_]*)\(')
BANNER = re.compile(r'^/\*[-=]{20,}\*/$')
# a section label: a one-line comment padded out to the banner's width
LABEL = re.compile(r'^/\* .*\s{2,}\*/$')
# a one-line divider, a rule and a label on one line: /* ---- label */
DIVIDER = re.compile(r'^/\* ?[-=]{3,}.*\*/\s*$')
# a one-word group label, /* App. */ or the generated block's /* x.c */
GROUP_LABEL = re.compile(r'^/\*\s*\S+\s*\*/$')


def is_doc_end(line):
    """Whether a line closes a comment that documents what follows, as
    against a banner or its label."""
    t = line.rstrip()
    # the split tool's markers around generated declarations document nothing
    if 'forward declarations' in t or 'Used before they are defined' in t:
        return False
    return (t.endswith('*/') and not BANNER.match(t) and not LABEL.match(t)
            and not DIVIDER.match(t) and not GROUP_LABEL.match(t))
DECL = re.compile(r'^    ((?:const\s+)?(?:unsigned\s+|struct\s+)?[A-Za-z_]\w*(?:\s+\*+|\s+|\s*\*+\s*))(.*);\s*$')


def rd(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def strip_comments_and_strings(line):
    line = re.sub(r'"(\\.|[^"])*"', '""', line)
    line = re.sub(r"'(\\.|[^'])*'", "''", line)
    return re.sub(r'/\*.*?\*/', '', line)


def functions(lines):
    """(name, static, doc, sig_line, end_line, locals) for each definition."""
    out = []
    i = 0
    n = len(lines)
    while i < n:
        m = re.match(r'^([a-z_][a-z0-9_]*)\(', lines[i])
        one = ONELINE.match(lines[i])
        if m and i > 0 and TYPE_LINE.match(lines[i - 1].rstrip()) and lines[i - 1].strip():
            name, static, k = m.group(1), lines[i - 1].startswith('static'), i - 2
        elif one:
            name, static, k = one.group(3), lines[i].startswith('static'), i - 1
        else:
            i += 1
            continue
        body = i
        while body < n and lines[body] != '{' and ';' not in lines[body]:
            body += 1
        if body == n or lines[body] != '{':
            i += 1
            continue
        j = body
        while j < n and lines[j] != '}':
            j += 1
        while k >= 0 and lines[k].strip() == '':
            k -= 1
        doc = k >= 0 and is_doc_end(lines[k])
        # locals: declarations at the head of the body, before the first statement
        b = i
        while b < j and lines[b] != '{':
            b += 1
        count = 0
        for l in lines[b + 1:j]:
            if l.strip() == '' or l.strip().startswith(('/*', '*')):
                continue
            d = DECL.match(l)
            if not d or l.strip().startswith(('return', 'if', 'for', 'while', 'switch')) or '(' in d.group(1):
                break
            depth = 0
            for ch in d.group(2):
                if ch in '([{':
                    depth += 1
                elif ch in ')]}':
                    depth -= 1
                elif ch == ',' and depth == 0:
                    count += 1
            count += 1
        out.append((name, static, doc, i, j, count))
        i = j + 1
    return out


def header_lines(lines):
    if not lines or not lines[0].startswith('/*'):
        return 0
    for k, l in enumerate(lines):
        if l.rstrip().endswith('*/'):
            return k + 1
    return 0


def main(argv):
    report_only = '--report' in argv
    show_all = '--all' in argv
    trees = [a for a in argv if not a.startswith('--')] or (REPORT_TREES if report_only else TREES)
    files = []
    for t in trees:
        for d, _, fs in os.walk(os.path.join(ROOT, t)):
            files += [os.path.join(d, f) for f in fs if f.endswith(('.c', '.h', '.inl', '.m'))]
    errors = []
    counts = dict(funcs=0, long_funcs=0, many_locals=0, banners=0, headers=0,
                  slashes=0, long_lines=0, undocumented=0, twice=0, fields=0)
    hdr_doc, hdr_all = set(), {}
    for p in files:
        if not p.endswith('.h'):
            continue
        L = rd(p).split('\n')
        # the generated shared block of a family's private header is
        # rewritten by tools/split_file.py sync and carries no comments:
        # a function whose prototype sits there is documented at its
        # definition
        ga = next((k for k, l in enumerate(L) if 'Shared across the family' in l or "Shared across the desktop's files" in l), len(L))
        gb = next((k for k, l in enumerate(L) if 'End of the shared' in l), len(L))
        for i, l in enumerate(L):
            m = re.match(r'^[A-Za-z_][\w \t*]*?\b([a-z_][a-z0-9_]*)\s*\(', l)
            if m and not l.startswith(('static', '#', '/', '*', ' ', 'typedef')):
                if ga < i < gb:
                    hdr_all.setdefault(m.group(1), os.path.relpath(p, ROOT) + ' (generated block: document the definition)')
                    continue
                k = i - 1
                while k >= 0 and L[k].strip() == '':
                    k -= 1
                hdr_all.setdefault(m.group(1), os.path.relpath(p, ROOT))
                # a comment documents the declaration right below it, and a
                # one-word group label documents nothing
                if (k >= 0 and is_doc_end(L[k])
                        and not GROUP_LABEL.match(L[k].strip())
                        and 'Shared across' not in L[k]):
                    hdr_doc.add(m.group(1))
    for p in sorted(files):
        rel = os.path.relpath(p, ROOT)
        L = rd(p).split('\n')
        for i, l in enumerate(L):
            if len(l) > COLS:
                counts['long_lines'] += 1
            if re.search(r'(^|\s)//', strip_comments_and_strings(l)):
                counts['slashes'] += 1
                errors.append('%s:%d: a // comment' % (rel, i + 1))
        banners = 0
        k = 0
        while k < len(L):
            if k + 2 < len(L) and BANNER.match(L[k]) and BANNER.match(L[k + 2]):
                banners += 1
                k += 3          # its closing rule opens no second banner
                continue
            if DIVIDER.match(L[k]) and not BANNER.match(L[k]):
                banners += 1
            k += 1
        if banners > 1 and rel not in EXEMPT_BANNERS:
            counts['banners'] += 1
            errors.append('%s: %d section banners; a banner is a file waiting to happen' % (rel, banners))
        h = header_lines(L)
        if h > HEADER_MAX and rel not in EXEMPT_HEADER:
            counts['headers'] += 1
            errors.append('%s: file header is %d lines (max %d)' % (rel, h, HEADER_MAX))
        if p.endswith('_private.h'):
            s = '\n'.join(L)
            for m in re.finditer(r'(typedef struct \w*\s*|^struct \w+\s*)\{(.*?)^\}', s, re.S | re.M):
                base = s[:m.start()].count('\n') + 1
                body = m.group(2).split('\n')
                for j, fl in enumerate(body):
                    above = body[j - 1].strip() if j > 0 else ''
                    if (re.match(r'^\s+[A-Za-z_][\w \t*]*\w+(\[[^\]]*\])*;', fl) and '/*' not in fl
                            and not (above.startswith('/*') or above.startswith('*') or above.endswith('*/'))):
                        counts['fields'] += 1
                        errors.append('%s:%d: field without a comment: %s' % (rel, base + j, fl.strip()))
        if not p.endswith(('.c', '.inl', '.m')):
            continue
        for name, static, doc, i, j, nloc in functions(L):
            counts['funcs'] += 1
            if j - i > FUNC_MAX and name not in EXEMPT_FUNC:
                counts['long_funcs'] += 1
                errors.append('%s:%d: %s is %d lines (max %d)' % (rel, i + 1, name, j - i, FUNC_MAX))
            if nloc > LOCALS_MAX and name not in EXEMPT_LOCALS:
                counts['many_locals'] += 1
                errors.append('%s:%d: %s declares %d locals (max %d)' % (rel, i + 1, name, nloc, LOCALS_MAX))
            if static and not doc:
                counts['undocumented'] += 1
                errors.append('%s:%d: static %s has no comment at its definition' % (rel, i + 1, name))
            elif not static and not doc and name not in hdr_doc:
                counts['undocumented'] += 1
                where = hdr_all.get(name)
                errors.append('%s:%d: %s is documented nowhere%s' % (rel, i + 1, name, (' (prototype in %s)' % where) if where else ''))
            elif not static and doc and name in hdr_doc:
                counts['twice'] += 1
                errors.append('%s:%d: %s is documented here and at its prototype in %s' % (rel, i + 1, name, hdr_all[name]))
    if counts['long_lines'] > LONG_LINES_CEILING:
        errors.append('%d lines over %d columns (ceiling %d)' % (counts['long_lines'], COLS, LONG_LINES_CEILING))
    print('check_style: %(funcs)d functions, %(long_funcs)d over the length ceiling, '
          '%(many_locals)d over the locals ceiling; %(undocumented)d undocumented, '
          '%(twice)d documented twice; %(fields)d private fields uncommented; '
          '%(banners)d files with several banners, %(headers)d headers over; '
          '%(slashes)d // comments, %(long_lines)d lines over 80' % counts)
    if errors and not report_only:
        for e in (errors if show_all else errors[:40]):
            print('  ' + e)
        if len(errors) > 40 and not show_all:
            print('  ... and %d more (%d total)' % (len(errors) - 40, len(errors)))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
