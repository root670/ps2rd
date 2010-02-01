/*
 * udp.h - lightweight UDP implementation
 *
 * Copyright (C) 2009-2010 jimmikaelkael <jimmikaelkael@wanadoo.fr>
 *
 * This file is part of ps2rd, the PS2 remote debugger.
 *
 * ps2rd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ps2rd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ps2rd.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _IOP_UDP_H_
#define _IOP_UDP_H_

#include <tamtypes.h>
#include <irx.h>

#include <intrman.h>
#include <thbase.h>
#include <thsemap.h>
#include <sysclib.h>
#include <stdio.h>

#include "linux/if_ether.h"
#include "linux/ip.h"

/* flags for udp_open */
#define UDP_ACTIVE_CONN		1
#define UDP_PASSIVE_CONN	2

void udp_init(u32 ip_addr_dst, u32 ip_addr_src);
int udp_output(u16 ip_port_src, u16 ip_port_dst, void *buf, int size);
int udp_input(void *buf, int size);
int udp_open(int *s, u16 ip_port, int flags);
int udp_close(int s);
int udp_recv(int s, void *buf, int size);
int udp_send(int s, void *buf, int size);

#define I_udp_init		DECLARE_IMPORT(10, udp_init)
#define I_udp_output		DECLARE_IMPORT(11, udp_output)
#define I_udp_input		DECLARE_IMPORT(12, udp_input)
#define I_udp_open		DECLARE_IMPORT(13, udp_open)
#define I_udp_close		DECLARE_IMPORT(14, udp_close)
#define I_udp_recv		DECLARE_IMPORT(15, udp_recv)
#define I_udp_send		DECLARE_IMPORT(16, udp_send)

#endif /* _IOP_UDP_H_ */