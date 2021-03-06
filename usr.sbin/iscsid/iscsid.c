/*	$OpenBSD: iscsid.c,v 1.3 2010/09/25 16:20:06 sobrado Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <err.h>
#include <event.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

void		main_sig_handler(int, short, void *);
__dead void	usage(void);

struct initiator *initiator;

int
main(int argc, char *argv[])
{
	struct event ev_sigint, ev_sigterm, ev_sighup;
	struct passwd *pw;
	char *ctrlsock = ISCSID_CONTROL;
	char *vscsidev = ISCSID_DEVICE;
	int ch, debug = 0;

	log_init(1);    /* log to stderr until daemonized */

	while ((ch = getopt(argc, argv, "dn:s:")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			vscsidev = optarg;
			break;
		case 's':
			ctrlsock = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	log_init(debug);
	if (!debug)
		daemon(1, 0);
	log_info("startup");

	event_init();
	vscsi_open(vscsidev);
	if (control_init(ctrlsock) == -1)
		fatalx("control socket setup failed");

	/* chroot and drop to iscsid user */
	if ((pw = getpwnam(ISCSID_USER)) == NULL)
		errx(1, "unknown user %s", ISCSID_USER);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	if (control_listen() == -1)
		fatalx("control socket listen failed");

	initiator = initiator_init();

	event_dispatch();

	/* CLEANUP XXX */
	control_cleanup(ctrlsock);
	initiator_cleanup(initiator);
	log_info("exiting.");
	return 0;
}

/* ARGSUSED */
void
main_sig_handler(int sig, short event, void *arg)
{
	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
	case SIGHUP:
		event_loopexit(NULL);
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-d] [-n device] [-s socket]\n",
	    __progname);
	exit(1);
}

void
iscsid_ctrl_dispatch(void *ch, struct pdu *pdu)
{
	struct ctrlmsghdr *cmh;
	struct initiator_config *ic;
	struct session_config *sc;
	struct session *s;

	cmh = pdu_getbuf(pdu, NULL, 0);
	if (cmh == NULL)
		goto done;

	switch (cmh->type) {
	case CTRL_INITIATOR_CONFIG:
log_debug("CTRL_INITIATOR_CONFIG");
		if (cmh->len[0] != sizeof(*ic)) {
			log_warnx("CTRL_INITIATOR_CONFIG bad size");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			break;
		}
		ic = pdu_getbuf(pdu, NULL, 1);
		bcopy(ic, &initiator->config, sizeof(initiator->config));
		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	case CTRL_SESSION_CONFIG:
log_debug("CTRL_SESSION_CONFIG");
		if (cmh->len[0] != sizeof(*sc)) {
			log_warnx("CTRL_INITIATOR_CONFIG bad size");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			break;
		}
		sc = pdu_getbuf(pdu, NULL, 1);
		if (cmh->len[1])
			sc->TargetName = pdu_getbuf(pdu, NULL, 2);
		else if (sc->SessionType != SESSION_TYPE_DISCOVERY) {
log_debug("no TargetName but not discovery");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			goto done;
		} else
			sc->TargetName = NULL;
		if (cmh->len[2])
			sc->InitiatorName = pdu_getbuf(pdu, NULL, 3);
		else
			sc->InitiatorName = NULL;

log_debug("session %s to %s", sc->SessionName, log_sockaddr(&sc->connection.TargetAddr));
		s = session_find(initiator, sc->SessionName);
		if (s == NULL) {
			s = session_new(initiator, sc->SessionType);
			if (s == NULL) {
				control_compose(ch, CTRL_FAILURE, NULL, 0);
				goto done;
			}
		}

		session_config(s, sc);

		if (s->state == SESS_FREE) {
			log_debug("new connection to %s",
			    log_sockaddr(&s->config.connection.TargetAddr));
			conn_new(s, &s->config.connection);
		}

		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	default:
		log_warnx("unknown control message type %d", cmh->type);
		control_compose(ch, CTRL_FAILURE, NULL, 0);
		break;
	}

done:
	pdu_free(pdu);
}
