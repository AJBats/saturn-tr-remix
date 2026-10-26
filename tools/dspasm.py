"""Assembler for the SCU DSP (SCU manual ST-097; semantics as Mednafen's
scu_dsp_*.cpp emulate them).

usage: dspasm.py IN.dsp OUT.h NAME
       writes OUT.h holding `static const uint32_t NAME[]` and NAME_COUNT

Syntax: one instruction a line; `;` starts a comment; `label:` names the
next instruction's address. Numbers are decimal or 0x hex.

  General (one word, operations in parallel, separated by `|`):
    ALU   nop and or xor add sub ad2 sr rr sl rl rl8
    X     mov mul,p   mov S,p   mov S,x   (mov mul,p may pair with mov S,x;
                                           mov S,p with mov S,x, same S)
    Y     clr a   mov alu,a   mov S,a   mov S,y   (clr a or mov alu,a may pair
                                                  with mov S,y; mov S,a with
                                                  mov S,y, same S)
    D1    mov #IMM8,D   mov S1,D   (IMM8 a number or a label)
          S: m0-m3 (MDn[CTn]), mc0-mc3 (and CTn++)
          S1: S, all (the ALU result), alh (its upper 32 of 48 bits)
          D: mc0-mc3, rx, pl, ra0, wa0, lop, top, ct0-ct3
  mvi IMM,D[,COND]  D: mc0-mc3, rx, pl, ra0, wa0, lop, pc (call: TOP = PC - 1)
  dma d0,RAM,N      external (RA0 << 2) to RAM (mc0-mc3, prg), N longwords
                    (or N = S: the count in MDn[CTn])
  dma RAM,d0,N      RAM (mc0-mc3) to external (WA0 << 2)
  dmah ...          the same, RA0 / WA0 held
  jmp ADDR[,COND]   one delay slot
  lps   btm   end   endi
  COND: z nz s ns c nc t0 nt0 zs nzs
"""
import re
import sys

ALU = {"nop": 0, "and": 1, "or": 2, "xor": 3, "add": 4, "sub": 5, "ad2": 6, "sr": 8, "rr": 9, "sl": 10,
       "rl": 11, "rl8": 15}
SRC = {"m0": 0, "m1": 1, "m2": 2, "m3": 3, "mc0": 4, "mc1": 5, "mc2": 6, "mc3": 7}
D1_SRC = dict(SRC, all=9, alh=10)
D1_DST = {"mc0": 0, "mc1": 1, "mc2": 2, "mc3": 3, "rx": 4, "pl": 5, "ra0": 6, "wa0": 7, "lop": 10, "top": 11,
          "ct0": 12, "ct1": 13, "ct2": 14, "ct3": 15}
MVI_DST = {"mc0": 0, "mc1": 1, "mc2": 2, "mc3": 3, "rx": 4, "pl": 5, "ra0": 6, "wa0": 7, "lop": 10, "pc": 12}
COND = {"z": 0x61, "nz": 0x41, "s": 0x62, "ns": 0x42, "c": 0x64, "nc": 0x44, "t0": 0x68, "nt0": 0x48,
        "zs": 0x63, "nzs": 0x43}
RAM = {"mc0": 0, "mc1": 1, "mc2": 2, "mc3": 3, "prg": 4}


def number(text, labels):
    text = text.strip()
    if text in labels:
        return labels[text]
    return int(text, 0)


