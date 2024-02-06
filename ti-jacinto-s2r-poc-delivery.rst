============================
TI-Jacinto7-S2R POC Delivery
============================

:Date: 2023-02-06
:Version: 3
:Authors: Thomas Richard <thomas.richard@bootlin.com>

Introduction
============

This document describes the POC delivery for Suspend to RAM for J7200 target.

It's only a POC and not all features of the board are supported for now.

Repositories
============

* Kernel:

  - https://bitbucket.itg.ti.com/users/a0504495/repos/ti-linux-kernel-jacinto/browse
  - branch ti-linux-6.1.y-7200-lpm

* U-Boot:

  - https://bitbucket.itg.ti.com/users/a0504495/repos/ti-u-boot-jacinto/browse
  - branch ti-u-boot-2023.04-7200-lpm

* Linux Firmware:

  - https://bitbucket.itg.ti.com/users/a0504495/repos/ti-linux-firmware-jacinto/browse
  - branch: sdk-7200-lpm

* ATF:

  - https://bitbucket.itg.ti.com/users/a0504495/repos/arm-trusted-firmware-jacinto/browse
  - branch sdk-7200-lpm

* DM-Firmware:

  - https://bitbucket.itg.ti.com/users/a0504495/repos/pdk_jacinto/browse
  - branch master-s2r-j7200

The BL32 binary added in ti-linux-firmware-jacinto repository is the BL32
binary compiled using master branch (commit 439c5ecbb) which includes the fix
for the OP-TEE hwrng.

The BL31 binary added in ti-linux-firmware-jacinto repository was built using
ATF sources from arm-trusted-firmware-jacinto repository (branch
sdk-7200-lpm).

The command line was modified to build power management support.
::

   $ make ARCH=aarch64 PLAT=k3 TARGET_BOARD=j7200 K3_PM_SYSTEM_SUSPEND=1 SPD=opteed all

The DM-Firmware binary added in ti-linux-firmware-jacinto repository was built
using the pdk branch master-s2r-j7200 (commit bc412f853).

For ATF, the sdk-7200-lpm is based on the commit d7a7135d3 (sdk 09.01.00.07).

For ti-linux-firmware, the sdk-7200-lpm branch is based on the tag 09.01.00.07


Scope / Limitations
===================

For now the Suspend to RAM support was only implemented for J7200 target.

Suspend to RAM is not supported for remoteproc and USB3, that's why the drivers
are removed in the procedure.

Build the POC
=============

* Download the SDK version 09.01.00.07 and install it.
  
* Remove existing linux and uboot repositories.
  ::

     $ rm -rf board-support/ti-linux-* board-support/ti-u-boot-* board-support/optee-os-* board-support/trusted-firmware-a*

* Clone all needed repositories
  ::

     $ git clone --branch ti-linux-6.1.y-7200-lpm --single-branch --depth 1 \
       ssh://git@bitbucket.itg.ti.com/~a0504495/ti-linux-kernel-jacinto.git board-support/ti-linux-kernel-jacinto
       
     $ git clone --branch ti-u-boot-2023.04-7200-lpm --single-branch --depth 1 \
       ssh://git@bitbucket.itg.ti.com/~a0504495/ti-u-boot-jacinto.git board-support/ti-u-boot-jacinto
       
     $ git clone --branch sdk-7200-lpm --single-branch --depth 1 \
       ssh://git@bitbucket.itg.ti.com/~a0504495/ti-linux-firmware-jacinto.git board-support/ti-linux-firmware-jacinto

     $ git clone --branch sdk-7200-lpm --single-branch --depth 1 \
       ssh://git@bitbucket.itg.ti.com/~a0504495/arm-trusted-firmware-jacinto.git board-support/arm-trusted-firmware-jacinto

     $ git clone --branch master-7200-lpm --single-branch --depth 1 \
       ssh://git@bitbucket.itg.ti.com/~a0504495/pdk_jacinto.git board-support/pdk-jacinto
     
* Remove kernel config fragments
  ::

     $ sed -i -e 's/ti_arm64_prune.config//' makerules/Makefile_linux
     $ sed -i -e 's/ti_arm64_prune.config//' makerules/Makefile_linux-dtbs
    
