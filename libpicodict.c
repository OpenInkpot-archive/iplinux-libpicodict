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

#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <zlib.h>

/* Older glibc don't have it */
#ifndef le16toh
static uint16_t
le16toh(uint16_t arg)
{
#if BYTE_ORDER == LITTLE_ENDIAN
    return arg;
#elif BYTE_ORDER == BIG_ENDIAN
    return __bswap_16(arg);
#else
#  error Unknown byte order!
#endif
}
#endif


#define CHUNK_CACHE_SIZE 3

typedef struct {
    int next_id;
    int id[CHUNK_CACHE_SIZE];
    char *data[CHUNK_CACHE_SIZE];
} _pd_chunk_cache;

struct pd_dictionary {
    void *index;
    size_t index_size;

    void *data;
    size_t data_size;

    pd_sort_mode mode;

    /* Compressed (.dz) dictionaries */
    bool compressed;
    size_t chunk_length;
    size_t chunk_count;
    size_t *chunk_offsets;
    z_stream z;
    _pd_chunk_cache chunk_cache;
};

typedef struct {
    const char *lower;
    const char *upper;
} _pd_interval;

struct pd_result {
    pd_dictionary *dict;

    _pd_interval result;

    char *article;
    size_t article_length;
    bool article_allocated;
};

typedef int (*_pd_cmp)(const char *lhs, const char *rhs);

/* -- Search -- */

/*
 * find_entry() searches for interval of entries starting with given prefix
 * ('yr' => 'yraft' .. 'yronne')
 *
 * 0. Entries are supposed to be sorted wrt passed comparison function.
 *
 * 1. Binary search is performed looking for entry (E) which starts with a given
 * prefix.
 *
 * 1a. If such entry is not found, then there are no entries with given prefix
 * in dictionary. Stop.
 *
 * 1b. Else, it is known there are matching entries in dictionary, starting
 * somewhere before found entry and finishing somewhere after (it is probable
 * that start or end of interval is on entry found).
 *
 * 2. First entry (F) that matches given prefix is binary-searched in [start, E]
 * interval. It is known that such entry exists.
 *
 * 3. First entry (L) that does not match given prefix is binary-searched in (E,
 * end] interval. Such entry is not guaranteed to exist, so "first entry after
 * the end" may be returned instead.
 *
 * 4. [F, L) result is returned.
 *
 * Result is returned in "raw" form, that is, just the region in index
 * file. It's up to a caller to actually parse lines and locate dictionary
 * articles.
 */

/*
 * Check whether str starts with prefix
 */
static int
_pd_strprefixcmp(const unsigned char *prefix, const unsigned char *str)
{
    while (*prefix) {
        if (*str == '\t')
            return 1;
        if (*prefix < *str)
            return -1;
        if (*prefix > *str)
            return 1;
        str++;
        prefix++;
    }
    return 0;
}

/*
 * Checks whether str starts with prefix, ignoring all characters except
 * alphanumeric and whitespace.
 */
static int
_pd_strprefixdictcmp(const unsigned char* prefix, const unsigned char *str)
{
    while (*prefix) {
        /* UTF-8 is assumed */
        while (*prefix && !isblank(*prefix)
               && !isalnum(*prefix) && *prefix < 0x80) prefix++;
        while (*str != '\t' && !isblank(*str)
               && !isalnum(*str) && *str < 0x80) str++;
        if (!*prefix)
            break;

        if (*str == '\t')
            return 1;
        unsigned char prefixc = tolower(*prefix);
        unsigned char strc = tolower(*str);
        if (prefixc < strc)
            return -1;
        if (prefixc > strc)
            return 1;
        str++;
        prefix++;
    }
    return 0;
}

/*
 * Checks whether lhs equals rhs. rhs and lhs may be \t-terminated.
 */
static int
_pd_strcmp(const unsigned char *lhs, const unsigned char *rhs)
{
    for (;;) {
        if ((!*lhs || *lhs == '\t') && (!*rhs || *rhs == '\t')) return 0;
        if (!*lhs || *lhs == '\t') return -1;
        if (!*rhs || *rhs == '\t') return 1;
        if (*lhs < *rhs) return -1;
        if (*lhs > *rhs) return 1;
        lhs++;
        rhs++;
    }
}

/*
 * Checks whether lhs equals rhs, ignoring all characters except alphanumeric
 * and whitespace.
 *
 * Note that lhs and rhs may be \t-terminated.
 */
