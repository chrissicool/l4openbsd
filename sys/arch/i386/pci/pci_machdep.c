/*	$OpenBSD: pci_machdep.c,v 1.53 2010/07/08 20:56:31 jordan Exp $	*/
/*	$NetBSD: pci_machdep.c,v 1.28 1997/06/06 23:29:17 thorpej Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Machine-specific functions for PCI autoconfiguration.
 *
 * On PCs, there are two methods of generating PCI configuration cycles.
 * We try to detect the appropriate mechanism for this machine and set
 * up a few function pointers to access the correct method directly.
 *
 * The configuration method can be hard-coded in the config file by
 * using `options PCI_CONF_MODE=N', where `N' is the configuration mode
 * as defined section 3.6.4.1, `Generating Configuration Cycles'.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/pio.h>
#include <machine/i8259.h>
#include <machine/biosvar.h>

#include "bios.h"
#if NBIOS > 0
extern bios_pciinfo_t *bios_pciinfo;
#endif

#include <dev/isa/isavar.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

#include "ioapic.h"

#include <machine/i82093var.h>
#if NIOAPIC > 0
#include <machine/mpbiosvar.h>
#endif

#include "pcibios.h"
#if NPCIBIOS > 0
#include <i386/pci/pcibiosvar.h>
#endif

#ifdef L4
#include <l4/vbus/vbus.h>
#include <l4/vbus/vbus_pci.h>
#include <l4/re/c/namespace.h>
#include <machine/l4/l4lib.h>
#include <i386/l4/dev/l4_machdep.h>

int l4pci_probe(void);

l4_cap_idx_t vbus;
l4vbus_device_handle_t root_bridge;
#endif	/* L4 */

int pci_mode = -1;

/*
 * Memory Mapped Configuration space access.
 *
 * Since mapping the whole configuration space will cost us up to
 * 256MB of kernel virtual memory, we use seperate mappings per bus.
 * The mappings are created on-demand, such that we only use kernel
 * virtual memory for busses that are actually present.
 */
bus_addr_t pci_mcfg_addr;
int pci_mcfg_min_bus, pci_mcfg_max_bus;
bus_space_tag_t pci_mcfgt = I386_BUS_SPACE_MEM;
bus_space_handle_t pci_mcfgh[256];
void pci_mcfg_map_bus(int);

struct mutex pci_conf_lock = MUTEX_INITIALIZER(IPL_HIGH);

#define	PCI_CONF_LOCK()							\
do {									\
	mtx_enter(&pci_conf_lock);					\
} while (0)

#define	PCI_CONF_UNLOCK()						\
do {									\
	mtx_leave(&pci_conf_lock);					\
} while (0)

#define	PCI_MODE1_ENABLE	0x80000000UL
#define	PCI_MODE1_ADDRESS_REG	0x0cf8
#define	PCI_MODE1_DATA_REG	0x0cfc

#define	PCI_MODE2_ENABLE_REG	0x0cf8
#define	PCI_MODE2_FORWARD_REG	0x0cfa

#define _m1tag(b, d, f) \
	(PCI_MODE1_ENABLE | ((b) << 16) | ((d) << 11) | ((f) << 8))
#define _qe(bus, dev, fcn, vend, prod) \
	{_m1tag(bus, dev, fcn), PCI_ID_CODE(vend, prod)}
struct {
	u_int32_t tag;
	pcireg_t id;
} pcim1_quirk_tbl[] = {
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX1),
	/* XXX Triflex2 not tested */
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX2),
	_qe(0, 0, 0, PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_TRIFLEX4),
	/* Triton needed for Connectix Virtual PC */
	_qe(0, 0, 0, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82437FX),
	/* Connectix Virtual PC 5 has a 440BX */
	_qe(0, 0, 0, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82443BX_NOAGP),
	{0, 0xffffffff} /* patchable */
};
#undef _m1tag
#undef _qe

/*
 * PCI doesn't have any special needs; just use the generic versions
 * of these functions.
 */