* Build
  ::

     $ export TI_LINUX_FIRMWARE='$(TI_SDK_PATH)/board-support/ti-linux-firmware-jacinto'
     $ export UBOOT_ATF='$(TI_SDK_PATH)/board-support/ti-linux-firmware-jacinto/bl31/bl31.bin'
     $ export UBOOT_TEE='$(TI_SDK_PATH)/board-support/ti-linux-firmware-jacinto/bl32/bl32.bin'
     $ MAKE_ALL_TARGETS="u-boot linux linux-dtbs" make

* Copy generated tiboot3.bin, tispl.bin, and uboot.img
  ::

     $ mkdir -p binaries/
     $ cp board-support/ti-u-boot-jacinto/build/r5/tiboot3.bin binaries/
     $ cp board-support/ti-u-boot-jacinto/build/a72/tispl.bin binaries/
     $ cp board-support/ti-u-boot-jacinto/build/a72/u-boot.img binaries/

* Override some U-Boot env variables (load of rproc firmwares during the boot causes some issues
  with DM-Firmware during the resume).
  ::

     $ cat <<EOF> binaries/uEnv.txt
     > rproc_fw_binaries=
     > EOF

* Create the SDcard:
  ::
     $ sudo ./bin/create-sdcard.sh

     ################################################################################

     This script will create a bootable SD card from custom or pre-built binaries.

     The script must be run with root permissions and from the bin directory of
     the SDK

     Example:
     $ sudo ./create-sdcard.sh

     Formatting can be skipped if the SD card is already formatted and
     partitioned properly.

     ################################################################################

     ./bin/create-sdcard.sh: line 85: /bin/common.sh: No such file or directory

     Available Drives to write images to:

     #  major   minor    size   name
     1:   8       16   15558144 sdb

     Enter Device Number or n to exit: 1

     sdb was selected

     /dev/sdb is an sdx device
     Unmounting the sdb drives
     unmounted /dev/sdb1
     unmounted /dev/sdb2
     Current size of sdb1 131072 bytes
     Current size of sdb2 15426048 bytes

     ################################################################################

     Detected device has 2 partitions already

     Re-partitioning will allow the choice of 2 or 3 partitions

     ################################################################################

     Would you like to re-partition the drive anyways [y/n] : y


     Now partitioning sdb ...


     ################################################################################

     Select 2 partitions if only need boot and rootfs (most users).
     Select 3 partitions if need SDK & other content on SD card.  This is
     usually used by device manufacturers with access to partition tarballs.

     ****WARNING**** continuing will erase all data on sdb

     ################################################################################

     Number of partitions needed [2/3] : 2


     Now partitioning sdb with 2 partitions...


     ################################################################################

     Now making 2 partitions

     ################################################################################

     1024+0 records in
     1024+0 records out
     1048576 bytes (1.0 MB, 1.0 MiB) copied, 0.265555 s, 3.9 MB/s
     DISK SIZE - 15931539456 bytes

     Welcome to fdisk (util-linux 2.38.1).
     Changes will remain in memory only, until you decide to write them.
     Be careful before using the write command.

     Device does not contain a recognized partition table.
     Created a new DOS (MBR) disklabel with disk identifier 0xf4a32d37.

     Command (m for help): Partition type
     p   primary (0 primary, 0 extended, 4 free)
     e   extended (container for logical partitions)
     Select (default p): Partition number (1-4, default 1): First sector (2048-31116287, default 2048): Last sector, +/-sectors or +/-size{K,M,G,T,P} (2048-31116287, default 31116287):
     Created a new partition 1 of type 'Linux' and of size 128 MiB.
     Partition #1 contains a vfat signature.

     Command (m for help): Partition type
     p   primary (1 primary, 0 extended, 3 free)
     e   extended (container for logical partitions)
     Select (default p): Partition number (2-4, default 2): First sector (264192-31116287, default 264192): Last sector, +/-sectors or +/-size{K,M,G,T,P} (264192-31116287, default 31116287):
     Created a new partition 2 of type 'Linux' and of size 14.7 GiB.
     Partition #2 contains a ext4 signature.

     Command (m for help): Partition number (1,2, default 2): Hex code or alias (type L to list all):
     Changed type of partition 'Linux' to 'W95 FAT32 (LBA)'.

     Command (m for help): Partition number (1,2, default 2):
     The bootable flag on partition 1 is enabled now.

     Command (m for help): The partition table has been altered.
     Calling ioctl() to re-read partition table.
     Syncing disks.


     ################################################################################

     Partitioning Boot

     ################################################################################
     mkfs.fat 4.2 (2021-01-31)
     mkfs.fat: Warning: lowercase labels might not work properly on some systems

     ################################################################################

     Partitioning rootfs

     ################################################################################
     mke2fs 1.47.0 (5-Feb-2023)
     /dev/sdb2 contains a ext4 file system labelled 'root'
     last mounted on Tue Feb  6 17:50:26 2024
     Proceed anyway? (y,N) y
     Creating filesystem with 3856512 4k blocks and 964768 inodes
     Filesystem UUID: 2d412169-0bc9-4930-a93e-386c94d3438d
     Superblock backups stored on blocks:
     32768, 98304, 163840, 229376, 294912, 819200, 884736, 1605632, 2654208

     Allocating group tables: done
     Writing inode tables: done
     Creating journal (16384 blocks): done
     Writing superblocks and filesystem accounting information: done



     ################################################################################

     Partitioning is now done
     Continue to install filesystem or select 'n' to safe exit

     **Warning** Continuing will erase files any files in the partitions

     ################################################################################


     Would you like to continue? [y/n] : y



     Mount the partitions

     Emptying partitions


     Syncing....

     ################################################################################

     Choose file path to install from

     1 ) Install pre-built images from SDK
     2 ) Enter in custom boot and rootfs file paths

     ################################################################################

     Choose now [1/2] : 2



     ################################################################################

     For U-boot and MLO

     If files are located in Tarball write complete path including the file name.
     e.x. $:  /home/user/MyCustomTars/boot.tar.xz

     If files are located in a directory write the directory path
     e.x. $: /ti-processor-sdk-linux/board-support/prebuilt-images/

     NOTE: Not all platforms will have an MLO file and this file can
     be ignored for platforms that do not support an MLO.

     Update: The proper location for the kernel image and device tree
     files have moved from the boot partition to the root filesystem.

     ################################################################################

     Enter path for Boot Partition : binaries

     Directory exists

     This directory contains:
     tiboot3.bin  tispl.bin	u-boot.img  uEnv.txt

     Is this correct? [y/n] : y


     ################################################################################

     For Kernel Image and Device Trees files

     What would you like to do?
     1) Reuse kernel image and device tree files found in the selected rootfs.
     2) Provide a directory that contains the kernel image and device tree files
     to be used.

     ################################################################################

     Choose option 1 or 2 : 1


     Reusing kernel and dt files from the rootfs's boot directory



     ################################################################################

     For Rootfs partition

     If files are located in Tarball write complete path including the file name.
     e.x. $:  /home/user/MyCustomTars/rootfs.tar.xz

     If files are located in a directory write the directory path
     e.x. $: /ti-processor-sdk-linux/targetNFS/

     ################################################################################

     Enter path for Rootfs Partition : filesystem/tisdk-base-image-j7200-evm.tar.xz

     File exists


     ################################################################################

     Copying files now... will take minutes

     ################################################################################

     Copying boot partition



     tispl.bin copied


     tiboot3.bin copied


     u-boot.img copied

     uEnv.txt copied


     Copying rootfs System partition



     Syncing...

     Un-mount the partitions

     Remove created temp directories

     Operation Finished

     
