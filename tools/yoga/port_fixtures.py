#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Ports Yoga's generated conformance tests (tests/generated/*.cpp, MIT, Meta Platforms) to a table of
# C fixtures for AXION's flex protocol (decision 91). Each test becomes: the nodes, their style as
# AXION properties (Yoga's start/end/horizontal/vertical/all edges resolved left to right with
# Yoga's precedence), the tree, the containing block of the left-to-right layout, and the frames
# Yoga expects for it. The right-to-left half of each test is not ported (RTL arrives with bidi, M3).
#
# Usage: tools/yoga/port_fixtures.py <yoga source dir> <output .inc>
# The output is committed (tests/unit/core/yoga_fixtures.inc); this script is only rerun to move to
# another Yoga release. It needs the source tree, not a network.
import os
import re
import subprocess
import sys

DIM_PROPS = {
    'Width': 'AX_PROP_WIDTH', 'Height': 'AX_PROP_HEIGHT',
    'MinWidth': 'AX_PROP_MIN_WIDTH', 'MinHeight': 'AX_PROP_MIN_HEIGHT',
    'MaxWidth': 'AX_PROP_MAX_WIDTH', 'MaxHeight': 'AX_PROP_MAX_HEIGHT',
    'FlexBasis': 'AX_PROP_FLEX_BASIS',
}
EDGE_GROUPS = {  # Yoga style name -> AXION longhands by physical edge
    'Margin': ['AX_PROP_MARGIN_LEFT', 'AX_PROP_MARGIN_TOP', 'AX_PROP_MARGIN_RIGHT', 'AX_PROP_MARGIN_BOTTOM'],
    'Padding': ['AX_PROP_PADDING_LEFT', 'AX_PROP_PADDING_TOP', 'AX_PROP_PADDING_RIGHT', 'AX_PROP_PADDING_BOTTOM'],
    'Position': ['AX_PROP_LEFT', 'AX_PROP_TOP', 'AX_PROP_RIGHT', 'AX_PROP_BOTTOM'],
    'Border': ['AX_PROP_BORDER_LEFT_WIDTH', 'AX_PROP_BORDER_TOP_WIDTH', 'AX_PROP_BORDER_RIGHT_WIDTH',
               'AX_PROP_BORDER_BOTTOM_WIDTH'],
}
ENUMS = {
    'FlexDirection': ('AX_PROP_FLEX_DIRECTION', {
        'YGFlexDirectionColumn': 'AX_FLEX_DIRECTION_COLUMN',
        'YGFlexDirectionColumnReverse': 'AX_FLEX_DIRECTION_COLUMN_REVERSE',
        'YGFlexDirectionRow': 'AX_FLEX_DIRECTION_ROW',
        'YGFlexDirectionRowReverse': 'AX_FLEX_DIRECTION_ROW_REVERSE'}),
    'FlexWrap': ('AX_PROP_FLEX_WRAP', {
        'YGWrapNoWrap': 'AX_FLEX_WRAP_NO_WRAP', 'YGWrapWrap': 'AX_FLEX_WRAP_WRAP',
        'YGWrapWrapReverse': 'AX_FLEX_WRAP_WRAP_REVERSE'}),
    'JustifyContent': ('AX_PROP_JUSTIFY_CONTENT', {
        'YGJustifyFlexStart': 'AX_JUSTIFY_FLEX_START', 'YGJustifyCenter': 'AX_JUSTIFY_CENTER',
        'YGJustifyFlexEnd': 'AX_JUSTIFY_FLEX_END', 'YGJustifySpaceBetween': 'AX_JUSTIFY_SPACE_BETWEEN',
        'YGJustifySpaceAround': 'AX_JUSTIFY_SPACE_AROUND', 'YGJustifySpaceEvenly': 'AX_JUSTIFY_SPACE_EVENLY'}),
    'PositionType': ('AX_PROP_POSITION', {
        'YGPositionTypeRelative': 'AX_POSITION_RELATIVE', 'YGPositionTypeAbsolute': 'AX_POSITION_ABSOLUTE',
        'YGPositionTypeStatic': 'AX_POSITION_STATIC'}),
    'Display': ('AX_PROP_DISPLAY', {
        'YGDisplayFlex': 'AX_DISPLAY_FLEX', 'YGDisplayNone': 'AX_DISPLAY_NONE'}),
    'Overflow': ('AX_PROP_OVERFLOW', {
        'YGOverflowVisible': 'AX_OVERFLOW_VISIBLE', 'YGOverflowHidden': 'AX_OVERFLOW_HIDDEN'}),
    'BoxSizing': ('AX_PROP_BOX_SIZING', {
        'YGBoxSizingBorderBox': 'AX_BOX_SIZING_BORDER_BOX',
        'YGBoxSizingContentBox': 'AX_BOX_SIZING_CONTENT_BOX'}),
}
ALIGN = {
    'YGAlignAuto': 'AX_ALIGN_AUTO', 'YGAlignFlexStart': 'AX_ALIGN_FLEX_START',
    'YGAlignCenter': 'AX_ALIGN_CENTER', 'YGAlignFlexEnd': 'AX_ALIGN_FLEX_END',
    'YGAlignStretch': 'AX_ALIGN_STRETCH', 'YGAlignBaseline': 'AX_ALIGN_BASELINE',
    'YGAlignSpaceBetween': 'AX_ALIGN_SPACE_BETWEEN', 'YGAlignSpaceAround': 'AX_ALIGN_SPACE_AROUND',
    'YGAlignSpaceEvenly': 'AX_ALIGN_SPACE_EVENLY'}
