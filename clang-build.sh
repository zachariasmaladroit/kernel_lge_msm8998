#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2019 zacharias.maladroit
#
# Script to build a zImage from a kernel tree

#USE_CCACHE=1
#CACHE_DIR=~/.ccache
KBUILD_BUILD_USER=zacharias.maladroit
KBUILD_BUILD_HOST=BuildHost

#~/android/toolchains/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin:
#CROSS_COMPILE=aarch64-linux-gnu- \

export PATH="/home/matthias/android/toolchains/clang_google/clang-r349610/bin/:/home/matthias/android/toolchains/aarch64-linux-android-4.9_google_03.2018/bin/:$PATH"
export LD_LIBRARY_PATH="/home/matthias/android/toolchains/clang_google/clang-r349610/lib64/:$LD_LIBRARY_PATH"
export ARCH="arm64";
export SUBARCH="arm64";
export DEFCONFIG=lineageos_h930_defconfig
source arch/${ARCH}/configs/$DEFCONFIG

# Kernel make function
function kmake() {

KCFLAGS="-Wunused-variable" make -j8 ARCH=arm64 CC=clang \
CLANG_TRIPLE=aarch64-linux-gnu- \
CROSS_COMPILE=aarch64-linux-android- \
VERBOSE=1 \
HOSTCC="/home/matthias/android/toolchains/clang_google/clang-r349610/bin/clang" \
KBUILD_COMPILER_STRING="$(/home/matthias/android/toolchains/clang_google/clang-r349610/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g')" \
"${@}"

}

kmake lineageos_h930_defconfig

kmake
