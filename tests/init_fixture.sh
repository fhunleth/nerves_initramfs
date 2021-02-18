#!/bin/bash

# Required variables
#
# $WORK
# $TEST_ROOTFS
# $INIT
# $TESTS_DIR

mkdir -p "$TEST_ROOTFS/dev"
ln -s "$INIT" "$TEST_ROOTFS/init"
mkdir -p "$TEST_ROOTFS/usr/bin"
ln -s "$TESTS_DIR/fake_fwup" "$TEST_ROOTFS/usr/bin/fwup"
ln -s "$TESTS_DIR/fake_boardid" "$TEST_ROOTFS/usr/bin/boardid"
ln -s "$TESTS_DIR/fake_faulty_program" "$TEST_ROOTFS/usr/bin/faulty_program"

# Create "block devices" to simulate our filesystems
dd if=/dev/zero of="$TEST_ROOTFS/dev/mmcblk0p1" bs=512 count=0 seek=512 2>/dev/null
dd if=/dev/zero of="$TEST_ROOTFS/dev/mmcblk0p2" bs=512 count=0 seek=1024 2>/dev/null
dd if=/dev/zero of="$TEST_ROOTFS/dev/mmcblk0p5" bs=512 count=0 seek=640 2>/dev/null
dd if=/dev/zero of="$TEST_ROOTFS/dev/mmcblk0p6" bs=512 count=0 seek=768 2>/dev/null

ln -s /dev/null "$TEST_ROOTFS/dev/null"

# Fake active console
real_tty="$(tty)"
ln -s "$real_tty" "$TEST_ROOTFS/dev/ttyF1"
ln -s "$real_tty" "$TEST_ROOTFS/dev/ttyAMA0"
ln -s "$real_tty" "$TEST_ROOTFS/dev/tty1"

# Loop and dm-crypt
mkdir -p "$TEST_ROOTFS/dev/mapper"
touch "$TEST_ROOTFS/dev/mapper/control"
touch "$TEST_ROOTFS/dev/loop0"
touch "$TEST_ROOTFS/dev/loop1"
touch "$TEST_ROOTFS/dev/loop2"
touch "$TEST_ROOTFS/dev/loop3"

# "Block" devices
ln -s "$TESTS_DIR/gpt-disk.img" "$TEST_ROOTFS/dev/mmcblk0"
ln -s "$TESTS_DIR/mbr-disk.img" "$TEST_ROOTFS/dev/sda"
touch "$TEST_ROOTFS/dev/sda1"
touch "$TEST_ROOTFS/dev/sda2"

mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p1"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p2"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p5"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p6"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/queue"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/slaves"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/mq"
mkdir -p "$TEST_ROOTFS/sys/block/mmcblk0/holders"
mkdir -p "$TEST_ROOTFS/sys/block/sda/sda1"
mkdir -p "$TEST_ROOTFS/sys/block/sda/sda2"
echo "179:0" > "$TEST_ROOTFS/sys/block/mmcblk0/dev"
echo "179:1" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p1/dev"
echo "1" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p1/partition"
echo "179:2" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p2/dev"
echo "2" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p2/partition"
echo "179:5" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p5/dev"
echo "5" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p5/partition"
echo "179:6" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p6/dev"
echo "6" > "$TEST_ROOTFS/sys/block/mmcblk0/mmcblk0p6/partition"
echo "8:0" > "$TEST_ROOTFS/sys/block/sda/dev"
echo "8:1" > "$TEST_ROOTFS/sys/block/sda/sda1/dev"
echo "1" > "$TEST_ROOTFS/sys/block/sda/sda1/partition"
echo "8:2" > "$TEST_ROOTFS/sys/block/sda/sda2/dev"
echo "2" > "$TEST_ROOTFS/sys/block/sda/sda2/partition"

# The next init
mkdir -p "$TEST_ROOTFS/mnt/sbin"
ln -s "$TESTS_DIR/fake_init" "$TEST_ROOTFS/mnt/sbin/init"
