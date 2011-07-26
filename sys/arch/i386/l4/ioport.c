/*
 * License:
 * This file is largely based on code from the L4Linux project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */
/*
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/syscalls.h>
#include <asm/syscalls.h>
*/
#include <l4/sys/ipc.h>
#include <l4/log/log.h>
#include <l4/io/io.h>

//#include <asm/generic/kthreads.h>
#include <machine/l4/l4lib.h>
#include <machine/l4/iodb.h>

L4_EXTERNAL_FUNC(l4io_request_all_ioports);

//static l4_uint8_t l4x_iobitmap[L4X_IODB_NUMBER_IO_PORTS / 8];
static int l4x_ioprot_enabled;
static int l4x_ioprot_level;

/**
 * Returnes allocated size of the iodb.
 */
/*static inline int iodb_get_size(const struct task_struct *task)
{
	return task->thread.iodb ? task->thread.iodb->head.size : 0;
}
*/
/**
 * Returnes iopl of the iodb.
 */
/*static inline int iodb_get_iopl(const struct task_struct *task)
{
	return task->thread.iodb ? task->thread.iodb->head.iopl : 0;
}
*/
/**
 * Returnes the number of valid items in the iodb.
 */
/*static inline int iodb_get_count(const struct task_struct *task)
{
	return task->thread.iodb ? task->thread.iodb->head.count : 0;
}
*/
/**
 * Sets the ld(size) of the iodb to the given value.
 */
/*static int
iodb_set_size(struct task_struct *task, struct l4x_iodb **iodb, int size)
{
	int old_size  = iodb_get_size(task);
	int old_count = iodb_get_count(task);
	struct l4x_iodb *tmp = NULL;

	DBG_IODB("%s(%d)\n", __func__, size);

	if (old_size == size)
		return 0;

	if (size < 0 || size > L4X_IODB_MAX_SIZE) {
		LOG_printf("iodb_set_size(): invalid size %d\n", size);
		return -EINVAL;
	}

	if (old_count && size <= old_count) {
		LOG_printf("%s(): size (%d) <= count (%d)\n",
		           __func__, size, old_count);
		return -EINVAL;
	}

	if (size > 0) {
		if (!(tmp = kmalloc(size * L4X_IODB_BLOCK_SIZE, GFP_KERNEL))) {
			LOG_printf("%s: Out of memory\n", __func__);
			return -ENOMEM;
		}
		if (old_size > 0)
			memcpy(tmp, task->thread.iodb,
			       (size < old_size ? size : old_size)
			        * L4X_IODB_BLOCK_SIZE);
		tmp->head.iopl  = iodb_get_iopl(task);
		tmp->head.count = old_count;
		tmp->head.size  = size;
	}

	if (old_size > 0)
		kfree(task->thread.iodb);

	*iodb = task->thread.iodb = tmp;
	return 0;
}

int l4x_iodb_copy(struct task_struct *from, struct task_struct *to)
{
	struct l4x_iodb *n, *f = from->thread.iodb;

	if (!f)
		return 0;

	if (!to->thread.iodb)
		kfree(to->thread.iodb);

	if (!(n = kmalloc(f->head.size * L4X_IODB_BLOCK_SIZE, GFP_KERNEL))) {
		LOG_printf("%s: Out of memory\n", __func__);
		return -ENOMEM;
	}

	memcpy(n, f, f->head.size * L4X_IODB_BLOCK_SIZE);

	to->thread.iodb = n;

	return 0;
}
*/
/**
 * Sets the IOPL to the given value.
 */
/*static int iodb_set_iopl(struct task_struct *task, int value)
{
	int iodb_size         = iodb_get_size(task);
	struct l4x_iodb *iodb = task->thread.iodb;
	int ret;

	if (!value) {
		if (iodb_size > 0) {
			if (iodb_get_count(task) > 0)
				// store IOPL
				task->thread.iodb->head.iopl = value;
			else
				// delete IODB
				return iodb_set_size(task, &iodb, 0);
		}
	} else {
		if (!iodb_size)
			if ((ret = iodb_set_size(task, &iodb, 1)))
				return ret;
		iodb->head.iopl = value;
	}
	return 0;
}
*/
/**
 * Enables a port range [port,port+count-1]
 */
