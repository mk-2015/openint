#define _DEFAULT_SOURCE
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