* Mount the SDcard and remove /boot of the rootfs
  ::

     $ sudo mount /dev/sdb2 /mnt/
     $ sudo rm -rf /mnt/boot/*

* Do linux_install in the SDcard rootfs
  ::

     $ sudo ROOTFS_PART="/mnt" make linux_install

* Remove remoteproc drivers (suspend to ram not suported)
  ::

     $ sudo rm -rf /mnt/lib/modules/*/kernel/drivers/remoteproc/

* Remove usb3 drivers (suspend to ram not suported)
  ::

     $ sudo rm -rf /mnt/lib/modules/*/kernel/drivers/usb/cdns3/

* Umount SDcard
  ::

     $ sync
     $ sudo umount /mnt
  

Test Suspend to RAM
===================

* Boot the board from SDCard

* Test Suspend/Resume with console suspend and no messages
  ::

     root@j7200-evm:~# echo 0 > /proc/sys/kernel/printk
     root@j7200-evm:~# echo deep > /sys/power/mem_sleep
     root@j7200-evm:~# echo mem > /sys/power/state

     ##### Push The SW12 CAN_WKP button to resume #####
     
     U-Boot SPL 2023.10-rc4-02169-ge5a49d3b4e (Oct 02 2023 - 17:43:36 +0200)
     SYSFW ABI: 3.1 (firmware rev 0x0009 '9.0.6--v09.00.06 (Kool Koala)')
     Trying to boot from MMC2
     Starting ATF on ARM64 core...

     I/TC: Secondary CPU 1 initializing
     I/TC: Secondary CPU 1 switching to normal world boot
     root@j7200-evm:~# 

* Test Suspend/Resume without console suspend
  ::

     root@j7200-evm:~# echo 0 > /sys/module/printk/parameters/console_suspend
     root@j7200-evm:~# echo 8 > /proc/sys/kernel/printk
     root@j7200-evm:~# echo deep > /sys/power/mem_sleep
     root@j7200-evm:~# echo mem > /sys/power/state
     [  344.788749] PM: suspend entry (deep)
     [  344.792400] Filesystems sync: 0.000 seconds
     [  344.797017] Freezing user space processes
     [  344.802363] Freezing user space processes completed (elapsed 0.001 seconds)
     [  344.809328] OOM killer disabled.
     [  344.812544] Freezing remaining freezable tasks
     [  344.818066] Freezing remaining freezable tasks completed (elapsed 0.001 seconds)
     [  344.830792] Disabling non-boot CPUs ...
     [  344.836148] psci: CPU1 killed (polled 0 ms)

     ##### Push The SW12 CAN_WKP button to resume #####

     U-Boot SPL 2023.10-rc4-02169-ge5a49d3b4e (Oct 02 2023 - 17:43:36 +0200)
     SYSFW ABI: 3.1 (firmware rev 0x0009 '9.0.6--v09.00.06 (Kool Koala)')
     Trying to boot from MMC2
     Starting ATF on ARM64 core...

     [  344.840815] Enabling non-boot CPUs ...
     I/TC: Secondary CPU 1 initializing
     I/TC: Secondary CPU 1 switching to normal world boot
     [  345.648317] Detected PIPT I-cache on CPU1
     [  345.652342] GICv3: CPU1: found redistributor 1 region 0:0x0000000001920000
     [  345.659240] CPU1: Booted secondary processor 0x0000000001 [0x411fd080]
     [  345.666185] CPU1 is up
     [  345.674542] OOM killer enabled.
     [  345.677700] Restarting tasks ... done.
     [  345.682725] random: crng reseeded on system resumption
     [  345.688228] PM: suspend exit
     root@j7200-evm:~#

The button to resume the board is SW12 CAN_WKP.

Tests
=====

This chapter describes all tested features and the corresponding procedure

ADC
---

::

   root@j7200-evm:~#cat /sys/bus/iio/devices/iio:device0/in_voltage*_raw
   796
   781
   775
   779
   781
   504
   769
   792
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#cat /sys/bus/iio/devices/iio:device0/in_voltage*_raw
   803
   785
   777
   794
   784
   502
   745
   794

Ethernet
--------

For this test, the board shall be connected to a network with DHCP server.
::

   root@j7200-evm:~#udhcpc -i eth0
   udhcpc: started, v1.35.0
   udhcpc: broadcasting discover
   udhcpc: broadcasting select for 192.168.10.9, server 192.168.10.1
   udhcpc: lease of 192.168.10.9 obtained from 192.168.10.1, lease time 86400
   /etc/udhcpc.d/50default: Adding DNS 192.168.10.1
   root@j7200-evm:~#ping -c 3 8.8.8.8
   PING 8.8.8.8 (8.8.8.8): 56 data bytes
   64 bytes from 8.8.8.8: seq=0 ttl=115 time=5.904 ms
   64 bytes from 8.8.8.8: seq=1 ttl=115 time=5.504 ms
   64 bytes from 8.8.8.8: seq=2 ttl=115 time=5.044 ms

   --- 8.8.8.8 ping statistics ---
   3 packets transmitted, 3 packets received, 0% packet loss
   round-trip min/avg/max = 5.044/5.484/5.904 ms
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#sleep 5 ;ping -c 3 8.8.8.8
   PING 8.8.8.8 (8.8.8.8): 56 data bytes
   64 bytes from 8.8.8.8: seq=0 ttl=115 time=5.786 ms
   64 bytes from 8.8.8.8: seq=1 ttl=115 time=5.618 ms
   64 bytes from 8.8.8.8: seq=2 ttl=115 time=5.223 ms

   --- 8.8.8.8 ping statistics ---
   3 packets transmitted, 3 packets received, 0% packet loss
   round-trip min/avg/max = 5.223/5.542/5.786 ms
   root@j7200-evm:~#

GPIO
----

For this test, a GPIO loopback shall be defined in the device tree
::

   --- a/arch/arm64/boot/dts/ti/k3-j7200-common-proc-board.dts
   +++ b/arch/arm64/boot/dts/ti/k3-j7200-common-proc-board.dts
   @@ -165,6 +165,12 @@ vdd_sd_dv_pins_default: vdd-sd-dv-pins-default {
                           J721E_IOPAD(0xd0, PIN_OUTPUT, 7) /* (T5) SPI0_D1.GPIO0_55 */
                   >;
           };
   +
   +       gpio0_12: gpio0_12 {
   +               pinctrl-single,pins = <
   +                       J721E_IOPAD(0x2c, PIN_INPUT, 7) /* GPIO0_12 */
   +               >;
   +       };
    };

    &main_pmx1 {
   diff --git a/arch/arm64/boot/dts/ti/k3-j7200-main.dtsi b/arch/arm64/boot/dts/ti/k3-j7200-main.dtsi
   index 3f3d0abc4..3474fa8c5 100644
   --- a/arch/arm64/boot/dts/ti/k3-j7200-main.dtsi
   +++ b/arch/arm64/boot/dts/ti/k3-j7200-main.dtsi
   @@ -826,6 +826,8 @@ main_gpio0: gpio@600000 {
                   power-domains = <&k3_pds 105 TI_SCI_PD_EXCLUSIVE>;
                   clocks = <&k3_clks 105 0>;
                   clock-names = "gpio";
   +               pinctrl-0 = <&gpio0_12>;
   +               pinctrl-names = "default";
           };

           main_gpio2: gpio@610000 {

Test procedure
::

   root@j7200-evm:~#killall gpiomon
   killall: gpiomon: no process killed
   root@j7200-evm:~#gpiomon 5 12 &
   [1] 552
   root@j7200-evm:~#echo Push/Release SW1; sleep 5
   Push/Release SW1
   event: FALLING EDGE offset: 12 timestamp: [      20.810029875]
   event:  RISING EDGE offset: 12 timestamp: [      20.906335360]
   event: FALLING EDGE offset: 12 timestamp: [      21.011739560]
   event:  RISING EDGE offset: 12 timestamp: [      21.095866990]
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#killall gpiomon
   [1]+  Done                    gpiomon 5 12
   root@j7200-evm:~#gpiomon 5 12 &
   [1] 571
   root@j7200-evm:~#echo Push/Release SW1; sleep 5
   Push/Release SW1
   event: FALLING EDGE offset: 12 timestamp: [      27.068110600]
   event:  RISING EDGE offset: 12 timestamp: [      27.159081085]
   event: FALLING EDGE offset: 12 timestamp: [      27.244269100]
   event:  RISING EDGE offset: 12 timestamp: [      27.333000605]
   root@j7200-evm:~#killall gpiomon
   [1]+  Done                    gpiomon 5 12
   root@j7200-evm:~#

PCIe
----

For this test a PCIe network card (Intel, igb driver) is connected in the PCIe slot.
One ethernet port of this PCIe card is connected to a network with DHCP server.
To be sure, the onboard ethernet was unplugged for the test.
::

   root@j7200-evm:~# lspci
   00:00.0 PCI bridge: Texas Instruments Device b00f
   01:00.0 Ethernet controller: Intel Corporation I350 Gigabit Network Connection (rev 01)
   01:00.1 Ethernet controller: Intel Corporation I350 Gigabit Network Connection (rev 01)
   01:00.2 Ethernet controller: Intel Corporation I350 Gigabit Network Connection (rev 01)
   01:00.3 Ethernet controller: Intel Corporation I350 Gigabit Network Connection (rev 01)
   root@j7200-evm:~# ip link
   1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
   link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
   2: eth0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc mq state DOWN mode DEFAULT group default qlen 1000
   link/ether 24:76:25:96:c9:3c brd ff:ff:ff:ff:ff:ff
   3: enp1s0f0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
   link/ether b4:96:91:12:77:78 brd ff:ff:ff:ff:ff:ff
   4: enp1s0f1: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc mq state DOWN mode DEFAULT group default qlen 1000
   link/ether b4:96:91:12:77:79 brd ff:ff:ff:ff:ff:ff
   5: enp1s0f2: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc mq state DOWN mode DEFAULT group default qlen 1000
   link/ether b4:96:91:12:77:7a brd ff:ff:ff:ff:ff:ff
   6: enp1s0f3: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc mq state DOWN mode DEFAULT group default qlen 1000
   link/ether b4:96:91:12:77:7b brd ff:ff:ff:ff:ff:ff
   root@j7200-evm:~#
   root@j7200-evm:~#
   root@j7200-evm:~#udhcpc -i enp1s0f0
   udhcpc: started, v1.35.0
   udhcpc: broadcasting discover
   udhcpc: broadcasting select for 192.168.10.38, server 192.168.10.1
   udhcpc: lease of 192.168.10.38 obtained from 192.168.10.1, lease time 86400
   /etc/udhcpc.d/50default: Adding DNS 192.168.10.1
   root@j7200-evm:~#ping -c 3 8.8.8.8
   PING 8.8.8.8 (8.8.8.8): 56 data bytes
   64 bytes from 8.8.8.8: seq=0 ttl=115 time=6.098 ms
   64 bytes from 8.8.8.8: seq=1 ttl=115 time=7.086 ms
   64 bytes from 8.8.8.8: seq=2 ttl=115 time=5.765 ms

   --- 8.8.8.8 ping statistics ---
   3 packets transmitted, 3 packets received, 0% packet loss
   round-trip min/avg/max = 5.765/6.316/7.086 ms
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#sleep 5 ;ping -c 3 8.8.8.8
   PING 8.8.8.8 (8.8.8.8): 56 data bytes
   64 bytes from 8.8.8.8: seq=0 ttl=115 time=5.803 ms
   64 bytes from 8.8.8.8: seq=1 ttl=115 time=5.578 ms
   64 bytes from 8.8.8.8: seq=2 ttl=115 time=5.523 ms

   --- 8.8.8.8 ping statistics ---
   3 packets transmitted, 3 packets received, 0% packet loss
   round-trip min/avg/max = 5.523/5.634/5.803 ms
   
Regulator
---------

::

   root@j7200-evm:~#find /sys | grep regulator | grep microvolts | xargs cat
   3300000
   3300000
   1800000
   1800000
   850000
   850000
   850000
   900000
   600000
   800000
   1800000
   1800000
   1800000
   850000
   850000
   850000
   800000
   800000
   800000
   1100000
   1100000
   1100000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   800000
   800000
   800000
   850000
   850000
   850000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   3300000
   5000000
   12000000
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#find /sys | grep regulator | grep microvolts | xargs cat
   3300000
   3300000
   1800000
   1800000
   850000
   850000
   850000
   900000
   600000
   800000
   1800000
   1800000
   1800000
   850000
   850000
   850000
   800000
   800000
   800000
   1100000
   1100000
   1100000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   800000
   800000
   800000
   850000
   850000
   850000
   1800000
   1800000
   1800000
   1800000
   1800000
   1800000
   3300000
   5000000
   12000000
   root@j7200-evm:~#

SPI
---

For this test, the spidev_test tool shall be built and added in the rootfs.
And an overlay shall be applied to the devicetree, you can add the following
line in the uEnv.txt file:
::

   name_overlays=k3-j7200-evm-mcspi-loopback.dtbo

Test procedure
::

   root@j7200-evm:~#spidev_test -v -D /dev/spidev2.0  -p slave-hello-to-master &
   [1] 548
   spi mode: 0x0
   bits per word: 8
   max speed: 500000 Hz (500 kHz)
   root@j7200-evm:~#
   root@j7200-evm:~#spidev_test -v -D /dev/spidev1.0 -p master-hello-to-slave
   spi mode: 0x0
   bits per word: 8
   max speed: 500000 Hz (500 kHz)
   TX | 73 6C 61 76 65 2D 68 65 6C 6C 6F 2D 74 6F 2D 6D 61 73 74 65 72 __ __ __ __ __ __ __ __ __ __ __  |slave-hello-to-master|
   RX | 6D 61 73 74 65 72 2D 68 65 6C 6C 6F 2D 74 6F 2D 73 6C 61 76 65 __ __ __ __ __ __ __ __ __ __ __  |master-hello-to-slave|
   TX | 6D 61 73 74 65 72 2D 68 65 6C 6C 6F 2D 74 6F 2D 73 6C 61 76 65 __ __ __ __ __ __ __ __ __ __ __  |master-hello-to-slave|
   RX | 73 6C 61 76 65 2D 68 65 6C 6C 6F 2D 74 6F 2D 6D 61 73 74 65 72 __ __ __ __ __ __ __ __ __ __ __  |slave-hello-to-master|
   [1]+  Done                    spidev_test -v -D /dev/spidev2.0 -p slave-hello-to-master
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#spidev_test -v -D /dev/spidev2.0  -p slave-hello-to-master &
   [1] 565
   spi mode: 0x0
   bits per word: 8
   max speed: 500000 Hz (500 kHz)
   root@j7200-evm:~#
   root@j7200-evm:~#spidev_test -v -D /dev/spidev1.0 -p master-hello-to-slave
   spi mode: 0x0
   bits per word: 8
   max speed: 500000 Hz (500 kHz)
   TX | 73 6C 61 76 65 2D 68 65 6C 6C 6F 2D 74 6F 2D 6D 61 73 74 65 72 __ __ __ __ __ __ __ __ __ __ __  |slave-hello-to-master|
   RX | 6D 61 73 74 65 72 2D 68 65 6C 6C 6F 2D 74 6F 2D 73 6C 61 76 65 __ __ __ __ __ __ __ __ __ __ __  |master-hello-to-slave|
   TX | 6D 61 73 74 65 72 2D 68 65 6C 6C 6F 2D 74 6F 2D 73 6C 61 76 65 __ __ __ __ __ __ __ __ __ __ __  |master-hello-to-slave|
   RX | 73 6C 61 76 65 2D 68 65 6C 6C 6F 2D 74 6F 2D 6D 61 73 74 65 72 __ __ __ __ __ __ __ __ __ __ __  |slave-hello-to-master|
   [1]+  Done                    spidev_test -v -D /dev/spidev2.0 -p slave-hello-to-master
   root@j7200-evm:~#

Thermal
-------

::

   root@j7200-evm:~#cat /sys/class/thermal/thermal_zone*/temp
   46569
   45663
   44525
   root@j7200-evm:~#echo 0 > /proc/sys/kernel/printk
   root@j7200-evm:~#echo 1 > /sys/module/printk/parameters/console_suspend
   root@j7200-evm:~#echo deep > /sys/power/mem_sleep
   root@j7200-evm:~#echo mem > /sys/power/state

   U-Boot SPL 2023.04-gae5b4845 (Feb 06 2024 - 19:03:45 +0100)
   SYSFW ABI: 3.1 (firmware rev 0x0009 '9.1.2--v09.01.02 (Kool Koala)')
   Trying to boot from MMC2
   Starting ATF on ARM64 core...

   I/TC: Secondary CPU 1 initializing
   I/TC: Secondary CPU 1 switching to normal world boot
   root@j7200-evm:~#cat /sys/class/thermal/thermal_zone*/temp
   43611
   43611
   42462
   root@j7200-evm:~#
