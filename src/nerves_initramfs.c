#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/loop.h>
#include <linux/dm-ioctl.h>

#include "util.h"
#include "linenoise.h"
#include "script.h"
#include "block_device.h"
#include "rootdisk.h"

#define MAX_EXTRA_ENCRYPTED_DEVS 8

// Global U-Boot environment data
struct uboot_env working_uboot_env;

static int losetup(int rootfs_fd, int loop_ix)
{
    char loop_devname[16];
    snprintf(loop_devname, sizeof(loop_devname), "/dev/loop%d", loop_ix);

    // Assume loop0 is available since who else would be able to take it?
    int loop_fd = open(loop_devname, O_RDONLY);
    if (loop_fd < 0)
        fatal("Enable CONFIG_BLK_DEV_LOOP in kernel and check that at least %d loop devices", loop_ix + 1);

    OK_OR_FATAL(ioctl(loop_fd, LOOP_SET_FD, rootfs_fd), "LOOP_SET_FD failed");

    return loop_fd;
}

static int dm_create(int map_ix,
                     const char *name,
                     off_t rootfs_blocks,
                     const char *cipher,
                     const char *secret)
{
    int dm_control = open("/dev/mapper/control", O_RDWR);

    uint8_t request_buffer[16384];
    struct dm_ioctl *dm = (struct dm_ioctl *) request_buffer;

    memset(&request_buffer, 0, sizeof(request_buffer));
    dm->version[0] = DM_VERSION_MAJOR;
    dm->version[1] = DM_VERSION_MINOR;
    dm->version[2] = DM_VERSION_PATCHLEVEL;
    dm->data_size = sizeof(request_buffer);
    dm->data_start = sizeof(struct dm_ioctl);
    dm->target_count = 0;
    dm->open_count = 0;
    dm->flags = 0;
    dm->event_nr = 0;
    dm->dev = 0;
    strcpy(dm->name, name);
    snprintf(dm->uuid, sizeof(dm->uuid), "CRYPT-PLAIN-%s", name);

    OK_OR_FATAL(ioctl(dm_control, DM_DEV_CREATE, request_buffer), "Enable CONFIG_DM_CRYPT in kernel");

    memset(&request_buffer, 0, sizeof(request_buffer));
    dm->version[0] = DM_VERSION_MAJOR;
    dm->version[1] = DM_VERSION_MINOR;
    dm->version[2] = DM_VERSION_PATCHLEVEL;
    dm->data_size = sizeof(request_buffer);
    dm->data_start = sizeof(struct dm_ioctl);
    dm->target_count = 1;
    dm->open_count = 0;
    dm->flags = DM_SECURE_DATA_FLAG;
    dm->dev = 0;
    strcpy(dm->name, name);
    struct dm_target_spec *target = (struct dm_target_spec *) &request_buffer[dm->data_start];
    memset(target, 0, sizeof(struct dm_target_spec));
    target->sector_start = 0;
    target->length = rootfs_blocks;
    strcpy(target->target_type, "crypt");
    sprintf((char *) &request_buffer[dm->data_start + sizeof(struct dm_target_spec)], "%s %s %d /dev/loop%d 0", cipher, secret, map_ix, map_ix);
    OK_OR_FATAL(ioctl(dm_control, DM_TABLE_LOAD, request_buffer), "Check CONFIG_DM_CRYPT and crypto algs enabled");


    memset(&request_buffer, 0, sizeof(request_buffer));
    dm->version[0] = DM_VERSION_MAJOR;
    dm->version[1] = DM_VERSION_MINOR;
    dm->version[2] = DM_VERSION_PATCHLEVEL;
    dm->data_size = sizeof(request_buffer);
    dm->data_start = sizeof(struct dm_ioctl);
    dm->target_count = 0;
    dm->flags = DM_SECURE_DATA_FLAG;
    strcpy(dm->name, name);
    OK_OR_FATAL(ioctl(dm_control, DM_DEV_SUSPEND, request_buffer), "DM_DEV_SUSPEND failed");

    close(dm_control);

    return 0;
}

static const char *root_skip_list[] = {".", "..", "dev", "mnt", "proc", "sys", NULL};
static const char *nonroot_skip_list[] = {".", "..", NULL};

static bool in_skip_list(const char *what, const char **list)
{
    while (*list) {
        if (strcmp(*list, what) == 0)
            return true;
        list++;
    }
    return false;
}

void cleanup_dir(int dirfd, const char **skip_list)
{
    DIR *dir = fdopendir(dirfd);
    for (struct dirent *dt = readdir(dir); dt != NULL; dt = readdir(dir)) {
        // Skip . and ..
        const char *name = dt->d_name;
        if (in_skip_list(name, skip_list))
            continue;

        if (dt->d_type & DT_DIR) {
            int fd = openat(dirfd, name, O_RDONLY);
            if (fd >= 0) {
                cleanup_dir(fd, nonroot_skip_list);
            } else {
                info("openat %s failed", name);
            }
            OK_OR_WARN(unlinkat(dirfd, name, AT_REMOVEDIR), "unlinkat directory  %s", name);
        } else {
            OK_OR_WARN(unlinkat(dirfd, name, 0), "unlinkat %s", name);
        }
    }

    closedir(dir);
}

