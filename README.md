# nerves_initramfs

[![CircleCI](https://circleci.com/gh/nerves-project/nerves_initramfs.svg?style=svg)](https://circleci.com/gh/nerves-project/nerves_initramfs)

This project creates a tiny
[initramfs](https://en.wikipedia.org/wiki/Initial_ramdisk) that supports
cross-platform failure recovery and non-trivial root filesystem setups on
devices using Nerves.

The way it works is this:

1. The device's bootloader (U-Boot, Barebox, RPi, grub, etc.) boots the
   platform-specific Linux kernel and the initramfs from this project
2. The `init` process provided by this project handles any recovery tasks, and
   then mounts the real root filesystem, and runs the real `init` process
   (`erlinit` for Nerves)

This project is focused on recovery and root filesystem mounts. No
other functionality is likely to be added to this project to keep it small,
fast, and easy to audit.

Failure recovery at this level means that if a Nerves device fails
catastrophically after the Linux kernel starts, code here can revert devices to
previous firmware or start recovery mechanisms. This project does not protect
against Linux kernel initialization crashes. This is a tradeoff to avoid
maintaining an implementation for every bootloader where this logic is time
consuming to verify. You can, of course, transition away from this code as
needed.

Root filesystem mount functionality is handled here as well since the initramfs
is typically where this feature lives for when the root filesystem is encrypted.
Additionally, if the root filesystem fails to decrypt, the recovery mechanisms
described previously can be triggered.

## Boot configuration

`nerves_initramfs` is configured via rules in a file called
`/nerves_initramfs.conf` in the initramfs. Be aware that `nerves_initramfs` will
delete that file when it cleans up before switching to the true root filesystem.

The `nerves_initramfs.conf` file uses a simple language for specifying rules and
setting variables. Rules are composed of a condition and a list of actions .
Here's an example:

```config
!fw_validated && fw_booted -> { fwup_revert(); reboot(); }
```

Real configuration files contain rules to handle fallback logic or set up root
filesystem mounts that Linux wouldn't be able to do without help.

### Variables

Variables can be defined and used as needed like other languages. The following
variables have special uses:

Variable           | Description
-------------------|-------------
rootfs.fstype      | Root filesystem time. Defaults to "squashfs"
rootfs.path        | Root filesystem path or spec. Defaults to "/dev/mmcblk0p2"
rootfs.encrypted   | True if the filesystem is encrypted. Defaults to `false`
rootfs.cipher      | The cipher used to encrypt the filesystem. For example, "aes-cbc-plain"
rootfs.secret      | The secret key as hex digits
uboot_env.path     | The location for U-Boot environment data. Defaults to "/dev/mmcblk0"
uboot_env.loaded   | True if the U-Boot environment block has been loaded.
uboot_env.modified | True if something has modified the U-Boot block and it differs from what's on disk
uboot_env.start    | The block offset of the U-Boot environment. (512 byte blocks)
uboot_env.count    | The number of blocks in the environment. Defaults to 256.
run_repl           | True to run a REPL before booting. This is useful for debug. Defaults to `false`
dm_crypt.n.path    | The extra encrypted filesystem path.Where `n` is the index of the extra filesystem.
dm_crypt.n.cipher  | The cipher used to encrypt the extra filesystem. Where `n` is the index of the extra filesystem.
dm_crypt.n.secret  | The secret key as hex digits for the extra filesystem. Where `n` is the index of the extra filesystem.

_for more information about configuring extra encrypted filesystems see [Mounting extra encrypted filesystems](#mounting-extra-encrypted-file-systems)_

Variables can be overridden using the Linux commandline. See your platform's
bootloader documentation for how to pass options to Linux. At the end of the
commandline, add a `--` and then add `variable` and `variable=value` strings.
The `--` will stop the Linux kernel from processing parameters so make sure that
everything intended for Linux is on the left side and everything for
`nerves_initramfs` is on the right side.

For example, the following Linux commandline sets the Linux console and then
passes a root filesystem path to `nerves_initramfs` and starts a repl.

```text
console=tty1 -- rootfs.path=/dev/sda2 run_repl
```

### Functions

It's also possible to call built-in functions:

Function           | Description
-------------------|-------------
blkid()            | Print out information about all block devices
cmd()              | Run an external program. The first argument is the path to the program, the next is the first argument, and so on.
env()              | Print out all loaded U-Boot variables
fwup_revert()      | Run fwup with the appropriate parameters to revert to the previous firmware. Reboots on success.
getenv(key)        | Get the value of a U-Boot variable
help()             | Print out help when running in the REPL
print(...)         | Print one or more strings and variables
loadenv()          | Load a U-Boot environment block. Set up `uboot_env.path`, `uboot_env.start` and `uboot_env.count` first.
ls()               | List files a directory
poweroff()         | Power off the device
readfile(path)     | Read a file (truncates long files)
reboot()           | Reset the device
saveenv()          | Save all U-Boot variables back to storage
setenv(key, value) | Set a U-Boot variable. It is not saved until you call `saveenv()`
sleep(timeout)     | Wait for the specified milliseconds
vars()             | Print out all known variables and their values

### Block device specifications

Block devices, such as `/dev/sda2`, are either specified as absolute paths or
using the Linux match syntax. Currently only `PARTUUID` is supported for
identifying a partition by its UUID. Use the `blkid` function or the `blkid`
commandline utility in Linux to list information about block devices.

If you are only using one storage device, using absolute paths to block devices
is fine. If you have more than one storage device, Linux sometimes can enumerate
them in a different order so `/dev/sda` could be `/dev/sdb` sometimes. The way
around this is to identify devices by UUID.

## Building

Users should prefer to use pre-built releases. To build your own, you will need
to use Linux and install the [Buildroot required
packages](https://buildroot.org/downloads/manual/manual.html#requirement).

Change to the `builder` directory and run `./build-all.sh` to build for all
platforms.

If you're only building for one platform, run the following:

```sh
./create-build.sh configs/nerves_initramfs_arm_defconfig o/arm
cd o/arm
make
```

The `nerves_initramfs` images will be in the `images` directory. You can also run `make
menuconfig` to enable other applications and libraries that may be useful for
your particular setup.

## Linux kernel configuration

The following strings must be in your kernel configuration:

* `CONFIG_BLK_DEV_RAM=y`
* `CONFIG_BLK_DEV_INITRD=y`
* `CONFIG_RD_<compression>=y` - gzip compression is enabled by default, but if
  you want lz4, for example, you'll need to enable it.

To mount encrypted filesystems, you'll need these additional configuration strings:

* `CONFIG_BLK_DEV_LOOP=y`
* `CONFIG_MD=y`
* `CONFIG_BLK_DEV_DM=y`
* `CONFIG_DM_CRYPT=y` - Only `dm-crypt` is supported. `cryptoloop` and
  `loop-aes` are not supported.
* `CONFIG_CRYPTO_USER=y`
* `CONFIG_CRYPTO_USER_API_HASH=y`
* `CONFIG_CRYPTO_USER_API_SKCIPHER=y`
* `CONFIG_CRYPTO_AES=y` - Make sure to enable the cryptographic algorithms that
  you're using. Hardware acceleration options may exist as well.

## Preparing files for boot

`initramfs` requires a single file in `cpio` format. If you have multiple files, they
need to be converted to `cpio` format and concatenated together with the build.

The compressed build files are already in cpio format, but you will need to convert
your `nerves_initramfs.confg` and any other files (such as `revert.fw`) then
concatenate them together before using on boot. The helper script `file-to-cpio.sh`
was created for this and is included with the release so it can be used as needed.

Your implementation may differ, but a simple usage example is:

```sh
$ file-to-cpio.sh nerves_initramfs.conf nerves_initramfs.conf.cpio
$ file-to-cpio.sh revert.fw revert.fw.cpio

# Then concatenate files together
# The build .gz/.xz is already cpio format
$ cat nerves_initramfs_arm.gz nerves_initramfs.conf.cpio revert.fw.cpio > nerves_initramfs
$ cp nerves_initramfs /path/to/your/boot/partition
```

## Raspberry Pi configuration

The Raspberry Pi's bootloader supports loading `initramfs` images off the boot
partition that contains the Linux kernel. Since gzip compression is pretty much
always enabled in Linux kernels, the `nerves_initramfs_arm.gz` is safe to use.

First, follow the steps in [**Preparing files for boot**](#preparing-files-for-boot),
copy the `nerves_initramfs` file to the boot partition (in Nerves, you can do this
manually or edit the `fwup.conf` to automate prepping and copying the files),
then add this line to `config.txt`:

```config
initramfs nerves_initramfs followkernel
```

See the [official config.txt boot
documentation](https://www.raspberrypi.org/documentation/configuration/config-txt/boot.md)
for more information.

## U-boot configuration

## Grub configuration

## Mounting an encrypted file system

This project mounts encrypted file systems by using the Linux kernel's
`dm-crypt` module. As such, the web contains a lot of information on this
feature. The main difference, though, is that devices using this project are
expected to be unattended. In other words, a human can not enter a password when
the device boots. This ends up simplifying the options that can be used, but the
handling of the secret can become a major issue.

Storing the secret securely depends on your hardware and your needs. It is
expected that some projects will need to fork this repository to access their
secrets. The examples shown here are meant to be informative on the process, but
they are not secure. They are probably more accurately referred to as ways to
obfuscate the root filesystem.

Here's the general idea:

1. Create your root filesystem as normal
2. Write the filesystem to the destination media. If using a MicroSD card, this
   could be done by mounting it on a Linux PC using `dm-crypt` and writing the
   root filesystem via that. `fwup` also supports encryption.
3. Make sure that your device's Linux kernel supports the chosen encryption
   algorithms
4. Configure this project with how to find or generate the secret key

Here's an example configuration file with the secret key stored as plain text:

```config
rootfs.encrypted = true
rootfs.cipher = "aes-cbc-plain"
rootfs.secret = "8e9c0780fd7f5d00c18a30812fe960cfce71f6074dd9cded6aab2897568cc856"
```

This is illustrative, but obviously quite insecure. The current route to
obtaining the secret key is to edit the C code to this project to integrate it
with platform-specific way of keeping or hiding secrets. It is hoped that
alternatives can be shared in the future.

### Mounting extra encrypted file systems

If you want to mount more encrypted file systems outside of the `rootfs` you
can use the `dm_crypt` variable to configure the extra filesystems. The
`dm_crypt` variable works using a number to under the `dm_crypt` variable
namespace like so: `dm_crypt.n.path`.

Here's an example configuration file with configuration two more filesystems:

```config
dm_crypt.1.path = "/dev/mmcblk0p3"
dm_crypt.1.cipher = "aes-cbc-plain"
dm_crypt.1.secret = "8e9c0780fd7f5d00c18a30812fe960cfce71f6074dd9cded6aab2897568cc856"

dm_crypt.1.path = "/dev/mmcblk0p4"
dm_crypt.2.cipher = "aes-cbc-plain"
dm_crypt.2.secret = "4e9c781fd7f5d00c18a30812fe970cfce56f6064dd9cded6aab2897575cc861"
```
