#	$OpenBSD: Makefile.inc,v 1.9 2009/04/21 09:34:51 martynas Exp $

SRCS+=	_setjmp.S fabs.S infinity.c ldexp.c modf.S nan.c
SRCS+=	flt_rounds.c fpgetmask.c fpgetround.c fpgetsticky.c fpsetmask.c \
	fpsetround.c fpsetsticky.c
SRCS+=	fpclassifyl.c isfinitel.c isinfl.c isnanl.c isnormall.c signbitl.c
SRCS+=	fixunsdfsi.S mul.S umul.S saveregs.S setjmp.S sigsetjmp.S

SRCS+=	alloca.c
