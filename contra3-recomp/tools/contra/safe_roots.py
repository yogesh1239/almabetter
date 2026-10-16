"""Pick call targets that are safe to run as ahead-of-time code in Contra III.

A compiled routine that reaches an uncompiled gap calls the interpreter
nested inside C; that nested run cannot yield to the frame driver, so a
routine is only safe when its whole static call tree stays inside code we can
see: no path into the vblank wait ($85DF), the main idle loop, an indirect
jump/call, BRK/COP/WAI/STP, or a PLP/RTI that makes the M/X widths unknown.

usage: python3 -I safe_roots.py <snesrecomp_root> <rom> <coverage.json> <out_symbols_fragment>
"""
import collections, json, sys
sys.path.insert(0, sys.argv[1] + '/recompiler')
import snes65816 as s

rom = open(sys.argv[2], 'rb').read()
cov = json.load(open(sys.argv[3]))

BLOCKED = {0x0085DF, 0x0085EC, 0x008166, 0x00995A, 0x00819B, 0x008BE2}
UNSAFE_OPS = {'BRK', 'COP', 'WAI', 'STP', 'RTI', 'PLP', 'XCE', 'MVN', 'MVP'}

def norm(pc24):
    b = (pc24 >> 16) & 0x7f
    return (b << 16) | (pc24 & 0xffff)

def walk(entry, m, x):
    """Return (callees, unsafe_reason) for one function variant."""
    seen = set()
    work = [(entry, m, x)]
    callees = set()
    while work:
        pc24, m, x = work.pop()
        key = (pc24, m, x)
        if key in seen:
            continue
        seen.add(key)
        if len(seen) > 20000:
            return callees, 'too large'
        if norm(pc24) in BLOCKED and pc24 != entry:
            return callees, 'reaches blocked %06X' % pc24
        bank, pc = pc24 >> 16, pc24 & 0xffff
        if pc < 0x8000:
            return callees, 'executes outside ROM %06X' % pc24
        try:
            off = s.lorom_offset(bank, pc)
            ins = s.decode_insn(rom, off, pc, bank, m, x)
        except Exception:
            ins = None
        if ins is None:
            return callees, 'undecodable %06X' % pc24
        mn, mode = ins.mnem, ins.mode
        nxt = (bank << 16) | ((pc + ins.length) & 0xffff)
        if mn in UNSAFE_OPS:
            return callees, '%s at %06X' % (mn, pc24)
        if mn == 'SEP':
            work.append((nxt, m | (1 if ins.operand & 0x20 else 0), x | (1 if ins.operand & 0x10 else 0)))
            continue
        if mn == 'REP':
            work.append((nxt, m & (0 if ins.operand & 0x20 else 1), x & (0 if ins.operand & 0x10 else 1)))
            continue
        if mn in ('RTS', 'RTL'):
            continue
        if mn in ('JMP', 'JML', 'JSR', 'JSL'):
            if mode in (s.INDIR, s.INDIR_X) or (mn == 'JML' and ins.length == 3) or ins.opcode in (0x6C, 0x7C, 0xDC, 0xFC):
                return callees, 'indirect %s at %06X' % (mn, pc24)
            if mn in ('JSL', 'JML'):
                tgt = ins.operand
            else:
                tgt = (bank << 16) | ins.operand
            if mn in ('JSR', 'JSL'):
                callees.add((tgt, m, x))
                work.append((nxt, m, x))
            else:
                work.append((tgt, m, x))
            continue
        if mode in (s.REL, s.REL16):
            tgt = (bank << 16) | ins.operand
            work.append((tgt, m, x))
            if mn not in ('BRA', 'BRL'):
                work.append((nxt, m, x))
            continue
        work.append((nxt, m, x))
    return callees, None

# Observed call targets with their entry widths.
targets = collections.Counter()
for r in cov['discoveries']:
    if r['site_kind'] == 'call_gap':
        mx = r['entry_mx']
        targets[(int(r['target_pc24'], 16), int(mx[1]), int(mx[3]))] += r['observed_hits']

memo = {}
def safe(key, stack=()):
    if key in memo:
        return memo[key]
    if key in stack:
        return None  # recursion: decided by the outer frame
    if norm(key[0]) in BLOCKED:
        memo[key] = 'blocked'
        return 'blocked'
    callees, why = walk(*key)
    if why is None:
        for c in callees:
            r = safe(c, stack + (key,))
            if r:
                why = 'calls %06X: %s' % (c[0], r)
                break
    memo[key] = why
    return why

ok, bad = [], collections.Counter()
for key in sorted(targets):
    why = safe(key)
    if why is None:
        ok.append(key)
    else:
        bad[why.split(':')[0][:40]] += 1

# The best variant per address (symbols.toml has one entry per address).
best = {}
for key in ok:
    a = key[0]
    if a not in best or targets[key] > targets[best[a]]:
        best[a] = key

with open(sys.argv[4], 'w') as out:
    out.write('\n# Call targets observed by coverage capture whose whole static call tree\n'
              '# avoids the vblank wait, the idle loop, indirect jumps and unknown\n'
              '# widths (tools: safe_roots.py). Promoted to ahead-of-time code.\n')
    for a in sorted(best):
        _, m, x = best[a]
        out.write('\n[[func]]\nname = "f_%06X"\naddr = "%04x"\nbank = 0x%02x\nemit = true\n'
                  'entry_m = %d\nentry_x = %d\n' % (a, a & 0xffff, a >> 16, m, x))
print('observed variants', len(targets), 'safe', len(ok), 'addresses', len(best))
for k, v in bad.most_common(12):
    print('  excluded', v, k)