static void cleanup_old_rootfs()
{
    int fd = open("/", O_RDONLY);
    cleanup_dir(fd, root_skip_list);

    rmdir("/sys");
    rmdir("/proc");
}

static void switch_root()
{
    // Thank you busybox for all of the comments on how this is supposed to work.

    // Move /dev to its new home
    OK_OR_WARN(mount("/dev", "/mnt/dev", NULL, MS_MOVE, NULL), "moving /dev failed");

    // Unmount /sys and /proc so that the next init can mount them
    OK_OR_WARN(umount("/sys"), "unmounting /sys failed");
    OK_OR_WARN(umount("/proc"), "unmounting /proc failed");

    // Clean up the "old" rootfs.
    cleanup_old_rootfs();

    // Change to the new mount
    OK_OR_DEBUG(chdir("/mnt"), "chdir /mnt failed");

    // Make this directory the root one now.
    OK_OR_WARN(mount(".", "/", NULL, MS_MOVE, NULL), "moving / failed");

    // Fix ".."
    OK_OR_DEBUG(chroot("."), "chroot failed");
}

static void setup_initramfs()
{
    // Create necessary directories in case they're not around.
    (void) mkdir("/mnt", 0755);
    (void) mkdir("/dev", 0755);
    (void) mkdir("/sys", 0555);
    (void) mkdir("/proc", 0555);

    // devtmpfs must be enabled in the kernel. It must be manually mounted for the initramfs.
    OK_OR_WARN(mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, "mode=755,size=5%"), "Can't mount /dev");

    // Mount /sys and /proc since so many things depend on them
    OK_OR_WARN(mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL), "Can't mount /sys");
    OK_OR_WARN(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL), "Can't mount /proc");
}

static void mount_fs(const char *rootfs_path, const char *rootfs_type)
{
    // Wait for the rootfs to appear
    int rootfs_fd = open(rootfs_path, O_RDONLY);
    if (rootfs_fd < 0)
        fatal("Can't continue since '%s' does not exist.", rootfs_path);
    close(rootfs_fd);

    OK_OR_FATAL(mount(rootfs_path, "/mnt", rootfs_type, MS_RDONLY, NULL), "Expecting %s filesystem on %s", rootfs_type, rootfs_path);
}

static void mount_encrypted_fs(const char *rootfs_path, const char *rootfs_type, const char *cipher, const char *secret)
{
    // Wait for the rootfs to appear
    int rootfs_fd = open(rootfs_path, O_RDONLY);
    if (rootfs_fd < 0)
        fatal("Can't continue since '%s' does not exist.", rootfs_path);
    off_t rootfs_size = lseek(rootfs_fd, 0, SEEK_END);
    (void) lseek(rootfs_fd, 0, SEEK_SET);

    size_t block_size;
    if (ioctl(rootfs_fd, BLKSSZGET, &block_size) < 0)
        block_size = 512;;

    off_t rootfs_blocks = rootfs_size / block_size;

    int loop_fd = losetup(rootfs_fd, 0);
    close(rootfs_fd);

    dm_create(0, "rootfs", rootfs_blocks, cipher, secret);

    OK_OR_FATAL(mount("/dev/dm-0", "/mnt", rootfs_type, MS_RDONLY, NULL), "Expecting %s filesystem on %s", rootfs_type, rootfs_path);

    // It's ok to close loop_fd now that the mount happened.
    close(loop_fd);
}

static void map_encrypted_device(int map_ix, const char *spec, const char *name, const char *cipher, const char *secret)
{
    // Wait for the device to appear
    char path[BLOCK_DEVICE_PATH_LEN];
    int fd = open_block_device(spec, O_RDONLY, path);
    off_t size = lseek(fd, 0, SEEK_END);
    (void) lseek(fd, 0, SEEK_SET);

    int loop_fd = losetup(fd, map_ix);
    close(fd);

    char dm_name[16];
    snprintf(dm_name, sizeof(dm_name), "/dev/dm-%d", map_ix);

    char link_name[32];
    snprintf(link_name, sizeof(link_name), "/dev/mapper/%s", name);

    dm_create(map_ix, name, size, cipher, secret);

    OK_OR_WARN(symlink(link_name, dm_name), "Could not symlink %s -> %s", link_name, dm_name);

    // Hmmmmm.... It's ok to close loop_fd now that the mount happened.
    close(loop_fd);
}