static int
_pd_strdictcmp(const unsigned char *lhs, const unsigned char *rhs)
{
    for (;;) {
        /* UTF-8 is assumed */
        while (*lhs && *lhs != '\t' && *lhs < 0x80
               && !isblank(*lhs) && !isalnum(*lhs)) lhs++;
        while (*rhs && *rhs != '\t' && *rhs < 0x80
               && !isblank(*rhs) && !isalnum(*rhs)) rhs++;

        if ((!*lhs || *lhs == '\t') && (!*rhs || *rhs == '\t'))
            return 0;

        if (!*lhs || *lhs == '\t')
            return -1;

        if (!*rhs || *rhs == '\t')
            return 1;

        unsigned char lhsc = tolower(*lhs);
        unsigned char rhsc = tolower(*rhs);

        if (lhsc != rhsc)
            return lhsc < rhsc ? -1 : 1;

        lhs++;
        rhs++;
    }
}

static const char *
nextline(const char *c)
{
    return strchr(c, '\n') + 1;
}

static const char *
lower_bound(_pd_cmp cmp, const char *prefix, const char *start, const char *end)
{
    const char *middle = start + (end - start)/2;
    /* looking for the start of line */
    while (middle > start && middle[-1] != '\n') middle--;

    const char *next = nextline(middle);

    /* If we've got a single line, then we've found it */
    if (middle == start && next == end)
        return middle;

    int c = (*cmp)(prefix, middle);
    if (c > 0)
        return lower_bound(cmp, prefix, next, end);

    /* Check that middle is not last line. Without this check we'd go into
     * infinite loop, as we'd call ourself with the same arguments. To avoid
     * this situation, either terminate search or call itself again without last
     * line.
     */
    if (next == end) {
         const char *prevline = middle - 1; /* skip \n on previous line */
         while (prevline > start && prevline[-1] != '\n') prevline--;

         /*
          * Now check if prevline matches prefix. If it is, then drop last line,
          * else we've found lower bound
          */
         int c = (*cmp)(prefix, prevline);
         if (c > 0)
             return middle;

         return lower_bound(cmp, prefix, start, middle);
    }

    return lower_bound(cmp, prefix, start, next);
}

static const char *
upper_bound(_pd_cmp cmp, const char *prefix, const char *start, const char *end)
{
    if (start == end)
        return start;

    const char *middle = start + (end - start)/2;
    while (middle > start && middle[-1] != '\n') middle--;

    const char *next = nextline(middle);

    int c = (*cmp)(prefix, middle);
    if (c == 0)
        return upper_bound(cmp, prefix, next, end);

    /*
     * Check that middle is not a last line. Without this check we'd go into
     * infinite loop. To avoid this situation either terminate search or call
     * itself without last line.
     */
    if (next == end) {
        const char* prevline = middle - 1; /* skip \n on previous line */
        while (prevline > start && prevline[-1] != '\n') prevline--;

        int c = (*cmp)(prefix, prevline);
        if (c == 0)
            return middle;

        return upper_bound(cmp, prefix, start, middle);
    }

    return upper_bound(cmp, prefix, start, next);
}

static _pd_interval
find_entry(_pd_cmp cmp, const char *prefix, const char *start, const char *end)
{
    _pd_interval res = {};

    while (start < end) {
        const char *middle = start + (end - start)/2;

        /* looking for the start of line */
        while (middle > start && middle[-1] != '\n') middle--;

        const char *next = nextline(middle);

        int c = (*cmp)(prefix, middle);
        if (c == 0) {
            res.lower = lower_bound(cmp, prefix, start, next);
            res.upper = upper_bound(cmp, prefix, next, end);
            break;
        }

        if (c > 0) {
            start = next;
        } else {
            end = middle;
        }
    }

    return res;
}

/* -- Dictionary manipulation -- */

