#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2019 zacharias.maladroit
#
# Script to build a zImage from a kernel tree

USE_CCACHE=1
CACHE_DIR=~/.ccache
KBUILD_BUILD_USER=zacharias.maladroit
KBUILD_BUILD_HOST=BuildHost

PATH="~/android/toolchains/gcc-arm-8.2-2019.01-x86_64-aarch64-linux-gnu/bin:~/android/toolchains/clang_google/clang-r349610/bin:$PATH"

# Kernel make function
function kmake() {

fakeroot make -j8 ARCH=arm64 CC="${CCACHE} clang" \
CLANG_TRIPLE=aarch64-linux-gnu- \
CROSS_COMPILE=aarch64-linux-gnu- \
HOSTCC="${CCACHE} ~/android/toolchains/clang_google/clang-r349610/bin/clang" \
KBUILD_COMPILER_STRING="$(~/android/toolchains/clang_google/clang-r349610/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g')" \
"${@}"

}

kmake lineageos_h930_defconfig

kmake