static void map_extra_encrypted_devs()
{
    for (int ix = 1; ix <= MAX_EXTRA_ENCRYPTED_DEVS; ix++) {
        char base[12];
        snprintf(base, sizeof(base), "dm_crypt.%d", ix);

        char path_var[20];
        char name_var[20];
        char cipher_var[20];
        char secret_var[20];

        snprintf(path_var, sizeof(path_var), "%s.path", base);
        snprintf(name_var, sizeof(name_var), "%s.name", base);
        snprintf(cipher_var, sizeof(cipher_var), "%s.cipher", base);
        snprintf(secret_var, sizeof(secret_var), "%s.secret", base);

        const char *path = get_variable_as_string(path_var);
        const char *name = get_variable_as_string(name_var);
        const char *cipher = get_variable_as_string(cipher_var);
        const char *secret = get_variable_as_string(secret_var);

        if (*path != '\0') {
            if (*name == '\0' ||
                *cipher == '\0' ||
                *secret == '\0') {
                info("Not mapping %s. Make sure that %s.name, %s.cipher, and %s.secret are set",
                        path, base, base, base);
                continue;
            }
            info("mapping other encrypted block device %s", path);
            map_encrypted_device(ix, path, name, cipher, secret);
        }
    }
}

static void repl()
{
    // Reset the terminal colors
    printf("\033[0m");

    extern const struct term *parser_result;
    char *line;
    while((line = linenoise("nerves_initramfs> ")) != NULL) {
        linenoiseHistoryAdd(line);
        eval_string(line); // eval_string owns memory and calls free on "line".
        if (parser_result)
            printf("%s\n", term_to_string(parser_result)->string);
    }
}

static void initialize_script_defaults(int argc, char *argv[])
{
    term_gc_heap();
    uboot_env_init(&working_uboot_env);

    set_string_variable("rootfs.fstype", "squashfs");
    set_string_variable("rootfs.path", "/dev/mmcblk0p2");
    set_boolean_variable("rootfs.encrypted", false);
    set_string_variable("rootfs.cipher", "");
    set_string_variable("rootfs.secret", "");

    set_string_variable("uboot_env.path", "/dev/mmcblk0");
    set_boolean_variable("uboot_env.loaded", false);
    set_boolean_variable("uboot_env.modified", false);
    set_number_variable("uboot_env.start", 256);
    set_number_variable("uboot_env.count", 256);

    set_boolean_variable("run_repl", false);

    // Scan the commandline for more parameters to set. Our instructions tell
    // users to put options for us after a `--`. The `--` isn't passed in the
    // argument list, so we can't search for that. However, Linux won't remove
    // or process anything after the `--` and that is a big deal to avoid
    // confusing clashes with Linux options.
    //
    // Parameters have the form:
    //
    //   key
    // or
    //   key=value
    //
    // In the first case, `key` is set to `true`. For the second
    // case, the value is always interpreted as a string.
    debug("argc=%d, argv[0]='%s'", argc, argv[0]);
    for (int i = 1; i < argc; i++) {
        debug("argv[%d]='%s'", i, argv[i]);

        if (argv[i][0] != '-') {
            char *key = argv[i];
            char *equals = strchr(key, '=');

            if (equals) {
                *equals = '\0';

                set_string_variable(key, equals + 1);
            } else {
                set_boolean_variable(key, true);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    info("version " PROGRAM_VERSION_STR
#ifdef DEBUG
         " [DEBUG]"
#endif
            );

    if (getpid() != 1)
       fatal("Must be pid 1");

    setup_initramfs();

    // Initialize scripting environment
    initialize_script_defaults(argc, argv);

    eval_file("/nerves_initramfs.conf");

    if (get_variable_as_boolean("run_repl"))
        repl();

    // Mount the root filesystem
    const char *rootfs_spec = get_variable_as_string("rootfs.path");
    char resolved_rootfs_path[BLOCK_DEVICE_PATH_LEN];
    if (resolve_block_device_spec(rootfs_spec, resolved_rootfs_path) < 0)
        fatal("Can't continue since '%s' does not exist.", rootfs_spec);

    const char *rootfs_fstype = get_variable_as_string("rootfs.fstype");
    if (get_variable_as_boolean("rootfs.encrypted"))
        mount_encrypted_fs(resolved_rootfs_path,
                           rootfs_fstype,
                           get_variable_as_string("rootfs.cipher"),
                           get_variable_as_string("rootfs.secret"));
    else
        mount_fs(resolved_rootfs_path, rootfs_fstype);

    // Finalize our setup of the root filesystem
    create_rootdisk_symlinks(resolved_rootfs_path);
    map_extra_encrypted_devs();

    // Switch over to the new root filesystem
    switch_root();

    // Launch the real init. It's always /sbin/init with Buildroot.
    execv("/sbin/init", argv);

    fatal("Couldn't run /sbin/init: %s", strerror(errno));
}