/*
 * Format of dict.dz file.
 *
 * Gzip header:
 *
 *       +---+---+---+---+---+---+---+---+---+---+
 *       |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
 *       +---+---+---+---+---+---+---+---+---+---+
 *
 *    (if FLG.FEXTRA set)
 *
 *       +---+---+=================================+
 *       | XLEN  |...XLEN bytes of "extra field"...| (more-->)
 *       +---+---+=================================+
 *
 *    (if FLG.FNAME set)
 *
 *       +=========================================+
 *       |...original file name, zero-terminated...| (more-->)
 *       +=========================================+
 *
 *    (if FLG.FCOMMENT set)
 *
 *       +===================================+
 *       |...file comment, zero-terminated...| (more-->)
 *       +===================================+
 *
 *    (if FLG.FHCRC set)
 *
 *       +---+---+
 *       | CRC16 |
 *       +---+---+
 *
 * Data:
 *
 *       +=======================+
 *       |...compressed blocks...| (more-->)
 *       +=======================+
 *
 * Footer:
 *
 *         0   1   2   3   4   5   6   7
 *       +---+---+---+---+---+---+---+---+
 *       |     CRC32     |     ISIZE     |
 *       +---+---+---+---+---+---+---+---+
 *
 * Format of extra dz field (FLG.EXTRA):
 *
 *      +---+---+---+---+---+---+---+---+---+---+---+---+
 *      | XLEN  |SI1|SI2| SLEN  | SVER  | CHLEN | CHCNT | (more-->)
 *      +---+---+---+---+---+---+---+---+---+---+---+---+
 *      +================================================+
 *      |...CHCNT chunk compressed sizes, 2 bytes each...|
 *      +================================================+
 *
 * where
 *
 *      SI1 = 'R', 0x52
 *      SI2 = 'A', 0x41
 *      SLEN = XLEN - 4
 *      SVER = 1
 *
 *      CHLEN is a size of unpacked chunk.
 *      CHCNT is count of chunks in file
 *
 *      Sizes of compressed chunks follow from first to CHCNT one.
 *
 *      Each chunk can be decompressed individually.
 */

enum {
    GZIP_ID1 = 0x1f,
    GZIP_ID2 = 0x8b,

    GZIP_FTEXT = 1,
    GZIP_FHCRC = 2,
    GZIP_FEXTRA = 4,
    GZIP_FNAME = 8,
    GZIP_FCOMMENT = 16,

    DZIP_SI1 = 0x52,
    DZIP_SI2 = 0x41,
};

typedef enum {
    DZ_NOT_FOUND = -1,
    DZ_OK,
    DZ_ERROR,
} dz_parse_result;

static dz_parse_result
parse_dz_header(pd_dictionary *dict, const unsigned char *file, size_t size)
{
    if (size < 12)
        return DZ_NOT_FOUND;

    int compression = file[2];
    int flags = file[3];
    unsigned xlen = le16toh(*(unsigned short *)(file + 10));

    /* Basic info */

    if (file[0] != GZIP_ID1 || file[1] != GZIP_ID2 || compression != 8)
        return DZ_NOT_FOUND;

    /* 'extra' field */

    if (!(flags & GZIP_FEXTRA))
        return DZ_ERROR;

    if (size < 12 + xlen)
        return DZ_ERROR;

    if (file[12] != DZIP_SI1 || file[13] != DZIP_SI2)
        return DZ_ERROR;

    unsigned slen = le16toh(*(unsigned short *)(file + 14));
    if (slen != xlen - 4)
        return DZ_ERROR;

    unsigned sver = le16toh(*(unsigned short *)(file + 16));
    if (sver != 1)
        return DZ_ERROR;

    dict->chunk_length = le16toh(*(unsigned short *)(file + 18));
    dict->chunk_count = le16toh(*(unsigned short *)(file + 20));

    /* skipping various header stuff */

    int data_offset = 12 + xlen; /* header + extra header */
    if (flags & GZIP_FNAME) {
        while (data_offset < size && file[data_offset] != '\0') data_offset++;
        data_offset++;
        if (data_offset >= size)
            return DZ_ERROR;
    }
    if (flags & GZIP_FCOMMENT) {
        while (data_offset < size && file[data_offset] != '\0') data_offset++;
        data_offset++;
        if (data_offset >= size)
            return DZ_ERROR;
    }
    if (flags & GZIP_FHCRC)
        data_offset += 2;

    if (data_offset >= size)
        return DZ_ERROR;

    /* chunks extra data */

    dict->chunk_offsets = malloc((dict->chunk_count+1) * sizeof(int));
    for (int i = 0; i < dict->chunk_count; ++i) {
        unsigned chunk_len = le16toh(*(unsigned short *)(file + 22 + 2*i));
        dict->chunk_offsets[i] = data_offset;
        data_offset += chunk_len;
    }
    dict->chunk_offsets[dict->chunk_count] = data_offset;

    if (data_offset >= size + 1) { /* data_offset might be == size */
        free(dict->chunk_offsets);
        return DZ_ERROR;
    }

    return DZ_OK;
}

