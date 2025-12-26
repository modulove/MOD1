# save as build_grids_factory_nodes.py
import re, sys, textwrap

src = open("lookup_tables.py","r",encoding="utf-8").read()

# Extract the big `nodes = [ ... ]` literal
m = re.search(r"nodes\s*=\s*\[(.*?)\]\s*for i,\s*p\s*in\s*enumerate\(nodes\):", src, re.S)
if not m:
    print("Couldn't find `nodes = [...]` in lookup_tables.py"); sys.exit(1)

# Split into per-node lists
raw = m.group(1)
# naive split by '],', then re-add bracket
cells = [s.strip() for s in raw.split('],') if s.strip()]
cells = [ (s+(']' if not s.endswith(']') else '')) for s in cells ]

# Turn each into a list of ints
def parse_list(txt):
    nums = re.findall(r"-?\d+", txt)
    vals = list(map(int, nums))
    # Expect 96 values (32 BD + 32 SD + 32 HH)
    if len(vals) != 96:
        print("Warning: got", len(vals), "values (expected 96). Keeping anyway.")
    return vals

table = [parse_list(c) for c in cells]
num_cells = len(table)

# Emit C header
with open("grids_factory_nodes.h","w",encoding="utf-8") as f:
    f.write("// Auto-generated from Mutable Instruments Grids factory map\n")
    f.write("// Source: pichenettes/eurorack/grids/resources/lookup_tables.py\n")
    f.write("// Each cell: 96 bytes = 32 steps * 3 channels (BD,SD,HH), 0..255 probabilities.\n\n")
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("constexpr uint16_t GRIDS_NUM_CELLS = %d;\n" % num_cells)
    f.write("constexpr uint8_t  GRIDS_STEPS     = 32;\n")
    f.write("constexpr uint8_t  GRIDS_CHANNELS  = 3;  // 0=BD,1=SD,2=HH\n\n")
    f.write("const uint8_t PROGMEM GRIDS_NODES[%d][96] = {\n" % num_cells)
    for i, cell in enumerate(table):
        line = ", ".join(str(v) for v in cell)
        # Wrap to keep lines manageable
        wrapped = "\n    ".join(textwrap.wrap(line, width=95))
        f.write("  { %s }%s\n" % (wrapped, "," if i < num_cells-1 else ""))
    f.write("};\n")
print("Wrote grids_factory_nodes.h with", num_cells, "cells.")