struct bus_dma_tag pci_bus_dma_tag = {
	NULL,			/* _cookie */
	_bus_dmamap_create, 
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_unload,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

#ifdef L4
int
l4pci_probe(void) 
{
        L4XV_V(f);

        vbus = l4re_get_env_cap("vbus");
        if (l4_is_invalid_cap(vbus))
                return 0;

        L4XV_L(f);
        if (l4vbus_get_device_by_hid(vbus, 0, &root_bridge, "PNP0A03", 0, 0)
	    < 0) {
                L4XV_U(f);
                printf("no root bridge found, no PCI");
                return 0;
        } 
        L4XV_U(f);

	return 1;
}
#endif	/* L4 */

void
pci_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{

#if NBIOS > 0
	if (pba->pba_bus == 0)
		printf(": configuration mode %d (%s)",
			pci_mode, (bios_pciinfo?"bios":"no bios"));
#else
	if (pba->pba_bus == 0)
		printf(": configuration mode %d", pci_mode);
#endif
}

int
pci_bus_maxdevs(pci_chipset_tag_t pc, int busno)
{

	/*
	 * Bus number is irrelevant.  If Configuration Mechanism 2 is in
	 * use, can only have devices 0-15 on any bus.  If Configuration
	 * Mechanism 1 is in use, can have devices 0-32 (i.e. the `normal'
	 * range).
	 */
	if (pci_mode == 2)
		return (16);
	else
		return (32);
}

pcitag_t
pci_make_tag(pci_chipset_tag_t pc, int bus, int device, int function)
{
	pcitag_t tag;

	switch (pci_mode) {
	case 1:
		if (bus >= 256 || device >= 32 || function >= 8)
			panic("pci_make_tag: bad request");

		tag.mode1 = PCI_MODE1_ENABLE |
		    	(bus << 16) | (device << 11) | (function << 8);
		break;
	case 2:
		if (bus >= 256 || device >= 16 || function >= 8)
			panic("pci_make_tag: bad request");

		tag.mode2.port = 0xc000 | (device << 8);
		tag.mode2.enable = 0xf0 | (function << 1);
		tag.mode2.forward = bus;
		break;
	default:
		panic("pci_make_tag: mode not configured");
	}

	return tag;
}

void
pci_decompose_tag(pci_chipset_tag_t pc, pcitag_t tag, int *bp, int *dp, int *fp)
{

	switch (pci_mode) {
	case 1:
		if (bp != NULL)
			*bp = (tag.mode1 >> 16) & 0xff;
		if (dp != NULL)
			*dp = (tag.mode1 >> 11) & 0x1f;
		if (fp != NULL)
			*fp = (tag.mode1 >> 8) & 0x7;
		break;
	case 2:
		if (bp != NULL)
			*bp = tag.mode2.forward & 0xff;
		if (dp != NULL)
			*dp = (tag.mode2.port >> 8) & 0xf;
		if (fp != NULL)
			*fp = (tag.mode2.enable >> 1) & 0x7;
		break;
	default:
		panic("pci_decompose_tag: mode not configured");
	}
}

int
pci_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	int bus;

	if (pci_mcfg_addr) {
		pci_decompose_tag(pc, tag, &bus, NULL, NULL);
		if (bus >= pci_mcfg_min_bus && bus <= pci_mcfg_max_bus)
			return PCIE_CONFIG_SPACE_SIZE;
	}

	return PCI_CONFIG_SPACE_SIZE;
}

void
pci_mcfg_map_bus(int bus)
{
	if (pci_mcfgh[bus])
		return;

	if (bus_space_map(pci_mcfgt, pci_mcfg_addr + (bus << 20), 1 << 20,
	    0, &pci_mcfgh[bus]))
		panic("pci_conf_read: cannot map mcfg space");
}

