#define _DEFAULT_SOURCE
#include <sys/swap.h>

#include <init/log.h>
#include <init/macros.h>
#include <init/system.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct {
  const char *source;
  const char *target;
  const char *filesystemtype;
  unsigned long mountflags;
  const void *data;
} MountPoint;

static const MountPoint early_mounts[] = {
  {
    .source = "devtmpfs",
    .target = "/dev",
    .filesystemtype = "devtmpfs",
    .mountflags = MS_NOSUID,
    .data = "mode=0755"
  },
  {
    .source = "proc",
    .target = "/proc",
    .filesystemtype = "proc",
    .mountflags = MS_NOSUID | MS_NOEXEC | MS_NODEV,
    .data = NULL
  },
  {
    .source = "sysfs",
    .target = "/sys",
    .filesystemtype = "sysfs",
    .mountflags = MS_NOSUID | MS_NOEXEC | MS_NODEV,
    .data = NULL
  },
  {
    .source = "tmpfs",
    .target = "/run",
    .filesystemtype = "tmpfs",
    .mountflags = MS_NOSUID | MS_NODEV,
    .data = "mode=0755"
  }
};

static unsigned long parse_mount_options(char *opts, char *extra_data, size_t extra_size)
{
  unsigned long flags = 0;
  extra_data[0] = '\0';

  if (opts == NULL || strcmp(opts, "defaults") == 0)
    return 0;

  char *token = strtok(opts, ",");
  while (token != NULL)
  {
    if (strcmp(token, "ro") == 0)
      flags |= MS_RDONLY;
    else if (strcmp(token, "rw") == 0)
      flags &= ~MS_RDONLY;
    else if (strcmp(token, "nosuid") == 0)
      flags |= MS_NOSUID;
    else if (strcmp(token, "nodev") == 0)
      flags |= MS_NODEV;
    else if (strcmp(token, "noexec") == 0)
      flags |= MS_NOEXEC;
    else if (strcmp(token, "sync") == 0)
      flags |= MS_SYNCHRONOUS;
    else if (strcmp(token, "remount") == 0)
      flags |= MS_REMOUNT;
    else if (strcmp(token, "noatime") == 0)
      flags |= MS_NOATIME;
    else if (strcmp(token, "nodiratime") == 0)
      flags |= MS_NODIRATIME;
    else if (strcmp(token, "relatime") == 0)
      flags |= MS_RELATIME;
    else if (strcmp(token, "bind") == 0)
      flags |= MS_BIND;
    else if (strcmp(token, "rbind") == 0)
      flags |= (MS_BIND | MS_REC);
    else if (strcmp(token, "defaults") != 0)
    {
      if (extra_data[0] != '\0')
        strncat(extra_data, ",", extra_size - strlen(extra_data) - 1);
      strncat(extra_data, token, extra_size - strlen(extra_data) - 1);
    }

    token = strtok(NULL, ",");
  }

  return flags;
}

static int do_mount(const MountPoint *mp)
{
  if (mkdir(mp->target, 0755) == -1 && errno != EEXIST)
  {
    log_message(LOG_ERROR, "Failed to create mount directory %s: %s",
                mp->target, strerror(errno));
    return -1;
  }

  errno = 0;

  if (mount(mp->source, mp->target, mp->filesystemtype, mp->mountflags, mp->data) == -1)
  {
    if (errno == EBUSY || errno == EEXIST)
    {
      log_message(LOG_DEBUG, "Already mounted: %s", mp->target);
      return 0;
    }

    log_message(LOG_ERROR, "Failed to mount %s on %s (%s): %s",
                mp->source, mp->target, mp->filesystemtype, strerror(errno));
    return -1;
  }

  log_message(LOG_INFO, "Mounted %s on %s (%s)", mp->source, mp->target, mp->filesystemtype);
  return 0;
}

void mount_fstab_system(void)
{
  FILE *fp = setmntent("/etc/fstab", "r");
  if (fp == NULL)
  {
    log_message(LOG_WARN, "Could not open /etc/fstab: %s", strerror(errno));
    return;
  }

  struct mntent *ent;
  while ((ent = getmntent(fp)) != NULL)
  {
    if (strcmp(ent->mnt_type, "swap") == 0 || strcmp(ent->mnt_dir, "none") == 0)
      continue;

    char extra_data[256];
    char opts_copy[256];
    snprintf(opts_copy, sizeof(opts_copy), "%s", ent->mnt_opts);

    unsigned long flags = parse_mount_options(opts_copy, extra_data, sizeof(extra_data));

    if (strcmp(ent->mnt_dir, "/") == 0)
      flags |= MS_REMOUNT;

    MountPoint mp = {
      .source = ent->mnt_fsname,
      .target = ent->mnt_dir,
      .filesystemtype = ent->mnt_type,
      .mountflags = flags,
      .data = (extra_data[0] != '\0') ? extra_data : NULL
    };

    do_mount(&mp);
  }

  endmntent(fp);
}

void mount_system(void)
{
  size_t count = sizeof(early_mounts) / sizeof(early_mounts[0]);
  for (size_t i = 0; i < count; i++)
  {
    do_mount(&early_mounts[i]);
  }

  mkdir("/dev/pts", 0755);
  mkdir("/dev/shm", 1777);

  mount_fstab_system();
}

