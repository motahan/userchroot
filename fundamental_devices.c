#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _USE_MOUNT_LOFS_INSTEAD_OF_MKNOD
#include <sys/mntent.h>
#include <sys/mount.h>
#endif

#ifdef __linux__
#include <sys/mount.h>
#endif

#include "userchroot.h"
#include "fundamental_devices.h"

static void create_fundamental_device(const char* chroot_path,
                                     const char* device_path) {
  int rc;
  struct stat realdev;
  struct stat chrtdev;

  int name_size = strlen(chroot_path) + strlen(device_path) + 1;
  char* final_path = malloc(name_size);
  if (final_path == NULL) {
    fprintf(stderr,"Failed to allocate memory. Aborting.\n");
    exit(ERR_EXIT_CODE);
  }
  rc = snprintf(final_path, name_size, "%s%s", chroot_path, device_path);
  if (rc < 0) {
    fprintf(stderr,"Failed to produce full path for device. Aborting.\n");
    exit(ERR_EXIT_CODE);
  }
  rc = stat(final_path, &chrtdev);
  if (!rc) {
    fprintf(stderr,"%s already exists. Aborting.\n", final_path);
    exit(ERR_EXIT_CODE);
  }
  rc = stat(device_path, &realdev);
  if (rc) {
    fprintf(stderr,"Failed to stat %s. Aborting.\n", device_path);
    exit(ERR_EXIT_CODE);
  }
#ifdef _USE_MOUNT_LOFS_INSTEAD_OF_MKNOD
  char  mount_optbuf[MAX_MNTOPT_STR] = { '\0', };

  rc = mkdir(final_path, 0755);
  if (rc) {
    fprintf(stderr,"Failed to mkdir %s to mount. Aborting.\n", final_path);
  }
  rc = mount(device_path, final_path, MS_DATA|MS_OPTIONSTR,
             MNTTYPE_LOFS, NULL, 0, mount_optbuf, MAX_MNTOPT_STR);
  if (rc) {
    fprintf(stderr,"Failed to lofs mount %s.", final_path);
    exit(ERR_EXIT_CODE);
  }
#else
  rc = mknod(final_path, realdev.st_mode, realdev.st_rdev);
  if (rc) {
    fprintf(stderr,"Failed to create the device for %s.", final_path);
    exit(ERR_EXIT_CODE);
  }
#endif // _USE_MOUNT_LOFS_INSTEAD_OF_MKNOD

  free(final_path);
}

static void unlink_fundamental_device(const char* chroot_path,
                                     const char* device_path) {
  int rc;
  int name_size = strlen(chroot_path) + strlen(device_path) + 1;
  char* final_path = malloc(name_size);
  if (final_path == NULL) {
    fprintf(stderr,"Failed to allocate memory. Aborting.\n");
    exit(ERR_EXIT_CODE);
  }
  rc = snprintf(final_path, name_size, "%s%s", chroot_path, device_path);
  if (rc < 0) {
    fprintf(stderr,"Failed to produce full path for device. Aborting.\n");
    exit(ERR_EXIT_CODE);
  }
#ifdef _USE_MOUNT_LOFS_INSTEAD_OF_MKNOD
  rc = umount(final_path);
  if (rc) {
    fprintf(stderr,"Failed to umount %s. Aborting.\n", final_path);
    exit(ERR_EXIT_CODE);
  }
  rc = rmdir(final_path);
  if (rc) {
    fprintf(stderr,"Failed to rmdir %s. Aborting.\n", final_path);
    exit(ERR_EXIT_CODE);
  }
  rc = rmdir(final_path);
#else
  rc = unlink(final_path);
  if (rc) {
    fprintf(stderr,"Failed to unlink %s. Aborting.\n", final_path);
    exit(ERR_EXIT_CODE);
  }
#endif // _USE_MOUNT_LOFS_INSTEAD_OF_MKNOD
  free(final_path);
}

int create_fundamental_devices(const char* chroot_path) {
  // we need to let the devices be created with the appropriate
  // modes. However, since the file will be group-owned by
  // the user creating the device, we make sure the umask prevent
  // the user from having any permission granted just by the group.
  mode_t original_mask = umask(0070);
  create_fundamental_device(chroot_path,"/dev/null");
  create_fundamental_device(chroot_path,"/dev/zero");
  create_fundamental_device(chroot_path,"/dev/random");
  create_fundamental_device(chroot_path,"/dev/urandom");

  // add a mount for /dev/shm for linux only
#ifdef __linux__
    char *fullpath = (char *)
        malloc(strlen(chroot_path) + strlen("/dev/shm") + 1);
    sprintf(fullpath, "%s/dev/shm", chroot_path);

    struct stat statbuf;
    mode_t perms = (0777 | S_ISVTX);

    // clean up from a previous run and set up for this one
    umount2(fullpath, MNT_FORCE);
    rmdir(fullpath);
    mkdir(fullpath, perms);
    if (chown(fullpath, 0, 0) < 0)
    {
        fprintf(stderr, "Could not chown %s to root.  Aborting.\n", fullpath);
        exit(ERR_EXIT_CODE);
    }
    if (chmod(fullpath, perms) < 0)
    {
        fprintf(stderr, "Could not chmod %s to 777+sticky.  Aborting.\n",
            fullpath);
        exit(ERR_EXIT_CODE);
    }
    if (stat(fullpath, &statbuf) < 0)
    {
        fprintf(stderr, "Could not stat %s.  Aborting.\n", fullpath);
        exit(ERR_EXIT_CODE);
    }
    if (!S_ISDIR(statbuf.st_mode))
    {
        fprintf(stderr, "%s not a directory.  Aborting.\n", fullpath);
        exit(ERR_EXIT_CODE);
    }
    if ((statbuf.st_mode & perms) != perms)
    {
        fprintf(stderr, "Wrong perms on %s.  Aborting.\n", fullpath);
        exit(ERR_EXIT_CODE);
    }
    if (mount("tmpfs", fullpath, "tmpfs", MS_MGC_VAL, "size=128m") < 0)
    {
        fprintf(stderr, "Could not mount %s.  Aborting.\n", fullpath);
        exit(ERR_EXIT_CODE);
    }
    free(fullpath);
#endif
  umask(original_mask);
  return 0;
}

int unlink_fundamental_devices(const char* chroot_path) {
  unlink_fundamental_device(chroot_path,"/dev/null");
  unlink_fundamental_device(chroot_path,"/dev/zero");
  unlink_fundamental_device(chroot_path,"/dev/random");
  unlink_fundamental_device(chroot_path,"/dev/urandom");
  // unmount /dev/shm for linux only
#ifdef __linux__
    char *fullpath = (char *)
        malloc(strlen(chroot_path) + strlen("/dev/shm") + 1);
    sprintf(fullpath, "%s/dev/shm", chroot_path);
    if (umount2(fullpath, MNT_FORCE) < 0)
    {
        fprintf(stderr, "Could not unmount %s (%s).  Aborting.\n",
            fullpath, strerror(errno));
        exit(ERR_EXIT_CODE);
    }
    if (rmdir(fullpath) < 0)
    {
        fprintf(stderr, "Could not rmdir %s (%s).  Aborting.\n",
            fullpath, strerror(errno));
        exit(ERR_EXIT_CODE);
    }
    free(fullpath);
#endif
  return 0;
}

// ----------------------------------------------------------------------------
// Copyright 2015 Bloomberg Finance L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------- END-OF-FILE ----------------------------------