#ifdef L4
pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	L4XV_V(f);
	pcireg_t data;
	int ret;
	int bus, slot, function;
	l4_uint32_t df;

	PCI_CONF_LOCK();

	pci_decompose_tag(pc, tag, &bus, &slot, &function);
	df = ((slot & 0xffff) << 16) | (function & 0xffff);

	L4XV_L(f);
	ret = l4vbus_pci_cfg_read(vbus, root_bridge, bus, df, reg, &data,
	    sizeof(data) * 8);	/* XXX hshoexer */
	L4XV_U(f);

	PCI_CONF_UNLOCK();

	return data;
}
#else	/* L4 */
pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	pcireg_t data;
	int bus;

	if (pci_mcfg_addr && reg >= PCI_CONFIG_SPACE_SIZE) {
		pci_decompose_tag(pc, tag, &bus, NULL, NULL);
		if (bus >= pci_mcfg_min_bus && bus <= pci_mcfg_max_bus) {
			pci_mcfg_map_bus(bus);
			data = bus_space_read_4(pci_mcfgt, pci_mcfgh[bus],
			    (tag.mode1 & 0x000ff00) << 4 | reg);
			return data;
		}
	}

	PCI_CONF_LOCK();
	switch (pci_mode) {
	case 1:
		outl(PCI_MODE1_ADDRESS_REG, tag.mode1 | reg);
		data = inl(PCI_MODE1_DATA_REG);
		outl(PCI_MODE1_ADDRESS_REG, 0);
		break;
	case 2:
		outb(PCI_MODE2_ENABLE_REG, tag.mode2.enable);
		outb(PCI_MODE2_FORWARD_REG, tag.mode2.forward);
		data = inl(tag.mode2.port | reg);
		outb(PCI_MODE2_ENABLE_REG, 0);
		break;
	default:
		panic("pci_conf_read: mode not configured");
	}
	PCI_CONF_UNLOCK();

	return data;
}
#endif	/* L4 */

#ifdef L4
void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
	L4XV_V(f);
	int ret;
	int bus, slot, function;
	l4_uint32_t df;

	PCI_CONF_LOCK();

	pci_decompose_tag(pc, tag, &bus, &slot, &function);
	df = ((slot & 0xffff) << 16) | (function & 0xffff);

	L4XV_L(f);
	ret = l4vbus_pci_cfg_write(vbus, root_bridge, bus, df, reg, data,
	    sizeof(data) * 8);	/* XXX hshoexer */
	L4XV_U(f);

	PCI_CONF_UNLOCK();
}
#else	/* L4 */
void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{

	PCI_CONF_LOCK();
	switch (pci_mode) {
	case 1:
		outl(PCI_MODE1_ADDRESS_REG, tag.mode1 | reg);
		outl(PCI_MODE1_DATA_REG, data);
		outl(PCI_MODE1_ADDRESS_REG, 0);
		break;
	case 2:
		outb(PCI_MODE2_ENABLE_REG, tag.mode2.enable);
		outb(PCI_MODE2_FORWARD_REG, tag.mode2.forward);
		outl(tag.mode2.port | reg, data);
		outb(PCI_MODE2_ENABLE_REG, 0);
		break;
	default:
		panic("pci_conf_write: mode not configured");
	}
	PCI_CONF_UNLOCK();
}
#endif	/* L4 */

