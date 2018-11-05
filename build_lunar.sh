#!/bin/sh

export USE_CCACHE=1

export CACHE_DIR=~/.ccache


export KBUILD_BUILD_USER=zacharias.maladroit
export KBUILD_BUILD_HOST=BuildHost

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-android-
export PATH=~/android/toolchains/aarch64-linux-android-4.9_google_03.2018/bin/:$PATH


make lineageos_h930_defconfig

time make -j8



mkbootimg --kernel arch/arm64/boot/Image.gz-dtb --ramdisk ~/android/final_files/ramdisks_V30_8.0/boot_H930_21A_unpack_b/ramdisk --second /dev/null --cmdline "console=ttyMSM0,115200,n8 androidboot.console=ttyMSM0 user_debug=31 msm_rtb.filter=0x37 ehci-hcd.park=3 lpm_levels.sleep_disabled=1 sched_enable_hmp=1 sched_enable_power_aware=1 service_locator.enable=1 rcupdate.rcu_expedited=1 zswap.enabled=1 zswap.compressor=lz4 zswap.max_pool_percent=20 zswap.zpool=z3fold swiotlb=2048 androidboot.configfs=true androidboot.usbcontroller=a800000.dwc3 androidboot.hardware=joan buildvariant=user" --base 0x00000000 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --second_offset 0x00f00000 --os_version 8.0.0 --os_patch_level 2018-7 --tags_offset 0x00000100 --board "" --pagesize 4096 --out ~/android/final_files/ramdisks_V30_8.0/test/H930_21A_stock/boot
