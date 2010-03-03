/*
 * libpicodict - dictd dictionary format reading library
 *
 * Copyright © 2010 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * Substitution for le16toh for older glibc
 */
#include <sys/param.h>
#include <stdint.h>

uint16_t le16toh(uint16_t arg)
{
#if BYTE_ORDER == LITTLE_ENDIAN
    return arg;
#elif BYTE_ORDER == BIG_ENDIAN
    return __bswap_16(arg);
#else
#  error Unknown byte order!
#endif
}
