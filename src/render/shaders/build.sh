#!/bin/bash
# Assembles the collision viewer shaders into data/collision_gsh.bin, which
# the Makefile embeds as collision_gsh_bin. Needs latte-assembler from a
# decaf-emu release <https://github.com/decaf-emu/decaf-emu>, either on PATH
# or named by LATTE_ASSEMBLER.
set -e
cd "$(dirname "$0")"
LA="${LATTE_ASSEMBLER:-latte-assembler}"
"$LA" assemble --vsh=collision.vsh --psh=collision.psh collision.gsh
mv -f collision.gsh ../../../data/collision_gsh.bin
echo "wrote data/collision_gsh.bin"
