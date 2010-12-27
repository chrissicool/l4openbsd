/*
 * Process handling functions.
 */

#ifndef __L4__PROCESS_H__
#define __L4__PROCESS_H__

#include <sys/types.h>
#include <uvm/uvm_extern.h>

paddr_t		*l4x_run_uvm_fault(vm_map_t map, vaddr_t uva, vm_prot_t access_type);

#endif /* __L4__PROCESS_H__ */
