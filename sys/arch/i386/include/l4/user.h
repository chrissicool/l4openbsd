#ifndef __INCLUDE__ASM_L4__GENERIC__USER_H__
#define __INCLUDE__ASM_L4__GENERIC__USER_H__

#include <machine/cpu.h>

#include <machine/l4/linux_compat.h>

#include <l4/sys/types.h>
#include <l4/sys/consts.h>
#include <l4/sys/utcb.h>

/* Capability layout in a user process */
enum {
  L4LX_USER_CAP_TASK       = L4_BASE_TASK_CAP,     // 0
  L4LX_USER_CAP_FACTORY    = L4_BASE_FACTORY_CAP,  // 1
  L4LX_USER_CAP_THREAD     = L4_BASE_THREAD_CAP,   // 2
  L4LX_USER_CAP_PAGER      = L4_BASE_PAGER_CAP,    // 3
  L4LX_USER_CAP_LOG        = L4_BASE_LOG_CAP,      // 4
  L4LX_USER_CAP_MA         = 7 << L4_CAP_SHIFT,
  L4LX_USER_CAP_NS         = 8 << L4_CAP_SHIFT,

  L4LX_USER_CAP_PAGER_BASE = 0x10 << L4_CAP_SHIFT,


  L4LX_KERN_CAP_HYBRID_BASE = 0x2000 << L4_CAP_SHIFT,
};

/* Address space layout in a user process */
enum {
  L4X_USER_KIP_ADDR = 0xbfdfd000,
  L4X_USER_KIP_SIZE = 12,

  L4X_USER_UTCB_ADDR = 0xbfdff000,
  L4X_USER_UTCB_SIZE = 12,
};

static inline l4_cap_idx_t l4x_user_pager_cap(unsigned cpu)
{
	return cpu ? (L4LX_USER_CAP_PAGER_BASE + (cpu << L4_CAP_SHIFT))
	           : L4LX_USER_CAP_PAGER;
}

static inline l4_utcb_t *l4x_user_utcb_addr(unsigned cpu)
{
	BUILD_BUG_ON(MAXCPUS * L4_UTCB_OFFSET > (1 << L4X_USER_UTCB_SIZE));
	return (l4_utcb_t *)(L4X_USER_UTCB_ADDR + L4_UTCB_OFFSET * cpu);
}
#endif /* ! __INCLUDE__ASM_L4__GENERIC__USER_H__ */