for name, prop in (('AlignItems', 'AX_PROP_ALIGN_ITEMS'), ('AlignSelf', 'AX_PROP_ALIGN_SELF'),
                   ('AlignContent', 'AX_PROP_ALIGN_CONTENT')):
    ENUMS[name] = (prop, ALIGN)

EDGE_INDEX = {'YGEdgeLeft': 'Left', 'YGEdgeTop': 'Top', 'YGEdgeRight': 'Right', 'YGEdgeBottom': 'Bottom',
              'YGEdgeStart': 'Start', 'YGEdgeEnd': 'End', 'YGEdgeHorizontal': 'Horizontal',
              'YGEdgeVertical': 'Vertical', 'YGEdgeAll': 'All'}


def resolve_edges(edges):
    """Yoga's precedence, left to right: start/end, then the physical edge, then
    horizontal/vertical, then all. Returns (left, top, right, bottom), each a value or None."""
    def pick(*names):
        for n in names:
            if n in edges:
                return edges[n]
        return None
    return (pick('Start', 'Left', 'Horizontal', 'All'), pick('Top', 'Vertical', 'All'),
            pick('End', 'Right', 'Horizontal', 'All'), pick('Bottom', 'Vertical', 'All'))


def c_float(v):
    if v == 'YGUndefined':
        return 'NAN'
    v = re.sub(r'(?<=[\d.])f\b', '', v.strip())  # C float literals: 0.2f
    if re.fullmatch(r'[-+\d.eE\s/*()]+', v) is None:
        raise SystemExit('not a number: %r' % v)
    f = float(eval(v, {'__builtins__': {}}, {}))  # a literal or a ratio such as "1 / 1"
    s = repr(f)
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s + 'f'


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


class Test:
    def __init__(self, file, name):
        self.file = file
        self.name = name
        self.skipped = False
        self.nodes = {}       # var -> index
        self.ops = []         # C initializer strings
        self.edges = {}       # (node, group) -> {edge: (value, unit)}
        self.owner = None
        self.expect = {}      # node index -> [left, top, width, height]
        self.features = set()
        self.calc_count = 0
        self.texts = {}       # node index -> text


def parse_value(arg, unit):
    return (arg.strip(), unit)


