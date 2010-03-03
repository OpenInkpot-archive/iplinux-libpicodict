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
#ifndef PICODICT_H
#define PICODICT_H

#include <sys/types.h>

struct pd_dictionary;
struct pd_result;

typedef struct pd_dictionary pd_dictionary;
typedef struct pd_result pd_result;

/* -- Dictionary -- */

typedef enum {
    PICODICT_FIND_EXACT,
    PICODICT_FIND_STARTS_WITH,
} pd_find_mode;

typedef enum {
    PICODICT_DATA_MALFORMED = -2,
    PICODICT_SORT_UNKNOWN = -1,
    PICODICT_SORT_ALPHABET,
    PICODICT_SORT_SKIPUNALPHA,
} pd_sort_mode;

/*
 * Given index file and data file opens it and returns newly created
 * pd_dictionary. Returns NULL if there was error during opening the
 * dictionary. Sort mode should be obtained from pd_validate.
 *
 * Returned object is to be closed by passing into pd_close().
 */
pd_dictionary *
pd_open(const char *index_file, const char *data_file, pd_sort_mode sort_mode);

/*
 * Returns name of dictionary as stored inside it. Returned string is to be
 * freed by caller.
 */
char *
pd_name(pd_dictionary *d);

/*
 * Looks for given word and returns result. Result is to be freed by passing
 * pd_result_free().
 *
 * Returns NULL if result is empty.
 */
pd_result *
pd_find(pd_dictionary *d, const char *text, pd_find_mode options);

/*
 * Deallocates all resources bound to passed dictionary object.
 */
void
pd_close(pd_dictionary *d);

/* -- Result set -- */

/*
 * Returns dictionary article from result.
 */
const char *
pd_result_article(pd_result *r, size_t *size);

/*
 * Advances to next dictionary article from result. Returned is new pd_result
 * object, don't forget to free passed one when finished working with it.
 *
 * If there is no next dictionary article, NULL is returned.
 */
pd_result *
pd_result_next(pd_result *r);

/*
 * Frees all resources bound to dictionary result object.
 */
void
pd_result_free(pd_result *r);

/* -- Testing validity of files -- */

/*
 * Applications ought to check validity of dictionaries at first time and obtain
 * sorting function to be passed later to pd_open().
 *
 * It is strongly advised to checksum both index and data file and re-validate
 * dictionary if contents changes.
 */

typedef enum {
    PICODICT_VALIDATE_OK,
    PICODICT_VALIDATE_FORMAT, /* Invalid file format */
    PICODICT_VALIDATE_SORTING_UNKNOWN /* Unable to guess sorting function */
} pd_validation_result;

/*
 * Given index file and data file validates it and obtains sort mode to be used
 * sorting functionwith this dictionary.
 */
pd_sort_mode
pd_validate(const char *index_file, const char *data_file);

#endif