def general(ops, line, labels):
    alu = xop = xs = yop = ys = d1op = d1d = d1s = 0
    xparts, yparts = [], []
    for op in ops:
        op = op.strip()
        if op in ALU:
            alu = ALU[op]
            continue
        m = re.fullmatch(r"(clr) a", op)
        if m:
            yparts.append(("clr", None))
            continue
        m = re.fullmatch(r"mov\s+(\S+?)\s*,\s*(\S+)", op)
        if not m:
            raise SyntaxError(f"{line}: {op}")
        s, d = m.group(1), m.group(2)
        if d == "p" and s == "mul":
            xparts.append(("mul", None))
        elif d in ("p", "x"):
            xparts.append((d, SRC[s]))
        elif d == "a" and s == "alu":
            yparts.append(("alu", None))
        elif d in ("a", "y"):
            yparts.append((d, SRC[s]))
        else:
            if s.startswith("#"):
                d1op, d1s = 1, number(s[1:], labels) & 0xFF
            else:
                d1op, d1s = 3, D1_SRC[s]
            d1d = D1_DST[d]
    kinds = sorted(k for k, _ in xparts)
    srcs = {v for _, v in xparts if v is not None}
    if len(srcs) > 1:
        raise SyntaxError(f"{line}: X-bus sources differ")
    xs = srcs.pop() if srcs else 0
    xop = {(): 0, ("mul",): 2, ("p",): 3, ("x",): 4, ("mul", "x"): 6, ("p", "x"): 7}[tuple(kinds)]
    kinds = sorted(k for k, _ in yparts)
    srcs = {v for _, v in yparts if v is not None}
    if len(srcs) > 1:
        raise SyntaxError(f"{line}: Y-bus sources differ")
    ys = srcs.pop() if srcs else 0
    yop = {(): 0, ("clr",): 1, ("alu",): 2, ("a",): 3, ("y",): 4, ("clr", "y"): 5, ("alu", "y"): 6,
           ("a", "y"): 7}[tuple(kinds)]
    return (alu << 26) | (xop << 23) | (xs << 20) | (yop << 17) | (ys << 14) | (d1op << 12) | (d1d << 8) | d1s


def assemble(source):
    lines = []
    labels = {}
    for n, raw in enumerate(source.splitlines(), 1):
        text = raw.split(";")[0].strip()
        while True:
            m = re.match(r"(\w+):\s*(.*)", text)
            if not m or m.group(1) in ("mov",):
                break
            labels[m.group(1)] = len(lines)
            text = m.group(2).strip()
        if text:
            lines.append((n, text.lower()))
    words = []
    for n, text in lines:
        head, _, rest = text.partition(" ")
        args = [a.strip() for a in rest.split(",")] if rest else []
        if head == "mvi":
            imm, dest = number(args[0], labels), MVI_DST[args[1]]
            if len(args) > 2:
                words.append(0x80000000 | (dest << 26) | (COND[args[2]] << 19) | (imm & 0x7FFFF))
            else:
                words.append(0x80000000 | (dest << 26) | (imm & 0x1FFFFFF))
        elif head in ("dma", "dmah"):
            hold = head == "dmah"
            if args[0] == "d0":
                direction, ram = 0, RAM[args[1]]
            else:
                direction, ram = 1, RAM[args[0]]
            count = args[2]
            if count in SRC:
                fmt, field = 1, SRC[count]
            else:
                fmt, field = 0, number(count, labels) & 0xFF
            words.append(0xC0000000 | (2 << 15) | (hold << 14) | (fmt << 13) | (direction << 12) | (ram << 8) | field)
        elif head == "jmp":
            target = number(args[0], labels)
            cond = COND[args[1]] if len(args) > 1 else 0
            words.append(0xD0000000 | (cond << 19) | (target & 0xFF))
        elif head in ("lps", "btm", "end", "endi"):
            words.append({"btm": 0xE0000000, "lps": 0xE8000000, "end": 0xF0000000, "endi": 0xF8000000}[head])
        else:
            words.append(general(text.split("|"), n, labels))
    if len(words) > 256:
        raise ValueError(f"{len(words)} instructions: program RAM holds 256")
    return words, labels


def main():
    src, out, name = sys.argv[1:4]
    words, labels = assemble(open(src).read())
    with open(out, "w", newline="\n") as f:
        f.write(f"// Generated by tools/dspasm.py from {src.replace(chr(92), '/')}.\n")
        f.write(f"static const uint32_t {name}[] = {{\n")
        for i in range(0, len(words), 4):
            f.write("    " + ", ".join(f"0x{w:08X}" for w in words[i:i + 4]) + ",\n")
        f.write("};\n")
        f.write(f"#define {name.upper()}_COUNT {len(words)}\n")
        for label, addr in sorted(labels.items(), key=lambda kv: kv[1]):
            f.write(f"#define {name.upper()}_{label.upper()} {addr}\n")


if __name__ == "__main__":
    main()
