/*
 * l4ser - pseudo serial driver for OpenBSD/L4
 * Copyright (C) 2010  Christian Ludwig
 *
 * Large parts of this driver were taken from L4Linux. Other parts are taken
 * from the OpenBSD "com" driver. Copyrights and restrictions apply.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <dev/cons.h>
#include <sys/types.h>
#include <sys/errno.h>

#include <machine/l4/irq.h>
#include <machine/l4/vcpu.h>
#include <machine/l4/cap_alloc.h>
#include <machine/l4/util.h>
#include <machine/l4/log.h>

#include <l4/sys/vcon.h>
#include <l4/sys/factory.h>
#include <l4/sys/icu.h>
#include <l4/re/c/namespace.h>
#include <l4/re/c/util/cap.h>
#include <l4/log/log.h>

#define PORT0_NAME              "log"

cons_decl(l4ser);
cdev_decl(l4ser);
void l4serstart(struct tty *);

void l4ser_attach(struct device *, struct device *, void *);
int  l4ser_match(struct device *parent, void *v, void *aux);

/* helpers */
static int probe_l4ser(void);

/* DATA */

struct l4ser_softc {
	struct device	sc_dev;
	struct tty	*sc_tty;
	int		enabled;
};

struct cfattach l4ser_ca = {
	sizeof(struct l4ser_softc), l4ser_match, l4ser_attach
};

struct cfdriver l4ser_cd = {
	NULL, "l4ser", DV_TTY
};

static int l4sermajor;

static struct l4ser_uart {
	l4_cap_idx_t vcon_cap;
	l4_cap_idx_t vcon_irq_cap;
	int inited;
} l4ser;

/*
 * === AUTOCONF INTERFACE ===
 */

int
l4ser_match(struct device *parent, void *match, void *aux)
{
	struct l4ser_softc *sc = aux;
/*	struct cfdata *cf = match;		*/
/*	struct isa_attach_args *iaa = aux;	*/

	if (!probe_l4ser()) {
		sc->enabled = 1;
		return 1;
	}

	return 0;
}

void
l4ser_attach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");
}

int
l4seropen(dev_t dev, int oflags, int devtype, struct proc *p)
{
	struct l4ser_softc *sc;
	struct tty *tp;
	int error;

	if (!l4ser_cd.cd_ndevs || (sc = l4ser_cd.cd_devs[minor(dev)]) == NULL)
		return ENXIO;

	if ((tp = sc->sc_tty) == NULL) {
		tp = sc->sc_tty = ttymalloc(0);
	}
	else if ((tp->t_state & TS_ISOPEN) && (tp->t_state & TS_XCLUDE)
			&& suser(p, 0) != 0)
		return EBUSY;

	tp->t_oproc = l4serstart;
	tp->t_dev = dev;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		struct termios t;

		t.c_ispeed = t.c_ospeed = TTYDEF_SPEED;
		t.c_cflag = TTYDEF_CFLAG;
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		ttychars(tp);
		ttsetwater(tp);

		tp->t_state |= TS_CARR_ON;
	}

	error = ttyopen(dev, tp, p);
	if (error > 0)
		return error;

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
l4serclose(dev_t dev, int fflag, int devtype, struct proc *p)
{
	struct l4ser_softc *sc = l4ser_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;

	(*linesw[tp->t_line].l_close)(tp, fflag, p);

	return ttyclose(tp);
}

int
l4serread(dev_t dev, struct uio *uio, int ioflag)
{
	int c;
	struct l4ser_softc *sc = l4ser_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
	
	c = l4sercngetc(dev);
	if (c == -1)
		return EBUSY;

	/* Inject character into line. */
	(*linesw[tp->t_line].l_rint)(c, tp);

	return (*linesw[tp->t_line].l_read)(tp, uio, ioflag);
}

int
l4serwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct l4ser_softc *sc = l4ser_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;

	return (*linesw[tp->t_line].l_write)(tp, uio, ioflag);
}

int
l4serioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct l4ser_softc *sc = l4ser_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
	int error;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	return ENOTTY;
}

