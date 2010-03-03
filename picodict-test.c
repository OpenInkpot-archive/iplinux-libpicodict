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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpicodict.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: test-picodict <.index> <word> [<word>...]\n");
        exit(1);
    }

    char *s = strdup(argv[1]);
    char *s2 = strrchr(s, '.');
    *s2 = 0;

    char dz[1024];
    sprintf(dz, "%s.dict.dz", s);

    pd_dictionary *d = pd_open(argv[1], dz, PICODICT_SORT_ALPHABET);

    char *n = pd_name(d);

    printf("'%s'\n\n", n);

    free(n);

    int i;
    for(i = 2; i < argc; ++i) {
        printf("%s\n", argv[i]);

        pd_result *r = pd_find(d, argv[i], PICODICT_FIND_STARTS_WITH);

        while (r) {
            size_t alen;
            const char *a = pd_result_article(r, &alen);
            printf("%.*s\n----------------------------------------\n", alen, a);

            pd_result *next = pd_result_next(r);
            pd_result_free(r);
            r = next;
        }
    }
}