int
pci_mode_detect(void)
{

#ifdef PCI_CONF_MODE
#if (PCI_CONF_MODE == 1) || (PCI_CONF_MODE == 2)
	return (pci_mode = PCI_CONF_MODE);
#else
#error Invalid PCI configuration mode.
#endif
#else
	u_int32_t sav, val;
	int i;
	pcireg_t idreg;

	if (pci_mode != -1)
		return (pci_mode);

#if NBIOS > 0
	/*
	 * If we have PCI info passed from the BIOS, use the mode given there
	 * for all of this code.  If not, pass on through to the previous tests
	 * to try and divine the correct mode.
	 */
	if (bios_pciinfo != NULL) {
		if (bios_pciinfo->pci_chars & 0x2)
			return (pci_mode = 2);

		if (bios_pciinfo->pci_chars & 0x1)
			return (pci_mode = 1);

		/* We should never get here, but if we do, fall through... */
	}
#endif

	/*
	 * We try to divine which configuration mode the host bridge wants.
	 *
	 * This should really be done using the PCI BIOS.  If we get here, the
	 * PCI BIOS does not exist, or the boot blocks did not provide the
	 * information.
	 */

	sav = inl(PCI_MODE1_ADDRESS_REG);

	pci_mode = 1; /* assume this for now */
	/*
	 * catch some known buggy implementations of mode 1
	 */
	for (i = 0; i < sizeof(pcim1_quirk_tbl) / sizeof(pcim1_quirk_tbl[0]);
	     i++) {
		pcitag_t t;

		if (!pcim1_quirk_tbl[i].tag)
			break;
		t.mode1 = pcim1_quirk_tbl[i].tag;
		idreg = pci_conf_read(0, t, PCI_ID_REG); /* needs "pci_mode" */
		if (idreg == pcim1_quirk_tbl[i].id) {
#ifdef DEBUG
			printf("known mode 1 PCI chipset (%08x)\n",
			       idreg);
#endif
			return (pci_mode);
		}
	}

	/*
	 * Strong check for standard compliant mode 1:
	 * 1. bit 31 ("enable") can be set
	 * 2. byte/word access does not affect register
 	 */
	outl(PCI_MODE1_ADDRESS_REG, PCI_MODE1_ENABLE);
	outb(PCI_MODE1_ADDRESS_REG + 3, 0);
	outw(PCI_MODE1_ADDRESS_REG + 2, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != PCI_MODE1_ENABLE) {
#ifdef DEBUG
		printf("pci_mode_detect: mode 1 enable failed (%x)\n",
		       val);
#endif
		goto not1;
	}
	outl(PCI_MODE1_ADDRESS_REG, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != 0)
		goto not1;
	return (pci_mode);
not1:
	outl(PCI_MODE1_ADDRESS_REG, sav);
 
	/*
	 * This mode 2 check is quite weak (and known to give false
	 * positives on some Compaq machines).
	 * However, this doesn't matter, because this is the
	 * last test, and simply no PCI devices will be found if
	 * this happens.
	 */
	outb(PCI_MODE2_ENABLE_REG, 0);
	outb(PCI_MODE2_FORWARD_REG, 0);
	if (inb(PCI_MODE2_ENABLE_REG) != 0 ||
	    inb(PCI_MODE2_FORWARD_REG) != 0)
		goto not2;
	return (pci_mode = 2);
not2:
	return (pci_mode = 0);
#endif
}

int
pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int pin = pa->pa_rawintrpin;
	int line = pa->pa_intrline;
#if NIOAPIC > 0
	struct mp_intr_map *mip;
	int bus, dev, func;
#endif

	if (pin == 0) {
		/* No IRQ used. */
		goto bad;
	}

	if (pin > PCI_INTERRUPT_PIN_MAX) {
		printf("pci_intr_map: bad interrupt pin %d\n", pin);
		goto bad;
	}

	ihp->tag = pa->pa_tag;
	ihp->line = line;
	ihp->pin = pin;

#if NIOAPIC > 0
	pci_decompose_tag (pa->pa_pc, pa->pa_tag, &bus, &dev, &func);

	if (!(ihp->line & PCI_INT_VIA_ISA) && mp_busses != NULL) {
		/*
		 * Assumes 1:1 mapping between PCI bus numbers and
		 * the numbers given by the MP bios.
		 * XXX Is this a valid assumption?
		 */
		int mpspec_pin = (dev<<2)|(pin-1);

		for (mip = mp_busses[bus].mb_intrs; mip != NULL; mip=mip->next) {
			if (mip->bus_pin == mpspec_pin) {
				ihp->line = mip->ioapic_ih | line;
				return 0;
			}
		}

		if (pa->pa_bridgetag) {
			int swizpin = PPB_INTERRUPT_SWIZZLE(pin, dev);
			if (pa->pa_bridgeih[swizpin - 1].line != -1) {
				ihp->line = pa->pa_bridgeih[swizpin - 1].line;
				ihp->line |= line;
				return 0;
			}
		}
		/*
		 * No explicit PCI mapping found. This is not fatal,
		 * we'll try the ISA (or possibly EISA) mappings next.
		 */
	}
