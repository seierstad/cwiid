/* Copyright (C) 2007 L. Donnie Smith <wiimote@abstrakraft.org>
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
 */

#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "wiimote_internal.h"

struct write_seq speaker_enable_seq[] = {
	{WRITE_SEQ_RPT, RPT_SPEAKER_ENABLE, (unsigned char *)"\x04", 1, 0},
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (unsigned char *)"\x04", 1, 0},
	{WRITE_SEQ_MEM, 0xA20009, (unsigned char *)"\x01", 1, WIIMOTE_RW_REG},
	{WRITE_SEQ_MEM, 0xA20001, (unsigned char *)"\x08", 1, WIIMOTE_RW_REG},
	{WRITE_SEQ_MEM, 0xA20001, (unsigned char *)"\x00\x00\x00\x0C\x40\x00\x00",
	                          7, WIIMOTE_RW_REG},
	{WRITE_SEQ_MEM, 0xA20008, (unsigned char *)"\x01", 1, WIIMOTE_RW_REG},
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (unsigned char *)"\x00", 1, 0}
};

struct write_seq speaker_disable_seq[] = {
	{WRITE_SEQ_RPT,   RPT_SPEAKER_MUTE, (unsigned char *)"\x04", 1, 0},
	{WRITE_SEQ_RPT, RPT_SPEAKER_ENABLE, (unsigned char *)"\x00", 1, 0}
};



#define RPT_READ_REQ_LEN 6
int wiimote_read(struct wiimote *wiimote, unsigned int flags,
                 unsigned int offset, unsigned int len, unsigned char *data)
{
	unsigned char buf[RPT_READ_REQ_LEN];
	int ret = 0;
	int i;
	unsigned char address_flags;

	/* Lock wiimote rw access */
	if (pthread_mutex_lock(&wiimote->rw_mutex)) {
		wiimote_err("Error locking rw_mutex");
		return -1;
	}

	address_flags = flags & (WIIMOTE_RW_EEPROM | WIIMOTE_RW_REG);

	/* Compose read request packet */
	buf[0]=address_flags;
	buf[1]=(unsigned char)((offset>>16) & 0xFF);
	buf[2]=(unsigned char)((offset>>8) & 0xFF);
	buf[3]=(unsigned char)(offset & 0xFF);
	buf[4]=(unsigned char)((len>>8) & 0xFF);
	buf[5]=(unsigned char)(len & 0xFF);

	/* Setup read info */
	wiimote->rw_status = RW_PENDING;
	wiimote->read_buf = data;
	wiimote->read_len = len;
	wiimote->read_received = 0;

	/* Send read request packet */
	if (send_report(wiimote, 0, RPT_READ_REQ, RPT_READ_REQ_LEN, buf)) {
		wiimote_err("Error sending read request");
		ret = -1;
	}
	/* Lock rw_cond_mutex  */
	else if (pthread_mutex_lock(&wiimote->rw_cond_mutex)) {
		wiimote_err("Error locking rw_cond_mutex");
		ret = -1;
	}
	else {
		/* Wait on condition, signalled by wiimote_int_listen */
		while ((!ret) && (wiimote->rw_status == RW_PENDING)) {
			if (pthread_cond_wait(&wiimote->rw_cond,
			                      &wiimote->rw_cond_mutex)) {
				wiimote_err("Error waiting on rw_cond");
				ret = -1;
			}
		}
		/* Unlock rw_cond_mutex */
		if (pthread_mutex_unlock(&wiimote->rw_cond_mutex)) {
			wiimote_err("Error unlocking rw_cond_mutex: deadlock warning");
		}

		/* Check status */
		if (wiimote->rw_status == RW_READY) {
			ret = 0;
		}
		else {
			ret = -1;
		}
	}

	/* Clear rw_status */
	wiimote->rw_status = RW_NONE;

	/* Unlock rw_mutex */
	if (pthread_mutex_unlock(&wiimote->rw_mutex)) {
		wiimote_err("Error unlocking rw_mutex: deadlock warning");
	}

	if (flags & WIIMOTE_RW_DECODE) {
		for (i=0; i < len; i++) {
			data[i] = DECODE(data[i]);
		}
	}

	return ret;
}

