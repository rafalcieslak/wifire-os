#include <sys_mmap.h>
#include <thread.h>
#include <stdc.h>
#include <errno.h>
#include <vm_map.h>
#include <vm_pager.h>
#include <rwlock.h>

int sys_mmap(thread_t *td, syscall_args_t *args) {
  vm_addr_t addr = args->args[0];
  size_t length = args->args[1];
  vm_prot_t prot = args->args[2];
  int flags = args->args[3];

  kprintf("[syscall] mmap(%p, %zu, %d, %d)\n", (void *)addr, length, prot,
          flags);

  int error = 0;
  vm_addr_t result = do_mmap(addr, length, prot, flags, &error);
  if (error < 0)
    return -error;
  return result;
}

vm_addr_t do_mmap(vm_addr_t addr, size_t length, vm_prot_t prot, int flags,
                  int *error) {
  thread_t *td = thread_self();
  vm_map_t *vmap = td->td_uspace;

  assert(vmap);

  if (addr >= vmap->pmap->end) {
    /* mmap cannot callocate memory in kernel space! */
    *error = EINVAL;
    return MMAP_FAILED;
  }

  if (!flags & MMAP_FLAG_ANONYMOUS) {
    log("Non-anonymous memory mappings are not yet implemented.");
    *error = EINVAL;
    return MMAP_FAILED;
  }

  {
    rw_scoped_enter(&vmap->rwlock, RW_WRITER);

    length = roundup(length, PAGESIZE);

    /* Regardless of whether addr is 0 or an address hint, we correct it a
       bit. */
    if (addr < MMAP_LOW_ADDR)
      addr = MMAP_LOW_ADDR;
    addr = roundup(addr, PAGESIZE);

    if (vm_map_findspace_nolock(vmap, addr, length, &addr) != 0) {
      /* No memory was found following the hint. Search again entire address
         space. */
      if (vm_map_findspace_nolock(vmap, MMAP_LOW_ADDR, length, &addr) != 0) {
        /* Still no memory found. */
        *error = ENOMEM;
        return MMAP_FAILED;
      }
    }

    /* Create new vm map entry for this allocation. Temporarily use permissive
     * protection, so that we may optionally initialize the entry. */
    vm_map_entry_t *entry = vm_map_add_entry(vmap, addr, addr + length, prot);

    if (flags & MMAP_FLAG_ANONYMOUS) {
      /* Assign a pager which creates cleared pages . */
      entry->object = default_pager->pgr_alloc();
    }

  } /* Release vm_map rwlock. */

  log("Created entry at %p, length: %zu", (void *)addr, length);

  return addr;
}