#endif

#if NPCIBIOS > 0
	pci_intr_header_fixup(pa->pa_pc, pa->pa_tag, ihp);
	line = ihp->line & APIC_INT_LINE_MASK;
#endif

	/*
	 * Section 6.2.4, `Miscellaneous Functions', says that 255 means
	 * `unknown' or `no connection' on a PC.  We assume that a device with
	 * `no connection' either doesn't have an interrupt (in which case the
	 * pin number should be 0, and would have been noticed above), or
	 * wasn't configured by the BIOS (in which case we punt, since there's
	 * no real way we can know how the interrupt lines are mapped in the
	 * hardware).
	 *
	 * XXX
	 * Since IRQ 0 is only used by the clock, and we can't actually be sure
	 * that the BIOS did its job, we also recognize that as meaning that
	 * the BIOS has not configured the device.
	 */
	if (line == 0 || line == I386_PCI_INTERRUPT_LINE_NO_CONNECTION)
		goto bad;

	if (line >= ICU_LEN) {
		printf("pci_intr_map: bad interrupt line %d\n", line);
		goto bad;
	}
	if (line == 2) {
		printf("pci_intr_map: changed line 2 to line 9\n");
		line = 9;
	}

#if NIOAPIC > 0
	if (!(ihp->line & PCI_INT_VIA_ISA) && mp_busses != NULL) {
		if (mip == NULL && mp_isa_bus) {
			for (mip = mp_isa_bus->mb_intrs; mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
		if (mip == NULL && mp_eisa_bus) {
			for (mip = mp_eisa_bus->mb_intrs;  mip != NULL;
			    mip = mip->next) {
				if (mip->bus_pin == line) {
					ihp->line = mip->ioapic_ih | line;
					return 0;
				}
			}
		}
		if (mip == NULL) {
			printf("pci_intr_map: "
			    "bus %d dev %d func %d pin %d; line %d\n",
			    bus, dev, func, pin, line);
			printf("pci_intr_map: no MP mapping found\n");
		}
	}
#endif

#ifdef L4
	if ((ihp->line = l4pci_device_enable(pa, 0)) < 0)
		goto bad;
#endif

	return 0;

bad:
	ihp->line = -1;
	return 1;
}

const char *
pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	static char irqstr[64];
	int line = ih.line & APIC_INT_LINE_MASK;

#ifdef L4
	if (line >= 0) {
		snprintf(irqstr, sizeof(irqstr), "irq %d", line);
		return (irqstr);
	} else
		panic("%s: line %d", __func__, line);
#endif

#if NIOAPIC > 0
	if (ih.line & APIC_INT_VIA_APIC) {
		snprintf(irqstr, sizeof irqstr, "apic %d int %d (irq %d)",
		     APIC_IRQ_APIC(ih.line), APIC_IRQ_PIN(ih.line), line);
		return (irqstr);
	}
#endif

	if (line == 0 || line >= ICU_LEN || line == 2)
		panic("pci_intr_string: bogus handle 0x%x", line);

	snprintf(irqstr, sizeof irqstr, "irq %d", line);
	return (irqstr);
}

