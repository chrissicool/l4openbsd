#ifndef __ASM_L4__L4X_I386__IODB_H__
#define __ASM_L4__L4X_I386__IODB_H__


#define L4X_IODB_NUMBER_IO_PORTS (1 << 16)
#define L4X_IODB_MAX_IO_PORT	(L4X_IODB_NUMBER_IO_PORTS - 1)
#define L4X_IODB_PORT_IOPL	(L4X_IODB_NUMBER_IO_PORTS)
#define L4X_IODB_MAX_SIZE	64
#define L4X_IODB_BLOCK_SIZE	4

//#define DBG_IODB(format, args...)  LOG_printf(format , ## args)
#define DBG_IODB(format, args...)  do { } while (0)

/* Structure of the IODB. We assume:
 *  - only a few entries are used (0-2)
 *  - a size of IODB_MIN_SIZE should be the default
 *  - ioperm is independent from iopl
 *  - 64k ioports
 *  - minimal and sorted storage
 *  - the size is a FP (2<<size)
 *  - we don't allow more than 64 entries -- this should be sufficient
 */

//struct l4x_iodb_head {
//	unsigned iopl:2;	/**< IOPL of process */
//	unsigned count:16;	/**< number of valid entries in body */
//	unsigned size:14;	/**< number of allocated entries in body */
//};

/* we can not use count, because of 65536 port's */
//struct l4x_iodb_entry {
//	unsigned start:16;
//	unsigned end:16;
//};

//struct l4x_iodb {
//	struct l4x_iodb_head head;	/**< 4 bytes */
//	struct l4x_iodb_entry body[1];	/**< n * 4 bytes */
//};

void l4x_iodb_init(void);

//int l4x_iodb_write_portrange(struct task_struct *task,
//                             int port, int count, int value);
//int l4x_iodb_read_portrange(const struct task_struct *task,
 //                           int port, int count);

//int l4x_iodb_copy(struct task_struct *from, struct task_struct *to);

/*
static inline void l4x_iodb_free(struct task_struct *task)
{
	if (likely(task)) {
		kfree(task->thread.iodb);
		task->thread.iodb = NULL;
	}
}
*/
#endif /* ! __ASM_L4__L4X_I386__IODB_H__ */