#define RPT_WRITE_LEN 21
int wiimote_write(struct wiimote *wiimote, unsigned int flags,
                  unsigned int offset, unsigned int len, unsigned char *data)
{
	unsigned char buf[RPT_WRITE_LEN];
	unsigned int sent=0;
	int ret = 0;

	/* Lock wiimote rw access */
	if (pthread_mutex_lock(&wiimote->rw_mutex)) {
		wiimote_err("Error locking rw_mutex");
		return -1;
	}

	/* Compose write packet header */
	buf[0]=flags;

	/* Send packets */
	while (!ret && (sent<len)) {
		buf[1]=(unsigned char)(((offset+sent)>>16) & 0xFF);
		buf[2]=(unsigned char)(((offset+sent)>>8) & 0xFF);
		buf[3]=(unsigned char)((offset+sent) & 0xFF);
		if (len-sent >= 0x10) {
			buf[4]=(unsigned char)0x10;
		}
		else {
			buf[4]=(unsigned char)(len-sent);
		}
		memcpy(buf+5, data+sent, buf[4]);
		wiimote->rw_status = RW_PENDING;
		if (send_report(wiimote, 0, RPT_WRITE, RPT_WRITE_LEN, buf)) {
			wiimote_err("Error sending write");
			ret = -1;
		}
		/* Lock rw_cond_mutex  */
		else if (pthread_mutex_lock(&wiimote->rw_cond_mutex)) {
			wiimote_err("Error locking rw_cond_mutex");
			ret = -1;
		}
		else {
			/* Wait on condition, signalled by wiimote_int_listen */
			while ((!ret) && (wiimote->rw_status == RW_PENDING)) {
				if (pthread_cond_wait(&wiimote->rw_cond,
				                      &wiimote->rw_cond_mutex)) {
					wiimote_err("Error waiting on rw_cond");
					ret = -1;
				}
			}
			/* Unlock rw_cond_mutex */
			if (pthread_mutex_unlock(&wiimote->rw_cond_mutex)) {
				wiimote_err("Error unlocking rw_cond_mutex: deadlock warning");
			}

			/* Check status */
			if (wiimote->rw_status == RW_READY) {
				ret = 0;
			}
			else {
				ret = -1;
			}
		}
		sent+=buf[4];
	}

	/* Clear rw_status */
	wiimote->rw_status = RW_NONE;

	/* Unlock rw_mutex */
	if (pthread_mutex_unlock(&wiimote->rw_mutex)) {
		wiimote_err("Error unlocking rw_mutex: deadlock warning");
	}

	return ret;
}

#define SOUND_BUF_LEN	21
int wiimote_beep(wiimote_t *wiimote)
{
	/* unsigned char buf[SOUND_BUF_LEN] = { 0xA0, 0xCC, 0x33, 0xCC, 0x33,
	    0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x33,
	    0xCC, 0x33, 0xCC, 0x33}; */
	unsigned char buf[SOUND_BUF_LEN] = { 0xA0, 0xC3, 0xC3, 0xC3, 0xC3,
	    0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3,
	    0xC3, 0xC3, 0xC3, 0xC3};
	int i;
	int ret = 0;
	pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
	struct timespec t;

	if (exec_write_seq(wiimote, SEQ_LEN(speaker_enable_seq),
	                   speaker_enable_seq)) {
		wiimote_err("Error on speaker enable");
		ret = -1;
	}

	pthread_mutex_lock(&timer_mutex);

	for (i=0; i<100; i++) {
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_nsec += 10204081;
		/* t.tv_nsec += 7000000; */
		if (send_report(wiimote, 0, RPT_SPEAKER_DATA, SOUND_BUF_LEN, buf)) {
		 	printf("%d\n", i);
			wiimote_err("Error on speaker data");
			ret = -1;
			break;
		}
		/* TODO: I should be shot for this, but hey, it works.
		 * longterm - find a better wait */
		pthread_cond_timedwait(&timer_cond, &timer_mutex, &t);
	}

	pthread_mutex_unlock(&timer_mutex);

	if (exec_write_seq(wiimote, SEQ_LEN(speaker_disable_seq),
	                   speaker_disable_seq)) {
		wiimote_err("Error on speaker disable");
		ret = -1;
	}

	return ret;
}
