#!/bin/bash
# build-toolchain.sh - GCC as a cross compiler for the MS 0515.
#
#     build-toolchain.sh [<prefix>]        default: $HOME/pdp11-gcc
#
# Builds GNU binutils and GCC (C only) for the target pdp11-aout from
# their released sources, downloaded here, and installs them under
# <prefix>/bin as pdp11-aout-gcc, pdp11-aout-as, pdp11-aout-ld and the
# rest.  libgcc is compiled for the PDP-11/10 instruction set (-m10),
# which is what the KR1807VM1 (a T-11) executes: no EIS, no FPP.  The
# projects then build with `-m10` too (Rt11Gcc.cmake sets it).
#
# What it needs on the host: gcc, g++, make, bison, flex, texinfo, and the
# GMP, MPFR and MPC development packages (Debian/Ubuntu: build-essential
# bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev).  Sources and
# build trees go under <prefix>/src and <prefix>/build; the build takes
# about a quarter of an hour on a desktop.
#
# Point MS0515_GCC at <prefix> for the CMake projects, or put <prefix>/bin
# on the PATH.
set -euo pipefail

PREFIX="${1:-$HOME/pdp11-gcc}"
TARGET=pdp11-aout
BINUTILS=binutils-2.44
GCC=gcc-15.2.0
MIRROR=https://ftp.gnu.org/gnu
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

mkdir -p "$PREFIX/src" "$PREFIX/build"
cd "$PREFIX/src"
[ -f "$BINUTILS.tar.xz" ] || wget -q "$MIRROR/binutils/$BINUTILS.tar.xz"
[ -f "$GCC.tar.xz" ] || wget -q "$MIRROR/gcc/$GCC/$GCC.tar.xz"
[ -d "$BINUTILS" ] || tar xf "$BINUTILS.tar.xz"
[ -d "$GCC" ] || tar xf "$GCC.tar.xz"

echo "=== binutils ($BINUTILS)"
mkdir -p "$PREFIX/build/binutils"
cd "$PREFIX/build/binutils"
"$PREFIX/src/$BINUTILS/configure" --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --disable-werror --disable-gdb --disable-gprof \
    > configure.log 2>&1
make -j"$JOBS" > make.log 2>&1
make install > install.log 2>&1

export PATH="$PREFIX/bin:$PATH"

echo "=== gcc ($GCC)"
mkdir -p "$PREFIX/build/gcc"
cd "$PREFIX/build/gcc"
"$PREFIX/src/$GCC/configure" --target=$TARGET --prefix="$PREFIX" \
    --enable-languages=c --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp \
    --without-headers --with-newlib --disable-werror \
    > configure.log 2>&1
make -j"$JOBS" all-gcc > make-gcc.log 2>&1
make install-gcc > install-gcc.log 2>&1

echo "=== libgcc for the T-11 (-m10)"
make -j"$JOBS" all-target-libgcc CFLAGS_FOR_TARGET="-m10 -O2" \
    > make-libgcc.log 2>&1
make install-target-libgcc > install-libgcc.log 2>&1

echo "=== installed under $PREFIX/bin:"
"$PREFIX/bin/$TARGET-gcc" --version | head -1
"$PREFIX/bin/$TARGET-as" --version | head -1