/*
 * Returns NULL and sets errno on error
 */
static void *
_mmap_ro(const char *filename, size_t *size)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) == -1)
        goto err;

    void *ptr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
        goto err;

    close(fd);
    *size = st.st_size;
    return ptr;

err:
    close(fd);
    return NULL;
}

pd_dictionary *
pd_open(const char *index_file, const char *data_file, pd_sort_mode mode)
{
    pd_dictionary *dict = calloc(1, sizeof(pd_dictionary));
    if (!dict)
        return NULL;

    dict->mode = mode;

    dict->index = _mmap_ro(index_file, &dict->index_size);
    if (!dict->index)
        goto err;

    dict->data = _mmap_ro(data_file, &dict->data_size);
    if (!dict->data)
        goto err2;

    dz_parse_result res = parse_dz_header(dict, dict->data, dict->data_size);
    if (res == DZ_ERROR)
        goto err3;

    if (res == DZ_OK) {
        dict->z.zalloc = Z_NULL;
        dict->z.zfree = Z_NULL;
        dict->z.opaque = Z_NULL;
        dict->z.next_in = dict->data;
        dict->z.avail_in = dict->data_size;
        int ret = inflateInit2(&dict->z, -15);
        if (ret != Z_OK)
            goto err3;
        dict->compressed = true;

        for(int i = 0; i < CHUNK_CACHE_SIZE; ++i)
            dict->chunk_cache.id[i] = -1;
    }

    return dict;

err3:
    free(dict->chunk_offsets);
    munmap(dict->data, dict->data_size);
err2:
    munmap(dict->index, dict->index_size);
err:
    free(dict);
    return NULL;
}

static void
_pd_chunk_cache_free(_pd_chunk_cache* cache)
{
    for (int i = 0; i < CHUNK_CACHE_SIZE; ++i)
        if (cache->id[i] != -1)
            free(cache->data[i]);
}

void
pd_close(pd_dictionary *dict)
{
    munmap(dict->index, dict->index_size);
    munmap(dict->data, dict->data_size);

    if (dict->compressed) {
        free(dict->chunk_offsets);
        inflateEnd(&dict->z);
        _pd_chunk_cache_free(&dict->chunk_cache);
    }

    free(dict);
}

/* -- Resultset -- */

static bool
is_base64_sym(int c)
{
    return ('A' <= c && c <= 'Z')
        || ('a' <= c && c <= 'z')
        || ('0' <= c && c <= '9')
        || c == '+' || c == '/';
}

static unsigned
base64_decode(const char *str)
{
    unsigned n = 0;
    for (char c = *str++; is_base64_sym(c); c = *str++) {
        n <<= 6;
        if ('A' <= c && c <= 'Z') n += c - 'A';
        else if ('a' <= c && c <= 'z') n += c - 'a' + 26;
        else if ('0' <= c && c <= '9') n += c - '0' + 52;
        else if (c == '+') n += 62;
        else if (c == '/') n += 63;
    }
    return n;
}

typedef struct {
    const char *name;
    const char *endname;
    size_t article_offset;
    size_t article_length;

    const char *nextline;
} pd_index_line;

static pd_index_line
_parse_index_line(const char *line, const char *end)
{
    pd_index_line ret = {};
    /* <name> \t <pos> \t <len> \n
     * ^      ^  ^     ^  ^     ^
     * |      |  |     |  |     |
     * |      |  pos   |  len   endlen
     * |      |        |
     * name   endname  endpos
     *
     * <pos> and <len> are base64-encoded strings
     */
    const char *name = line;
    const char *endname = line;
    while (endname < end && *endname != '\t') endname++;
    if (endname == end || endname == name)
        return ret;
    const char *pos = endname + 1;
    const char *endpos = pos;
    while (endpos < end && is_base64_sym(*endpos)) endpos++;
    if (endpos == end || endpos == pos || *endpos != '\t')
        return ret;
    const char *len = endpos + 1;
    const char *endlen = len;
    while (endlen < end && is_base64_sym(*endlen)) endlen++;
    if (endlen == end || endlen == len || *endlen != '\n')
        return ret;

    ret.name = line;
    ret.endname = endname;
    ret.article_offset = base64_decode(pos);
    ret.article_length = base64_decode(len);
    ret.nextline = endlen + 1;
    return ret;
}

