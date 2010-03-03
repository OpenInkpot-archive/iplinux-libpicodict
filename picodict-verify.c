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

#include "libpicodict.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: test-picodict <.index>\n");
        return 1;
    }

    char *s = strdup(argv[1]);
    char *s2 = strrchr(s, '.');
    *s2 = 0;

    char dz[1024];
    sprintf(dz, "%s.dict.dz", s);

    pd_sort_mode m = pd_validate(argv[1], dz);
    printf("%d\n", m);

    return 0;
}

