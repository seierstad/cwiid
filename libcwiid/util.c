/* Copyright (C) 2007 L. Donnie Smith <cwiid@abstrakraft.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ChangeLog:
 *  2007-04-09 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * renamed wiimote to libcwiid, renamed structures accordingly
 *
 *  2007-04-08 Petter Reinholdtsen <pere@hungry.com>
 *  * fixed signed/unsigned comparison warning in send_report and
 *    exec_write_seq
 *
 *  2007-04-01 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * removed cwiid_findfirst (moved to bluetooth.c)
 *
 *  2007-03-27 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * moved cwiid_findfirst to bluetooth.c
 *
 *  2007-03-14 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * audited error checking (coda and error handler sections)
 *
 *  2007-03-05 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * created cwiid_err_func variable
 *  * created cwiid_err_default
 *  * added wiimote parameter to cwiid_err definition and calls
 *
 *  2007-03-01 L. Donnie Smith <cwiid@abstrakraft.org>
 *  * Initial ChangeLog
 *  * type audit (stdint, const, char booleans)
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cwiid_internal.h"

static cwiid_err_t cwiid_err_default;

static cwiid_err_t *cwiid_err_func = &cwiid_err_default;

int cwiid_set_err(cwiid_err_t *err)
{
	/* TODO: assuming pointer assignment is atomic operation */
	/* if it is, and the user doesn't care about race conditions, we don't
	 * either */
	cwiid_err_func = err;
	return 0;
}

static void cwiid_err_default(int id, const char *str, ...)
{
	va_list ap;

	va_start(ap, str);
	vfprintf(stderr, str, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

void cwiid_err(struct wiimote *wiimote, const char *str, ...)
{
	va_list ap;

	if (cwiid_err_func) {
		va_start(ap, str);
		if (wiimote) {
			(*cwiid_err_func)(wiimote->id, str, ap);
		}
		else {
			(*cwiid_err_func)(-1, str, ap);
		}
		va_end(ap);
	}
}

int verify_handshake(struct wiimote *wiimote)
{
	unsigned char handshake;
	if (read(wiimote->ctl_socket, &handshake, 1) != 1) {
		cwiid_err(wiimote, "Error on read handshake");
		return -1;
	}
	else if ((handshake & BT_TRANS_MASK) != BT_TRANS_HANDSHAKE) {
		cwiid_err(wiimote, "Handshake expected, non-handshake received");
		return -1;
	}
	else if ((handshake & BT_PARAM_MASK) != BT_PARAM_SUCCESSFUL) {
		cwiid_err(wiimote, "Non-successful handshake");
		return -1;
	}

	return 0;
}

#define SEND_RPT_BUF_LEN	23
int send_report(struct wiimote *wiimote, uint8_t flags, uint8_t report,
                size_t len, const void *data)
{
	unsigned char buf[SEND_RPT_BUF_LEN];

	if ((len+2) > SEND_RPT_BUF_LEN) {
		return -1;
	}

	buf[0] = BT_TRANS_SET_REPORT | BT_PARAM_OUTPUT;
	buf[1] = report;
	memcpy(buf+2, data, len);
	if (!(flags & SEND_RPT_NO_RUMBLE)) {
		buf[2] |= wiimote->led_rumble_state & 0x01;
	}

	if (write(wiimote->ctl_socket, buf, len+2) != (ssize_t)(len+2)) {
		return -1;
	}
	else if (verify_handshake(wiimote)) {
		return -1;
	}

	return 0;
}

int exec_write_seq(struct wiimote *wiimote, unsigned int len,
                   struct write_seq *seq)
{
	unsigned int i;

	for (i=0; i < len; i++) {
		switch (seq[i].type) {
		case WRITE_SEQ_RPT:
			if (send_report(wiimote, seq[i].flags, seq[i].report_offset,
			                seq[i].len, seq[i].data)) {
				return -1;
			}
			break;
		case WRITE_SEQ_MEM:
			if (cwiid_write(wiimote, seq[i].flags, seq[i].report_offset,
			                  seq[i].len, seq[i].data)) {
				return -1;
			}
			break;
		}
	}

	return 0;
}

void free_mesg_array(struct mesg_array *array)
{
	int i;

	for (i=0; i < array->count; i++) {
		free(array->mesg[i]);
	}
	free(array);
}