static bool
_uncompress_chunk(pd_dictionary *dict, int chunk_id, char *out)
{
    dict->z.next_in = dict->data + dict->chunk_offsets[chunk_id];
    dict->z.avail_in =
        dict->chunk_offsets[chunk_id + 1] - dict->chunk_offsets[chunk_id];
    dict->z.next_out = (unsigned char *)out;
    dict->z.avail_out = dict->chunk_length;

    int ret = inflate(&dict->z, Z_PARTIAL_FLUSH);

    if (ret == Z_OK || ret == Z_STREAM_END)
        return true;

    return false;
}

/*
 * Result is stored in cache in pd_dictionary and may be overwritten by
 * subsequent call to _read_chunk.
 */
static char *
_read_chunk(pd_dictionary *dict, int chunk_id)
{
    _pd_chunk_cache *cache = &dict->chunk_cache;

    for (int i = 0; i < CHUNK_CACHE_SIZE; ++i)
        if (cache->id[i] == chunk_id)
            return cache->data[i];

    int next = (cache->next_id++) % CHUNK_CACHE_SIZE;

    if (cache->id[next] == -1)
        cache->data[next] = malloc(dict->chunk_length);

    if (_uncompress_chunk(dict, chunk_id, cache->data[next])) {
        cache->id[next] = chunk_id;
        return cache->data[next];
    } else {
        free(cache->data[next]);
        cache->data[next] = NULL;
        cache->id[next] = -1;
        return NULL;
    }
}

static int
_min(size_t a, size_t b)
{
    return a < b ? a : b;
}

static char *
_read_compressed(pd_dictionary *dict, size_t offset, size_t size)
{
    char *data = malloc(sizeof(char) * size);
    if (!data)
        return NULL;
    size_t data_offset = 0;

    while (size) {
        int chunk_id = offset / dict->chunk_length;
        size_t offset_in_chunk = offset % dict->chunk_length;
        char *chunk = _read_chunk(dict, chunk_id);
        if (!chunk) {
            free(data);
            return NULL;
        }

        size_t to_copy = _min(dict->chunk_length - offset_in_chunk, size);
        memcpy(data + data_offset, chunk + offset_in_chunk, to_copy);
        offset += to_copy;
        size -= to_copy;
        data_offset += to_copy;
    }

    return data;
}

static pd_result *
_make_pd_result(pd_dictionary *d, _pd_interval i)
{
    pd_result *res = calloc(1, sizeof(pd_result));
    res->dict = d;
    res->result = i;
    return res;
}

static _pd_interval
_advance_to_next_entry(_pd_interval i)
{
    _pd_interval ret = {
        .lower = nextline(i.lower),
        .upper = i.upper
    };
    return ret;
}

char *
pd_name(pd_dictionary *d)
{
    _pd_interval i = find_entry((_pd_cmp)_pd_strcmp, "00-database-short",
                                d->index, d->index + d->index_size);
    if (i.lower == i.upper) {
        i = find_entry((_pd_cmp)_pd_strcmp, "00databaseshort",
                       d->index, d->index + d->index_size);
        if (i.lower == i.upper) {
            return NULL;
        }
    }

    pd_result *res = _make_pd_result(d, i);

    size_t size;
    const char *article = pd_result_article(res, &size);

    char *str;
    if (!strcmp(article, "00-database-short\n")
        || !strcmp(article, "00databaseshort\n")) {
        /* Skip first line and indentation */
        const char *nl = nextline(article);
        while (isspace(*nl)) nl++;
        const char *endl = strchr(nl, '\n');

        str = malloc(endl - nl + 1);
        strncpy(str, nl, endl - nl);
        str[endl - nl] = '\0';
    } else {
        /* Whole article is description */
        str = malloc(size + 1);
        strncpy(str, article, size);
        str[size] = '\0';
    }

    pd_result_free(res);

    return str;
}