def parse_file(path):
    tests = []
    cur = None
    base = os.path.basename(path)[:-4]
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        m = re.match(r'TEST\(YogaTest, (\w+)\)', line)
        if m:
            cur = Test(base, m.group(1))
            tests.append(cur)
            continue
        if cur is None:
            continue
        if line == 'GTEST_SKIP();':
            cur.skipped = True
            continue
        m = re.match(r'YGNodeRef (\w+) = YGNodeNewWithConfig\(config\);', line)
        if m:
            cur.nodes[m.group(1)] = len(cur.nodes)
            continue
        m = re.match(r'YGNodeInsertChild\((\w+), (\w+), (\d+)\);', line)
        if m:
            cur.ops.append(('insert', cur.nodes[m.group(1)], cur.nodes[m.group(2)]))
            continue
        m = re.match(r'YGNodeSetContext\((\w+), \(void\*\)"(.*)"\);', line)
        if m:
            cur.texts[cur.nodes[m.group(1)]] = m.group(2)
            continue
        m = re.match(r'YGNodeSetMeasureFunc\((\w+), &facebook::yoga::test::IntrinsicSizeMeasure\);', line)
        if m:
            cur.ops.append(('measure', cur.nodes[m.group(1)]))
            cur.features.add('measure')
            continue
        m = re.match(r'YGNodeCalculateLayout\((\w+), ([\w.]+), ([\w.]+), (YGDirection\w+)\);', line)
        if m:
            cur.calc_count += 1
            if cur.calc_count == 1:
                assert m.group(4) == 'YGDirectionLTR', (path, cur.name)
                cur.owner = (m.group(2), m.group(3))
            continue
        m = re.match(r'ASSERT_FLOAT_EQ\(([-\d.e]+), YGNodeLayoutGet(Left|Top|Width|Height)\((\w+)\)\);', line)
        if m:
            if cur.calc_count == 1:
                idx = cur.nodes[m.group(3)]
                e = cur.expect.setdefault(idx, [None] * 4)
                e[['Left', 'Top', 'Width', 'Height'].index(m.group(2))] = m.group(1)
            continue
        m = re.match(r'YGNodeStyleSet(\w+)\((\w+)(?:, (.*))?\);', line)
        if m:
            handle_style(cur, path, m.group(1), cur.nodes[m.group(2)], m.group(3))
            continue
        if line.startswith(('YGConfigRef', 'YGConfigFree', 'YGNodeFreeRecursive', '}', '//', '#', '*')) \
                or line == '':
            continue
        raise SystemExit('%s: %s: unhandled line: %s' % (path, cur.name, line))
    return tests


def handle_style(t, path, setter, node, args):
    args = [a.strip() for a in args.split(',')] if args else []
    unit = 'AX_UNIT_POINTS'
    name = setter
    if name.endswith('Percent'):
        unit = 'AX_UNIT_PERCENT'
        name = name[:-len('Percent')]
    elif name.endswith('Auto'):
        unit = 'AX_UNIT_AUTO'
        name = name[:-len('Auto')]
    if name in DIM_PROPS:
        val = '0' if unit == 'AX_UNIT_AUTO' else args[0]
        t.ops.append(('dim', node, DIM_PROPS[name], val, unit))
        return
    if name in EDGE_GROUPS:
        edge = EDGE_INDEX[args[0]]
        val = '0' if unit == 'AX_UNIT_AUTO' else args[1]
        t.edges.setdefault((node, name), {})[edge] = (val, unit)
        if edge in ('Start', 'End'):
            t.features.add('start-end')
        return
    if name == 'Gap':
        val = args[1]
        gutters = {'YGGutterColumn': ['AX_PROP_COLUMN_GAP'], 'YGGutterRow': ['AX_PROP_ROW_GAP'],
                   'YGGutterAll': ['AX_PROP_COLUMN_GAP', 'AX_PROP_ROW_GAP']}[args[0]]
        for g in gutters:
            t.ops.append(('dim', node, g, val, unit))
        return
    if name in ('FlexGrow', 'FlexShrink', 'AspectRatio'):
        prop = {'FlexGrow': 'AX_PROP_FLEX_GROW', 'FlexShrink': 'AX_PROP_FLEX_SHRINK',
                'AspectRatio': 'AX_PROP_ASPECT_RATIO'}[name]
        t.ops.append(('f32', node, prop, args[0]))
        return
    if name in ENUMS:
        prop, values = ENUMS[name]
        v = args[0]
        if v not in values:
            t.features.add({'YGDisplayContents': 'display-contents',
                            'YGOverflowScroll': 'overflow-scroll'}.get(v, v))
            return
        if v == 'YGAlignBaseline':
            t.features.add('baseline')
        t.ops.append(('u32', node, prop, values[v]))
        return
    raise SystemExit('%s: %s: unhandled setter %s' % (path, t.name, setter))


