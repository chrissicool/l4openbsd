/*
 * Copyright (c) 2011 Hans-Jörg Höxer <hshoexer@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <i386/l4/dev/l4busvar.h>

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/extent.h>

struct l4bus_softc {
        struct device sc_dev;
};

int l4bus_match(struct device *, void *, void *);
void l4bus_attach(struct device *, struct device *, void *);

struct cfattach l4bus_ca = {
        sizeof(struct l4bus_softc), l4bus_match, l4bus_attach
};

struct cfdriver l4bus_cd = {
        NULL, "l4bus", DV_DULL
};

int
l4bus_probe(void)
{
	return 1;
}

int
l4bus_match(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct l4bus_attach_arg *lba = aux;

	if (strcmp(lba->lba_busname, cf->cf_driver->cd_name))
		return (0);

	return 1;
}

void
l4bus_attach(struct device *parent, struct device *self, void *aux)
{
	struct l4bus_attach_arg attach_arg;

	printf("\n");

        /* l4ser */
	attach_arg.lba_busname = "l4ser";
	config_found(self, &attach_arg, NULL);
}
