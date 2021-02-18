#!/bin/sh

set -e

# Adapted from Buildroot
output=gpt-disk.img

# GPT partition type UUIDs
esp_type=c12a7328-f81f-11d2-ba4b-00a0c93ec93b
linux_type=44479540-f297-41b2-9af7-d131d5f0458a
linux_fs_data_type=0FC63DAF-8483-4772-8E79-3D69D8477DE4

# Hardcode UUIDs for reproducible images (call uuidgen to make more)
disk_uuid=b443fbeb-2c93-481b-88b3-0ecb0aeba911
efi_part_uuid=5278721d-0089-4768-85df-b8f1b97e6684
root_part_uuid=fcc205c8-2f1c-4dcd-bef4-7b209aa15cca
app_part_uuid=7e7b6f06-8aaf-42c6-9c3b-6ede014885a6
provision_part_uuid=14e534d0-7adc-4b8e-a208-460104c3c24a

# Boot partition offset and size, in 512-byte sectors
efi_part_start=64
efi_part_size=64

# Rootfs partition offset and size, in 512-byte sectors
root_part_start=$(( efi_part_start + efi_part_size ))
root_part_size=128

# App partition offset and size, in 512-byte sectors
app_part_start=$(( root_part_start + root_part_size ))
app_part_size=64

# Provisioning partition offset and size, in 512-byte sectors
provision_part_start=$(( app_part_start + app_part_size ))
provision_part_size=64

gpt_size=33

first_lba=$(( 1 + gpt_size ))
last_lba=$(( provision_part_start + provision_part_size ))

# Disk image size in 512-byte sectors
image_size=$(( last_lba + gpt_size + 1 ))

primary_gpt_lba=0
secondary_gpt_lba=$(( image_size - gpt_size ))

rm -f $output
dd if=/dev/zero of=$output bs=512 count=0 seek=$image_size 2>/dev/null

sfdisk $output <<EOF
label: gpt
label-id: $disk_uuid
device: /dev/nothing0
unit: sectors
first-lba: $first_lba
last-lba: $last_lba

/dev/nothing0p1 : start=$efi_part_start,  size=$efi_part_size,  type=$esp_type,   uuid=$efi_part_uuid,  name="efi-part"
/dev/nothing0p2 : start=$root_part_start, size=$root_part_size, type=$linux_type, uuid=$root_part_uuid, name="rootfs"
/dev/nothing0p5 : start=$app_part_start, size=$app_part_size, type=$linux_fs_data_type, uuid=$app_part_uuid, name="app"
/dev/nothing0p6 : start=$provision_part_start, size=$provision_part_size, type=$linux_fs_data_type, uuid=$provision_part_uuid, name="provision"
EOF