def emit(tests, out, version):
    lines = []
    lines.append('/*')
    lines.append(' * Generated by tools/yoga/port_fixtures.py from Yoga %s, tests/generated/ (the .cpp files)' % version)
    lines.append(' * (MIT License, Copyright (c) Meta Platforms, Inc. and affiliates; see NOTICE).')
    lines.append(' * Do not edit: rerun the script. Decision 91.')
    lines.append(' */')
    lines.append('/* clang-format off */')
    texts = []
    all_ops = []
    all_expect = []
    rows = []
    for t in tests:
        # Resolve edge groups into longhands (after other ops, before inserts: order does not matter
        # for style; the tree ops are kept in order).
        style_ops = []
        tree_ops = []
        for op in t.ops:
            if op[0] in ('insert',):
                tree_ops.append(op)
            else:
                style_ops.append(op)
        for (node, group), edges in sorted(t.edges.items()):
            for i, v in enumerate(resolve_edges(edges)):
                if v is None:
                    continue
                val, unit = v
                prop = EDGE_GROUPS[group][i]
                if group == 'Border':
                    style_ops.append(('f32', node, prop, val))
                else:
                    style_ops.append(('dim', node, prop, val, unit))
        start = len(all_ops)
        for op in style_ops + tree_ops:
            if op[0] == 'dim':
                all_ops.append('{YFX_DIM, %d, %s, %s, %s}' % (op[1], op[2], c_float(op[3]), op[4]))
            elif op[0] == 'f32':
                all_ops.append('{YFX_F32, %d, %s, %s, 0}' % (op[1], op[2], c_float(op[3])))
            elif op[0] == 'u32':
                all_ops.append('{YFX_U32, %d, %s, 0.0f, %s}' % (op[1], op[2], op[3]))
            elif op[0] == 'insert':
                all_ops.append('{YFX_INSERT, %d, %d, 0.0f, 0}' % (op[1], op[2]))
            elif op[0] == 'measure':
                ti = len(texts)
                texts.append(t.texts.get(op[1], ''))
                all_ops.append('{YFX_MEASURE, %d, 0, 0.0f, %d}' % (op[1], ti))
        estart = len(all_expect)
        for i in range(len(t.nodes)):
            e = t.expect.get(i)
            if e is None or None in e:
                raise SystemExit('%s: %s: node %d has no complete expectation' % (t.file, t.name, i))
            all_expect.append('{%s, %s, %s, %s}' % tuple(c_float(x) for x in e))
        feats = ' | '.join('YFX_FEATURE_' + f.upper().replace('-', '_') for f in sorted(t.features)) or '0'
        owner = t.owner or ('YGUndefined', 'YGUndefined')
        rows.append('{"%s", "%s", %d, %s, %s, %d, %d, %d, %d, %s}' % (
            t.file, t.name, 1 if t.skipped else 0, c_float(owner[0]), c_float(owner[1]),
            start, len(all_ops) - start, estart, len(t.nodes), feats))
    lines.append('static const char *const YFX_TEXTS[] = {')
    for s in texts:
        lines.append('    %s,' % c_string(s))
    if not texts:
        lines.append('    "",')
    lines.append('};')
    lines.append('static const yfx_op YFX_OPS[] = {')
    lines += ['    %s,' % o for o in all_ops]
    lines.append('};')
    lines.append('static const yfx_frame YFX_EXPECT[] = {')
    lines += ['    %s,' % e for e in all_expect]
    lines.append('};')
    lines.append('static const yfx_test YFX_TESTS[] = {')
    lines += ['    %s,' % r for r in rows]
    lines.append('};')
    lines.append('/* clang-format on */')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: port_fixtures.py <yoga source dir> <output .inc>')
    src = sys.argv[1]
    try:
        version = subprocess.run(['git', '-C', src, 'describe', '--tags'], capture_output=True,
                                 text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        version = 'unknown version'
    gen = os.path.join(src, 'tests', 'generated')
    tests = []
    for name in sorted(os.listdir(gen)):
        if name.endswith('.cpp'):
            tests += parse_file(os.path.join(gen, name))
    emit(tests, sys.argv[2], version)
    print('%d tests ported' % len(tests))


if __name__ == '__main__':
    main()