#define MAX_UNMOUNT_ENTRIES 128

static void unescape_proc_path(char *str)
{
  char *src = str;
  char *dst = str;
  while (*src)
  {
    if (src[0] == '\\' &&
        src[1] >= '0' && src[1] <= '7' &&
        src[2] >= '0' && src[2] <= '7' &&
        src[3] >= '0' && src[3] <= '7')
    {
      *dst++ = (char)(((src[1] - '0') << 6) |
                      ((src[2] - '0') << 3) |
                       (src[3] - '0'));
      src += 4;
    }
    else
    {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static void disable_swaps(void)
{
  int fd = open("/proc/swaps", O_RDONLY);
  if (fd == -1)
    return;

  char buf[4096];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return;
  buf[n] = '\0';

  char *line = buf;
  int line_num = 0;
  while (line && *line)
  {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';

    /* Skip header line ("Filename Type Size Used Priority") */
    if (line_num++ > 0 && *line != '\0')
    {
      char *space = strpbrk(line, " \t");
      if (space)
        *space = '\0';

      unescape_proc_path(line);
      if (swapoff(line) == 0)
        log_message(LOG_INFO, "Disabled swap %s", line);
      else
        log_message(LOG_WARN, "Failed to disable swap %s: %s", line, strerror(errno));
    }

    line = next;
  }
}

static int is_virtual_fs(const char *target, const char *type)
{
  if (strcmp(target, "/proc") == 0 ||
      strcmp(target, "/sys") == 0 ||
      strcmp(target, "/dev") == 0 ||
      strcmp(target, "/run") == 0)
    return 1;

  if (strcmp(type, "proc") == 0 ||
      strcmp(type, "sysfs") == 0 ||
      strcmp(type, "devtmpfs") == 0 ||
      strcmp(type, "devpts") == 0 ||
      strcmp(type, "tmpfs") == 0 ||
      strcmp(type, "cgroup") == 0 ||
      strcmp(type, "cgroup2") == 0 ||
      strcmp(type, "securityfs") == 0 ||
      strcmp(type, "debugfs") == 0 ||
      strcmp(type, "tracefs") == 0 ||
      strcmp(type, "pstore") == 0 ||
      strcmp(type, "efivarfs") == 0)
    return 1;

  return 0;
}

void unmount_system(void)
{
  disable_swaps();

  int fd = open("/proc/mounts", O_RDONLY);
  if (fd == -1)
  {
    log_message(LOG_WARN, "Could not open /proc/mounts for unmounting: %s", strerror(errno));
    mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
    return;
  }

  char buf[8192];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n <= 0)
  {
    mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL);
    return;
  }
  buf[n] = '\0';

  char *targets[MAX_UNMOUNT_ENTRIES];
  char *types[MAX_UNMOUNT_ENTRIES];
  size_t count = 0;

  char *line = buf;
  while (line && *line && count < MAX_UNMOUNT_ENTRIES)
  {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';

    /* Format: <source> <target> <fstype> <options> <dump> <pass> */
    char *sp1 = strchr(line, ' ');
    if (sp1)
    {
      char *target = sp1 + 1;
      char *sp2 = strchr(target, ' ');
      if (sp2)
      {
        *sp2 = '\0';
        char *fstype = sp2 + 1;
        char *sp3 = strchr(fstype, ' ');
        if (sp3)
          *sp3 = '\0';

        unescape_proc_path(target);
        targets[count] = target;
        types[count] = fstype;
        count++;
      }
    }

    line = next;
  }

  /* 1. Unmount real disk filesystems in reverse order (deepest submounts first) */
  for (size_t i = count; i > 0; i--)
  {
    size_t idx = i - 1;
    const char *target = targets[idx];
    const char *type = types[idx];

    if (strcmp(target, "/") == 0 || is_virtual_fs(target, type))
      continue;

    if (umount(target) == 0)
    {
      log_message(LOG_INFO, "Unmounted %s", target);
    }
    else if (errno == EBUSY)
    {
      log_message(LOG_WARN, "Target %s busy, forcing lazy unmount", target);
      if (umount2(target, MNT_DETACH) == -1)
        log_message(LOG_ERROR, "Failed to detach %s: %s", target, strerror(errno));
    }
    else
    {
      log_message(LOG_ERROR, "Failed to unmount %s: %s", target, strerror(errno));
    }
  }

  /* 2. Unmount early_mounts in reverse order (/run -> /sys -> /proc -> /dev) */
  size_t early_count = sizeof(early_mounts) / sizeof(early_mounts[0]);
  for (size_t i = early_count; i > 0; i--)
  {
    const char *target = early_mounts[i - 1].target;
    if (umount(target) == -1 && errno == EBUSY)
      umount2(target, MNT_DETACH);
  }

  /* 3. Remount / read-only to flush metadata cleanly */
  if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) == -1)
    log_message(LOG_ERROR, "Failed to remount / read-only: %s", strerror(errno));
  else
    log_message(LOG_INFO, "Remounted / read-only");
}
