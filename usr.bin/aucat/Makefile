#	$OpenBSD: Makefile,v 1.14 2010/07/31 08:48:01 ratchov Exp $

PROG=	aucat
SRCS=	aucat.c abuf.c aparams.c aproc.c dev.c midi.c file.c headers.c \
	siofile.c miofile.c sock.c pipe.c listen.c opt.c wav.c dbg.c
MAN=	aucat.1 midicat.1
LINKS=  ${BINDIR}/aucat ${BINDIR}/midicat
CFLAGS+= -Wall -Wstrict-prototypes -Wundef -DDEBUG
LDADD+=	-lsndio
.include <bsd.prog.mk>
