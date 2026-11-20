"""Reject S31 builds that resolve our grayscale JPEG decoder to ROM's ABI."""
from pathlib import Path
import re
import sys

link_map = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
for symbol in ("thingsos_jpeg_prepare", "thingsos_jpeg_decomp"):
    matches = re.findall(r"^\s*0x([0-9a-fA-F]+)\s+" + symbol + r"\s*$", link_map, re.M)
    if len(matches) != 1:
        raise SystemExit(f"Missing or ambiguous local JPEG symbol: {symbol}")
    address = int(matches[0], 16)
    if not (0x40000000 <= address < 0x40400000 or 0x2F000000 <= address < 0x2F080000):
        raise SystemExit(f"JPEG symbol outside application code: {symbol}=0x{address:x}")
    print(f"Verified application JPEG symbol: {symbol}=0x{address:x}")
