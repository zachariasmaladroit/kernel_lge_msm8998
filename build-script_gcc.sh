export USE_CCACHE=1

export CACHE_DIR=~/.ccache


export KBUILD_BUILD_USER=kernel.dev
export KBUILD_BUILD_HOST=BuildHost

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export PATH=~/android/toolchains/gcc-linaro-6.5.0-2018.12-x86_64_aarch64-linux-gnu/bin/:$PATH


fakeroot make lineageos_h930_defconfig


fakeroot make -j8
