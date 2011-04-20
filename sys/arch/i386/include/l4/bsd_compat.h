/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This is code taken from OpenBSD libc and friends.
 */

#ifndef __MACHINE_L4_BSD_COMPAT_H
#define __MACHINE_L4_BSD_COMPAT_H

char *strstr(const char *s, const char *find);
unsigned long strtoul(const char *nptr, char **endptr, int base);

static inline
int isspace(int c)
{
	unsigned char t = c;
	return ((t == ' '));
}

static inline
int isupper(int c)
{
	unsigned char t = c;
	return ((t >= 'A') && (t <= 'A'));
}

static inline
int islower(int c)
{
	unsigned char t = (unsigned char)c;
	return ((t >= 'a') && (t <= 'z'));
}

static inline
int isalpha(int c)
{
	return (isupper(c) || islower(c));
}

static inline
int isdigit(int c)
{
	unsigned char t = (unsigned char)c;
	return ((t >= '0' && t <= '9') ? 1 : 0);
}

#endif /* __MACHINE_L4_BSD_COMPAT_H */