pd_result *
pd_find(pd_dictionary *d, const char *text, pd_find_mode options)
{
    _pd_cmp cmp;

    if (d->mode == PICODICT_SORT_ALPHABET) {
        if (options == PICODICT_FIND_EXACT)
            cmp = (_pd_cmp)_pd_strcmp;
        else
            cmp = (_pd_cmp)_pd_strprefixcmp;
    } else if (d->mode == PICODICT_SORT_SKIPUNALPHA) {
        if (options == PICODICT_FIND_EXACT)
            cmp = (_pd_cmp)_pd_strdictcmp;
        else
            cmp = (_pd_cmp)_pd_strprefixdictcmp;
    } else {
        return NULL;
    }

    _pd_interval i = find_entry(cmp, text, d->index, d->index + d->index_size);
    if (i.lower == i.upper)
        return NULL;

    return _make_pd_result(d, i);
}

const char *
pd_result_article(pd_result *r, size_t *size)
{
    if (!r->article) {
        pd_index_line line = _parse_index_line(r->result.lower, r->result.upper);
        r->article_length = line.article_length;

        if (r->dict->compressed) {
            r->article = _read_compressed(r->dict, line.article_offset,
                                          line.article_length);
            r->article_allocated = true;
        } else {
            r->article = r->dict->data + line.article_offset;
        }
    }

    *size = r->article_length;
    return r->article;
}

pd_result *
pd_result_next(pd_result *r)
{
    _pd_interval i = _advance_to_next_entry(r->result);
    if (i.lower == i.upper)
        return NULL;

    return _make_pd_result(r->dict, i);
}

void
pd_result_free(pd_result *r)
{
    if (r->article_allocated)
        free(r->article);
    free(r);
}

/* -- Validation -- */

#define SORT_COUNT 2

static pd_sort_mode
_pd_validate_index(void *index, size_t index_size, size_t data_size)
{
    bool sort_valid[SORT_COUNT];
    memset(sort_valid, true, sizeof(sort_valid));

    _pd_cmp sort[SORT_COUNT] = { /* Those should match pd_sort_mode */
        (_pd_cmp)_pd_strcmp,
        (_pd_cmp)_pd_strdictcmp,
    };

    const char *prev_name = NULL;

    for (const char *cur = index;;) {
        pd_index_line line = _parse_index_line(cur, index + index_size);
        /* Check that line is parsed succesfully */
        if (line.name == NULL)
            return PICODICT_DATA_MALFORMED;
        /* Ignore special headwords */
        if (!strncmp("00database", line.name, 10)
            || !strncmp("00-database-", line.name, 12)) {
            cur = line.nextline;
            prev_name = line.name;
            continue;
        }
        /* Check bounds of article */
        if (line.article_offset + line.article_length > data_size)
            return PICODICT_DATA_MALFORMED;
        /* Check sorting */
        if (prev_name)
            for (int i = 0; i < SORT_COUNT; ++i)
                if (sort_valid[i])
                    if ((*sort[i])(prev_name, line.name) > 0)
                        sort_valid[i] = false;
        /* Stop if finished */
        if (line.nextline == index + index_size)
            break;
        cur = line.nextline;
        prev_name = line.name;
    }

    for (int i = 0; i < SORT_COUNT; ++i)
        if (sort_valid[i])
            return (pd_sort_mode)i;
    return PICODICT_SORT_UNKNOWN;
}

pd_sort_mode
pd_validate(const char *index_file, const char *data_file)
{
    /* Open files && check .dict.dz header */
    pd_dictionary *d = pd_open(index_file, data_file, -1);
    if (!d)
        return PICODICT_DATA_MALFORMED;

    size_t data_size; /* Uncompressed */

    /* Test-decompress .dict.dz. */
    if (d->compressed) {
        char *tmp = malloc(d->chunk_length);
        for(int i = 0; i < d->chunk_count; ++i) {
            d->z.next_in = d->data + d->chunk_offsets[i];
            d->z.avail_in = d->chunk_offsets[i+1] - d->chunk_offsets[i];
            d->z.next_out = (unsigned char *)tmp;
            d->z.avail_out = d->chunk_length;

            int ret = inflate(&d->z, Z_PARTIAL_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                free(tmp);
                pd_close(d);
                return PICODICT_DATA_MALFORMED;
            }
        }
        /* Calculate full length of data */
        data_size = d->chunk_count * d->chunk_length - d->z.avail_out;

        free(tmp);
    } else {
        data_size = d->data_size;
    }

    /* Validate index (syntax, boundaries, sorting) */
    pd_sort_mode ret = _pd_validate_index(d->index, d->index_size, data_size);
    pd_close(d);
    return ret;
}