/*static int iodb_set_range(struct task_struct *task, int port, int count)
{
	int i, ret;
	int iodb_count        = iodb_get_count(task);
	int iodb_size         = iodb_get_size(task);
	struct l4x_iodb *iodb = task->thread.iodb;

	DBG_IODB("%s(%p, %04x, %04x)\n", __func__, iodb, port, count);

	// find insert pos
	for (i = 0; i < iodb_count && port > iodb->body[i].end + 1; i++)
		;

	if (i == iodb_count || port + count < iodb->body[i].start) {
		// append/insert
		if (iodb_count + 1 >= iodb_size)
			if ((ret = iodb_set_size(task, &iodb, iodb_size + 8)))
				return ret;
		if (i < iodb_count)
			memmove(&iodb->body[i + 1], &iodb->body[i],
			        (iodb_count - i) * L4X_IODB_BLOCK_SIZE);
		iodb->body[i].start = port;
		iodb->body[i].end   = port + count - 1;
		iodb->head.count++;
		return 0;
	}

	// merge somehow
	if (port < iodb->body[i].start)
		iodb->body[i].start = port;
	if (port + count - 1 > iodb->body[i].end)
		iodb->body[i].end = port + count - 1;

	for (; i < iodb_count-1 && port + count >= iodb->body[i + 1].start; ) {
		// merge with following entry
		if (iodb->body[i + 1].end > iodb->body[i].end)
			iodb->body[i].end = iodb->body[i + 1].end;
		if (i < iodb_count - 2)
			memmove(&iodb->body[i + 1], &iodb->body[i + 2],
			        (iodb_count - i - 1) * L4X_IODB_BLOCK_SIZE);
		iodb->head.count--;
		if (iodb_count <= iodb_size - 8)
			if ((ret = iodb_set_size(task, &iodb, iodb_size - 8)))
				return ret;
		iodb_count = iodb_get_count(task);
		iodb_size  = iodb_get_size(task);
	}

	return 0;
}
*/
/**
 * Disables a port range [port,port+count-1]
 */
/*static int iodb_del_range(struct task_struct *task, int port, int count)
{
	int i, ret;
	int iodb_count        = iodb_get_count(task);
	int iodb_size         = iodb_get_size(task);
	struct l4x_iodb *iodb = task->thread.iodb;

	DBG_IODB("%s(%p, %04x, %04x)\n", __func__, iodb, port, count);

	if (!iodb_count)
		return 0;

	// find starting pos
	for (i = 0; i < iodb_count && port > iodb->body[i].end + 1; i++)
		;

	if (i == iodb_count || port + count < iodb->body[i].start)
		// delete bits between two entries
		return 0;

	// delete:
	if (port > iodb->body[i].start && port + count < iodb->body[i].end) {
		// delete a chunk from the middle of the current entry
		if (iodb_count + 1 >= iodb_size)
			if ((ret = iodb_set_size(task, &iodb, iodb_size + 8)))
				return ret;
		memmove(&iodb->body[i + 1], &iodb->body[i],
		        (iodb_count - i) * L4X_IODB_BLOCK_SIZE);
		iodb->body[i + 1].end   = iodb->body[i].end;
		iodb->body[i    ].end   = port - 1;
		iodb->body[i + 1].start = port + count;
		iodb->head.count++;
		return 0;
	}

	if (port > iodb->body[i].start && port + count >= iodb->body[i].end)
		// delete a chunk from the end of the current entry
		iodb->body[i].end = port - 1;

	for (; i < iodb_count; ) {
		if (port <= iodb->body[i].start
		    && port + count >= iodb->body[i].start)
			// delete a chunk from the begining of the current entry
			iodb->body[i].start = port + count;

		if (iodb->body[i].start > iodb->body[i].end) {
			// remove superfluous entry
			if (i < iodb_count - 1)
				memmove(&iodb->body[i], &iodb->body[i + 1],
				        (iodb_count - i - 1) * L4X_IODB_BLOCK_SIZE);
			iodb->head.count--;
			if (iodb_count <= iodb_size - 8)
				if ((ret = iodb_set_size(task, &iodb, iodb_size - 8)))
					return ret;
			iodb_count = iodb_get_count(task);
			iodb_size  = iodb_get_size(task);
		} else
			break;
	}

	return 0;
}
*/

/**
 * Writes the permission of a portrange to the iodb.
 * Returns -EINVAL on invalid parameters and 0 on success.
 */
/*int l4x_iodb_write_portrange(struct task_struct *task, int port,
                             int count, int value)
{
	int result;

	DBG_IODB("%s[%s](%p, %04x, %04x, %d)\n", __func__, current->comm,
	                                           __builtin_return_address(0),
	                                           port,count, value);

	if ((port < 0 || port > L4X_IODB_MAX_IO_PORT)
	    && port != L4X_IODB_PORT_IOPL) {
		LOG_printf("%s(): port %04x invalid\n", __func__, port);
		result = -EINVAL;
		goto done;
	}
	if (count < 0 || count - 1 > L4X_IODB_MAX_IO_PORT) {
		LOG_printf("%s(): count %04x invalid\n", __func__, count);
		result = -EINVAL;
		goto done;
	}

	result = (port == L4X_IODB_PORT_IOPL)
	         ? iodb_set_iopl(task, value)
	         : value ? iodb_set_range(task, port, count)
	                 : iodb_del_range(task, port, count);

done:
	DBG_IODB("%s() RESULT=%d\n", __func__, result);
	return result;
}
*/
/**
 * Read the permission of a portrange from the iodb.
 * Returns -EINVAL on invalid port number.
 * Otherwise the IOPL or the permission of a single port is returned.
 */
