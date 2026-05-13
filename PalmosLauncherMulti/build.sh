#!/bin/bash
set -e

# ---- usage check ----
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input_rom_file> <load_address>"
    exit 1
fi

INPUT_ROM="$1"
LOAD_ADDR="$2"

# ---- verify input file exists ----
if [ ! -f "$INPUT_ROM" ]; then
    echo "Error: input file '$INPUT_ROM' does not exist"
    exit 1
fi

# ---- verify load address is decimal or hex ----
if [[ ! "$LOAD_ADDR" =~ ^([0-9]+|0x[0-9a-fA-F]+)$ ]]; then
    echo "Error: load address must be decimal or hexadecimal (0x...)"
    exit 1
fi

# ---- build ----
make clean
make

# ---- compress ----
TMP_FILE="$(mktemp)"
trap 'rm -f "$TMP_FILE"' EXIT

./compress "$LOAD_ADDR" < "$INPUT_ROM" > "$TMP_FILE"

# ---- append to output files ----
cat "$TMP_FILE" >> pixterTest.bin
cat "$TMP_FILE" >> pixterTest.pci

# ---- write file size (little-endian u32) at offset 28 ----
PCI_SIZE=$(stat -c '%s' pixterTest.pci)

b0=$(( (PCI_SIZE      ) & 0xff ))
b1=$(( (PCI_SIZE >>  8) & 0xff ))
b2=$(( (PCI_SIZE >> 16) & 0xff ))
b3=$(( (PCI_SIZE >> 24) & 0xff ))

b0=$(printf '\\%03o' "$b0")
b1=$(printf '\\%03o' "$b1")
b2=$(printf '\\%03o' "$b2")
b3=$(printf '\\%03o' "$b3")

bb="$b0$b1$b2$b3"

printf "$bb" | \
    dd of=pixterTest.pci bs=1 seek=28 conv=notrunc status=none

# ---- explicit cleanup (still protected by trap) ----
rm -f "$TMP_FILE"