void
l4serstart(struct tty *tp)
{
	int c, s;

	s = spltty();
	c = getc(&tp->t_rawq);
	ttwakeupwr(tp);
	splx(s);

	if (c != -1)
		l4sercnputc(NODEV, c);
}

int
l4serstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
l4sertty(dev_t dev)
{
	struct l4ser_softc *sc = l4ser_cd.cd_devs[minor(dev)];

	return sc->sc_tty;
}

/*
 * === HELPERS ===
 */

static int probe_l4ser(void)
{
	int irq, r;
	l4_vcon_attr_t vcon_attr;
	L4XV_V(f);

	if (l4ser.inited)
		return 0;
	l4ser.inited = 1;

	if ((r = l4x_re_resolve_name(PORT0_NAME, &l4ser.vcon_cap))) {
		l4ser.vcon_cap = l4re_env()->log;
	}

	L4XV_L(f);
	l4ser.vcon_irq_cap = l4x_cap_alloc();
	if (l4_is_invalid_cap(l4ser.vcon_irq_cap)) {
		l4x_cap_free(l4ser.vcon_cap);
		l4ser.vcon_cap = L4_INVALID_CAP;
		L4XV_U(f);
		return ENOMEM;
	}
	if (l4_error(l4_factory_create_irq(l4re_env()->factory,
	                                   l4ser.vcon_irq_cap))) {
		l4re_util_cap_release(l4ser.vcon_cap);
		l4x_cap_free(l4ser.vcon_irq_cap);
		l4x_cap_free(l4ser.vcon_cap);
		L4XV_U(f);
		return ENOMEM;
	}

	if ((l4_error(l4_icu_bind(l4ser.vcon_cap, 0,
	                          l4ser.vcon_irq_cap)))) {
		l4re_util_cap_release(l4ser.vcon_irq_cap);
		l4x_cap_free(l4ser.vcon_irq_cap);

		// No interrupt, just output
		LOG_printf("l4ser: No interrupt, just output.\n");
		l4ser.vcon_irq_cap = L4_INVALID_CAP;
		irq = 0;
	} else if ((irq = l4x_register_irq_fixed(4, l4ser.vcon_irq_cap)) < 0) {
		l4x_cap_free(l4ser.vcon_irq_cap);
		l4x_cap_free(l4ser.vcon_cap);
		L4XV_U(f);
		return EIO;
	}

	vcon_attr.i_flags = 0;
	vcon_attr.o_flags = 0;
	vcon_attr.l_flags = 0;
	l4_vcon_set_attr(l4ser.vcon_cap, &vcon_attr);
	L4XV_U(f);

	return 0;
}

/*
 * === CONS INTERFACE ===
 */

/* cnprobe, cninit, cngetc, cnputc, cnpollc */
struct consdev l4ser_cons = cons_init(l4ser);

void l4sercnprobe(struct consdev *cp)
{
	/* Locate the major number. */
	for (l4sermajor = 0; l4sermajor < nchrdev; l4sermajor++)
		if (cdevsw[l4sermajor].d_open == l4seropen)
			break;
			
	if (probe_l4ser() || (l4sermajor == nchrdev)) {
		/* something went wrong, give up */
		l4x_printf("Error initializing l4ser console. NO CONSOLE!\n");
	}

	/* Initialize required fields */
	cp->cn_dev = makedev(l4sermajor, 0);
	cp->cn_pri = CN_HIGHPRI;
}

void l4sercninit(struct consdev *cp)
{
	/* NOTHING */
}

int l4sercngetc(dev_t dev)
{
	char c;
	int res;
	L4XV_V(f);

	L4XV_L(f);
	if (l4_is_invalid_cap(l4ser.vcon_irq_cap)
			|| l4_vcon_read(l4ser.vcon_cap, &c, 1) <= 0)
		res = -1;
	else
		res = c;
	L4XV_U(f);
	return res;
}

void l4sercnputc(dev_t dev, int i)
{
	char c;
	L4XV_V(f);

	c = i;
	L4XV_L(f);
	l4_vcon_write(l4ser.vcon_cap, &c, 1);
	L4XV_U(f);
}

void l4sercnpollc(dev_t dev, int i) { /* NOTHING */ }