/*int l4x_iodb_read_portrange(const struct task_struct *task, int port, int count)
{
	int result = 0;
	int i;
	const struct l4x_iodb *iodb = task->thread.iodb;

	DBG_IODB("%s[%s] (%p, %04x, %04x)\n", __func__, current->comm,
	                                        __builtin_return_address(0),
	                                        port, count);

	// per default are all ports disabled and the IOPL is 0.
	if (!iodb) {
		result = 0;
		goto done;
	}
	// the IOPL is in the head
	if (port == L4X_IODB_PORT_IOPL) {
		result = iodb->head.iopl;
		goto done;
	}
	if (port < 0 || port > L4X_IODB_MAX_IO_PORT) {
		result = 0;
		goto done;
	}
	// iterate through the body
	for (i = 0; i < iodb->head.count; i++)
		if (iodb->body[i].start <= port
		    && iodb->body[i].end >= port + count - 1) {
			result = 1;
			break;
		}

done:
	DBG_IODB("%s() RESULT=%d\n", __func__, result);
	return result;
}
*/
/**
 * Flush all IO-Pages in the current process.
 * Tell the emulib to do this for us,
 * because we are not in the context of the user task.
 */
/*static int flush_io_pages(struct task_struct *task, l4_fpage_t fpage, int flush)
{
	if (flush) {
		//task->tss.shared_data->flush_page = fpage.fpage;
		DBG_IODB("revoke rights %08x\n", (unsigned)fpage.fpage);
	}
	else
		//task->tss.shared_data->flush_page = 0;
		;

	return 0;
}
*/
/**
 * The same security policy as in arch/i386/kernel/ioport.c.
 */
/*asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	struct task_struct *task = current;
	int ret, i;

	DBG_IODB("%s(%s, %04lx, %04lx, %d)\n", __func__, task->comm,
	                                       from, num, turn_on);

	if ((from + num <= from) || (from + num > L4X_IODB_NUMBER_IO_PORTS))
		return -EINVAL;

	if (turn_on && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (!l4x_ioprot_enabled)
		return 0;

	// try to get access to ports that _we_ don't have?
	if (turn_on) {
		for (i = from; i < from + num; i++)
			if (!(l4x_iobitmap[i / 8] & (1 << (i % 8))))
				return -EPERM;
	}

	// set the iobitmap
	if ((ret = l4x_iodb_write_portrange(task, from, num, turn_on)))
		return ret;
	flush_io_pages(task, l4_fpage(0, L4_WHOLE_IOADDRESS_SPACE, 0),
	               !turn_on);

	return 0;
}
*/
/**
 * The same security policy as in arch/i386/kernel/ioport.c.
 */
/*long sys_iopl(unsigned int level, struct pt_regs *regs)
{
	struct task_struct *task = current;
	int old, ret;

	DBG_IODB("%s(%s, %ld)\n", __func__, task->comm, level);

	if (level > 3)
		return -EINVAL;

	if (!l4x_ioprot_enabled)
		return 0;

	// trying to gain more privileges than _we_ have?
	if (level > l4x_ioprot_level)
		return -EINVAL;

	// Trying to gain more privileges?
	old = l4x_iodb_read_portrange(task, L4X_IODB_PORT_IOPL, 0);
	if (level > old) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}

	if ((ret = l4x_iodb_write_portrange(task, L4X_IODB_PORT_IOPL, 0, level)))
		return ret;
	flush_io_pages(task, l4_fpage(0, L4_WHOLE_IOADDRESS_SPACE, 0),
	               old > level);

	return 0;
}
*/
void l4x_iodb_init(void)
{
	/* Get I/O ports we can get */
	int i;

	l4io_request_all_ioports();
#if 0
	if (c == 1 << L4_WHOLE_IOADDRESS_SPACE)
		asm volatile ("cli; sti");
#endif
	asm volatile ("pushf ; pop %0" : "=rm"(i));
	l4x_ioprot_level = (i & 0x3000) >> 12;
	LOG_printf("Running at IOPL %d\n", l4x_ioprot_level);

	l4x_ioprot_enabled = 1;
}