#include "acpiprt.h"
#if NACPIPRT > 0
void	acpiprt_route_interrupt(int bus, int dev, int pin);
#endif

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *what)
{
	void *ret;
	int bus, dev;
	int l = ih.line & APIC_INT_LINE_MASK;

	pci_decompose_tag(pc, ih.tag, &bus, &dev, NULL);
#if NACPIPRT > 0
	acpiprt_route_interrupt(bus, dev, ih.pin);
#endif

#if NIOAPIC > 0
	if (l != -1 && ih.line & APIC_INT_VIA_APIC)
		return (apic_intr_establish(ih.line, IST_LEVEL, level, func, 
		    arg, what));
#endif

#ifdef L4
	return (l4_intr_establish(ih.line, IST_LEVEL, level, func, arg, what));
#endif
	if (l == 0 || l >= ICU_LEN || l == 2)
		panic("pci_intr_establish: bogus handle 0x%x", l);

	ret = isa_intr_establish(NULL, l, IST_LEVEL, level, func, arg, what);
#if NPCIBIOS > 0
	if (ret)
		pci_intr_route_link(pc, &ih);
#endif
	return (ret);
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	/* XXX oh, unroute the pci int link? */
	isa_intr_disestablish(NULL, cookie);
}

struct extent *pciio_ex;
struct extent *pcimem_ex;

void
pci_init_extents(void)
{
#ifndef L4	/* XXX hshoexer */
	bios_memmap_t *bmp;
	u_int64_t size;
#endif

	if (pciio_ex == NULL) {
		/*
		 * We only have 64K of addressable I/O space.
		 * However, since BARs may contain garbage, we cover
		 * the full 32-bit address space defined by PCI of
		 * which we only make the first 64K available.
		 */
		pciio_ex = extent_create("pciio", 0, 0xffffffff, M_DEVBUF,
		    NULL, 0, EX_NOWAIT | EX_FILLED);
		if (pciio_ex == NULL)
			return;
		extent_free(pciio_ex, 0, 0x10000, M_NOWAIT);
	}

	if (pcimem_ex == NULL) {
		pcimem_ex = extent_create("pcimem", 0, 0xffffffff, M_DEVBUF,
		    NULL, 0, EX_NOWAIT);
		if (pcimem_ex == NULL)
			return;

#ifndef L4
		for (bmp = bios_memmap; bmp->type != BIOS_MAP_END; bmp++) {
			/*
			 * Ignore address space beyond 4G.
			 */
			if (bmp->addr >= 0x100000000ULL)
				continue;
			size = bmp->size;
			if (bmp->addr + size >= 0x100000000ULL)
				size = 0x100000000ULL - bmp->addr;

			/* Ignore zero-sized regions. */
			if (size == 0)
				continue;

			if (extent_alloc_region(pcimem_ex, bmp->addr, size,
			    EX_NOWAIT))
				printf("memory map conflict 0x%llx/0x%llx\n",
				    bmp->addr, bmp->size);
		}
#endif	/* !L4 */

		/* Take out the video buffer area and BIOS areas. */
		extent_alloc_region(pcimem_ex, IOM_BEGIN, IOM_SIZE,
		    EX_CONFLICTOK | EX_NOWAIT);
	}
}

#include "acpi.h"
#if NACPI > 0
void acpi_pci_match(struct device *, struct pci_attach_args *);
#endif

void
pci_dev_postattach(struct device *dev, struct pci_attach_args *pa)
{
#if NACPI > 0
	acpi_pci_match(dev, pa);
#endif
}

#ifdef L4
int
l4pci_device_enable(struct pci_attach_args *pa, int altirq)
{
	int pin = pa->pa_rawintrpin;
	int line = pa->pa_intrline;
	l4_uint32_t df;
	unsigned char trigger, polarity;
	int irq, bus, dev, func;
	L4XV_V(f);

	/* XXX hshoexer */
	if (altirq != 0)
		line = altirq;

	L4XV_L(f);

	pci_decompose_tag(pa->pa_pc, pa->pa_tag, &bus, &dev, &func);
	df = ((dev & 0xffff) << 16) | (func & 0xffff);

	irq = l4vbus_pci_irq_enable(vbus, root_bridge, bus, df, pin, &trigger,
	    &polarity);
	if (irq < 0)
		panic("Failed to enable PCI INT %c: no GSI\n", 'A' + pin);

	L4XV_U(f);

	return irq;
}
#endif	/* L4 */
