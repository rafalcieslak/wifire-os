#include <devfs.h>
#include <mount.h>
#include <vnode.h>
#include <errno.h>
#include <stdc.h>
#include <mutex.h>
#include <malloc.h>
#include <linker_set.h>

/* Structure for storage in mnt_data */
typedef struct devfs_mount { vnode_t *root_vnode; } devfs_mount_t;

static inline devfs_mount_t *devfs_of(mount_t *m) {
  return (devfs_mount_t *)m->mnt_data;
}

/* Structure for storing installed devices */
typedef struct devfs_device {
  char name[DEVFS_NAME_MAX];
  vnode_t *dev;
  TAILQ_ENTRY(devfs_device) list;
} devfs_device_t;

/* The list of all installed devices */
typedef TAILQ_HEAD(, devfs_device) devfs_device_list_t;
static devfs_device_list_t devfs_device_list =
  TAILQ_HEAD_INITIALIZER(devfs_device_list);
static mtx_t devfs_device_list_mtx = MUTEX_INITIALIZER(MTX_DEF);

static devfs_device_t *devfs_get_by_name(const char *name) {
  SCOPED_MTX_LOCK(&devfs_device_list_mtx);

  devfs_device_t *idev;
  TAILQ_FOREACH (idev, &devfs_device_list, list)
    if (!strcmp(name, idev->name))
      return idev;
  return NULL;
}

int devfs_install(const char *name, vnode_t *device) {
  size_t n = strlen(name);
  if (n >= DEVFS_NAME_MAX)
    return -ENAMETOOLONG;

  if (devfs_get_by_name(name) != NULL)
    return -EEXIST;

  devfs_device_t *idev = kmalloc(M_VFS, sizeof(devfs_device_t), M_ZERO);
  strlcpy(idev->name, name, DEVFS_NAME_MAX);
  idev->dev = device;

  WITH_MTX_LOCK (&devfs_device_list_mtx)
    TAILQ_INSERT_TAIL(&devfs_device_list, idev, list);

  return 0;
}

static vnode_lookup_t devfs_root_lookup;

static vnodeops_t devfs_root_ops = {.v_lookup = devfs_root_lookup,
                                    .v_readdir = vnode_readdir_nop,
                                    .v_open = vnode_open_nop,
                                    .v_close = vnode_close_nop,
                                    .v_read = vnode_read_nop,
                                    .v_write = vnode_write_nop,
                                    .v_seek = vnode_seek_nop,
                                    .v_getattr = vnode_getattr_nop};

static int devfs_mount(mount_t *m) {
  /* Prepare the root vnode. We'll use a single instead of allocating a new
     vnode each time, because this will suffice for now, and simplifies things a
     lot. */
  vnode_t *root = vnode_new(V_DIR, &devfs_root_ops);
  root->v_mount = m;

  devfs_mount_t *dfm = kmalloc(M_VFS, sizeof(devfs_mount_t), M_ZERO);
  dfm->root_vnode = root;
  m->mnt_data = dfm;

  return 0;
}

static int devfs_root_lookup(vnode_t *dir, const char *name, vnode_t **res) {
  assert(dir == devfs_of(dir->v_mount)->root_vnode);

  devfs_device_t *idev = devfs_get_by_name(name);
  if (!idev)
    return -ENOENT;

  *res = idev->dev;
  vnode_ref(*res);

  return 0;
}

static int devfs_root(mount_t *m, vnode_t **v) {
  *v = devfs_of(m)->root_vnode;
  vnode_ref(*v);
  return 0;
}

static int devfs_init(vfsconf_t *vfc) {
  /* Prepare some initial devices */
  typedef void devfs_init_func_t(void);
  SET_DECLARE(devfs_init, devfs_init_func_t);
  devfs_init_func_t **ptr;
  SET_FOREACH(ptr, devfs_init) {
    (**ptr)();
  }
  return 0;
}

static vfsops_t devfs_vfsops = {
  .vfs_mount = devfs_mount, .vfs_root = devfs_root, .vfs_init = devfs_init};

static vfsconf_t devfs_conf = {.vfc_name = "devfs",
                               .vfc_vfsops = &devfs_vfsops};

SET_ENTRY(vfsconf, devfs_conf);
