#include <mount.h>
#include <stdc.h>
#include <vnode.h>
#include <errno.h>

int main() {
  vnode_t *v;
  int error;
  error = vfs_lookup("/dev/SPAM", &v);
  assert(error == ENOENT);
  error = vfs_lookup("/usr", &v);
  assert(error == ENOTSUP); /* Root filesystem not implemented yet. */
  error = vfs_lookup("/", &v);
  assert(error == 0 && v == vfs_root_vnode);
  vnode_lock_release(v);
  mtx_unlock(&v->v_mtx);
  error = vfs_lookup("/dev////", &v);
  assert(error == 0 && v == vfs_root_dev_vnode);
  vnode_lock_release(v);
  return 0;
}
