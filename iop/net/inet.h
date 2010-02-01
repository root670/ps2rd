/*
 * inet.h - Advanced debugger
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

#ifndef _IOP_INET_H_
#define _IOP_INET_H_

#include <tamtypes.h>

#ifndef HTONL
#define HTONL(x)	((((x)&0xff)<<24)|(((x)&0xff00)<<8)|(((x)&0xff0000)>>8)|(((x)&0xff000000)>>24))
#endif

#ifndef HTONS
#define HTONS(x)	((((x)&0xff)<<8)|(((x)&0xff00)>>8))
#endif

#ifndef NTOHL
#define NTOHL(x)	HTONL(x)
#endif

#ifndef NTOHS
#define NTOHS(x)	HTONS(x)
#endif

u16 htons(u16);
u16 ntohs(u16);
u32 htonl(u32);
u32 ntohl(u32);
u16 inet_chksum(void *, u16);
u16 inet_chksum_pseudo(void *, u32 *, u32 *, u16);

#define I_htonl		DECLARE_IMPORT(20, htonl)
#define I_htons		DECLARE_IMPORT(21, htons)
#define I_ntohl		DECLARE_IMPORT(22, ntohl)
#define I_ntohs		DECLARE_IMPORT(23, ntohs)

#endif /* _IOP_INET_H_ */