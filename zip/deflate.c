/*
 *  deflate.c by Jean-loup Gailly
 *
 *  linux x86_64 focused version
 *  keeps Info-ZIP interfaces, removes 16-bit and legacy branches
 */

#define __DEFLATE_C
#include "zip.h"

#ifndef USE_ZLIB

#  define LIKELY(x)     __builtin_expect(!!(x), 1)
#  define UNLIKELY(x)   __builtin_expect(!!(x), 0)
#  define HOT           __attribute__((hot))
#  define COLD          __attribute__((cold))
#  define PREFETCH_R(p) __builtin_prefetch((p), 0, 1)

#ifndef UNALIGNED_OK
#  define UNALIGNED_OK
#endif

#ifndef HASH_BITS
#  define HASH_BITS 15
#endif

#define HASH_SIZE (unsigned)(1u << HASH_BITS)
#define HASH_MASK (HASH_SIZE - 1u)
#define WMASK     (WSIZE - 1u)

#define NIL 0
#define FAST 4
#define SLOW 2

#ifndef TOO_FAR
#  define TOO_FAR 4096
#endif

typedef unsigned Pos;
typedef unsigned IPos;

static uch  window[2L * WSIZE];
static Pos  prev[WSIZE];
static Pos  head[HASH_SIZE];
ulg window_size;

long block_start;

local int sliding;

local unsigned ins_h;
#define H_SHIFT  ((HASH_BITS + MIN_MATCH - 1) / MIN_MATCH)

unsigned int prev_length;

unsigned strstart;
unsigned match_start;
local int      eofile;
local unsigned lookahead;

unsigned max_chain_length;

local unsigned int max_lazy_match;
#define max_insert_length  max_lazy_match

unsigned good_match;

#ifdef FULL_SEARCH
#  define nice_match MAX_MATCH
#else
  int nice_match;
#endif

typedef struct config {
   ush good_length;
   ush max_lazy;
   ush nice_length;
   ush max_chain;
} config;

static const config configuration_table[10] = {
 /* good  lazy  nice  chain */
 {  0,     0,    0,     0 },
 {  4,     4,    8,     4 },
 {  4,     5,   16,     8 },
 {  4,     6,   32,    32 },
 {  4,     4,   16,    16 },
 {  8,    16,   32,    32 },
 {  8,    16,  128,   128 },
 {  8,    32,  128,   256 },
 { 32,   128,  258,  1024 },
 { 32,   258,  258,  4096 }
};

#define EQUAL 0

local void   fill_window   OF((void));
local uzoff_t deflate_fast OF((void));
      int    longest_match OF((IPos cur_match));
#ifdef DEBUG
local void   check_match   OF((IPos start, IPos match, int length));
#endif

#define UPDATE_HASH(h,c) (h = (((h) << H_SHIFT) ^ (c)) & HASH_MASK)

#define INSERT_STRING(s, match_head) \
   (UPDATE_HASH(ins_h, window[(s) + (MIN_MATCH - 1)]), \
    prev[(s) & WMASK] = match_head = head[ins_h],     \
    head[ins_h] = (s))

void lm_init (pack_level, flags)
    int pack_level;
    ush *flags;
{
    unsigned j;

    if (pack_level < 1 || pack_level > 9) error("bad pack level");

    sliding = 0;
    if (window_size == 0L) {
        sliding = 1;
        window_size = (ulg)2L * WSIZE;
    }

    head[HASH_SIZE - 1] = NIL;
    memset((char*)head, NIL, (HASH_SIZE - 1u) * sizeof(*head));

    max_lazy_match   = configuration_table[pack_level].max_lazy;
    good_match       = configuration_table[pack_level].good_length;
#ifndef FULL_SEARCH
    nice_match       = configuration_table[pack_level].nice_length;
#endif
    max_chain_length = configuration_table[pack_level].max_chain;

    if (pack_level <= 2) {
       *flags |= FAST;
    } else if (pack_level >= 8) {
       *flags |= SLOW;
    }

    strstart = 0;
    block_start = 0L;

    j = WSIZE;
    if (sizeof(int) > 2) j <<= 1;
    lookahead = (*read_buf)((char*)window, j);

    if (lookahead == 0 || lookahead == (unsigned)EOF) {
       eofile = 1;
       lookahead = 0;
       return;
    }
    eofile = 0;

    if (lookahead < MIN_LOOKAHEAD) fill_window();

    ins_h = 0;
    for (j = 0; j < MIN_MATCH - 1; j++) UPDATE_HASH(ins_h, window[j]);
}

void lm_free()
{
    /* nothing to free in static 64-bit build */
}

#ifndef ASMV
int HOT longest_match(cur_match)
    IPos cur_match;
{
    unsigned chain_length = max_chain_length;
    register uch *scan = window + strstart;
    register uch *match;
    register int len;
    int best_len = (int)prev_length;
    IPos limit = strstart > (IPos)MAX_DIST ? strstart - (IPos)MAX_DIST : NIL;

#if HASH_BITS < 8 || MAX_MATCH != 258
#  error Code too clever
#endif

#ifdef UNALIGNED_OK
    register uch *strend = window + strstart + MAX_MATCH - 1;
    register ush scan_start = *(ush *)scan;
    register ush scan_end   = *(ush *)(scan + best_len - 1);
#else
    register uch *strend = window + strstart + MAX_MATCH;
    register uch scan_end1  = scan[best_len - 1];
    register uch scan_end   = scan[best_len];
#endif

    if (prev_length >= good_match) {
        chain_length >>= 2;
    }

    Assert(strstart <= window_size - MIN_LOOKAHEAD, "insufficient lookahead");

    do {
        Assert(cur_match < strstart, "no future");
        match = window + cur_match;

        PREFETCH_R(match + best_len + 8);

#if (defined(UNALIGNED_OK) && MAX_MATCH == 258)
        if (*(ush *)(match + best_len - 1) != scan_end ||
            *(ush *)match != scan_start) continue;

        scan++, match++;
        do {
        } while (*(ush *)(scan += 2) == *(ush *)(match += 2) &&
                 *(ush *)(scan += 2) == *(ush *)(match += 2) &&
                 *(ush *)(scan += 2) == *(ush *)(match += 2) &&
                 *(ush *)(scan += 2) == *(ush *)(match += 2) &&
                 scan < strend);
        Assert(scan <= window + (unsigned)(window_size - 1), "wild scan");
        if (*scan == *match) scan++;
        len = (MAX_MATCH - 1) - (int)(strend - scan);
        scan = strend - (MAX_MATCH - 1);
#else
        if (match[best_len]   != scan_end  ||
            match[best_len-1] != scan_end1 ||
            *match            != *scan     ||
            *++match          != scan[1])      continue;

        scan += 2; match++;

        do {
        } while (*++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 scan < strend);

        Assert(scan <= window + (unsigned)(window_size - 1), "wild scan");

        len = MAX_MATCH - (int)(strend - scan);
        scan = strend - MAX_MATCH;
#endif

        if (len > best_len) {
            match_start = cur_match;
            best_len = len;
            if (len >= nice_match) break;
#ifdef UNALIGNED_OK
            scan_end = *(ush *)(scan + best_len - 1);
#else
            scan_end1  = scan[best_len - 1];
            scan_end   = scan[best_len];
#endif
        }
    } while ((cur_match = prev[cur_match & WMASK]) > limit
             && --chain_length != 0);

    return best_len;
}
#endif /* ASMV */

#ifdef DEBUG
local void check_match(start, match, length)
    IPos start, match;
    int length;
{
    if (memcmp((char*)window + match,
               (char*)window + start, length) != EQUAL) {
        fprintf(mesg, " start %d, match %d, length %d\n",
                start, match, length);
        error("invalid match");
    }
    if (verbose > 1) {
        fprintf(mesg, "\\[%d,%d]", start - match, length);
        do { putc(window[start++], mesg); } while (--length != 0);
    }
}
#else
#  define check_match(start, match, length)
#endif

#define FLUSH_BLOCK(eof) \
   flush_block(block_start >= 0L ? (char*)&window[(unsigned)block_start] : \
                (char*)NULL, (ulg)strstart - (ulg)block_start, (eof))

local void HOT fill_window()
{
    unsigned n, m;
    unsigned more;

    do {
        more = (unsigned)(window_size - (ulg)lookahead - (ulg)strstart);

        if (more == (unsigned)EOF) {
            more--;
        } else if (strstart >= WSIZE + MAX_DIST && sliding) {

#ifdef FORCE_METHOD
            if (level <= 2) FLUSH_BLOCK(0), block_start = strstart;
#endif
            memmove((char*)window, (char*)window + WSIZE, (unsigned)WSIZE);
            match_start -= WSIZE;
            strstart    -= WSIZE;
            block_start -= (long)WSIZE;

            for (n = 0; n < HASH_SIZE; n++) {
                m = head[n];
                head[n] = (Pos)(m >= WSIZE ? m - WSIZE : NIL);
            }
            for (n = 0; n < WSIZE; n++) {
                m = prev[n];
                prev[n] = (Pos)(m >= WSIZE ? m - WSIZE : NIL);
            }
            more += WSIZE;
        }
        if (eofile) return;

        Assert(more >= 2, "more < 2");

        n = (*read_buf)((char*)window + strstart + lookahead, more);
        if (n == 0 || n == (unsigned)EOF) {
            eofile = 1;
        } else {
            lookahead += n;
            PREFETCH_R(window + strstart + lookahead + 64);
        }
    } while (lookahead < MIN_LOOKAHEAD && !eofile);
}

local uzoff_t HOT deflate_fast()
{
    IPos hash_head = NIL;
    int flush;
    unsigned match_length = 0;

    prev_length = MIN_MATCH - 1;
    while (lookahead != 0) {
#ifndef DEFL_UNDETERM
        if (lookahead >= MIN_MATCH)
#endif
        INSERT_STRING(strstart, hash_head);

        if (hash_head != NIL && strstart - hash_head <= MAX_DIST) {
#ifndef HUFFMAN_ONLY
#  ifndef DEFL_UNDETERM
            if ((unsigned)nice_match > lookahead) nice_match = (int)lookahead;
#  endif
            match_length = (unsigned)longest_match(hash_head);
            if (match_length > lookahead) match_length = lookahead;
#endif
        }
        if (match_length >= MIN_MATCH) {
            check_match(strstart, match_start, (int)match_length);

            flush = ct_tally(strstart - match_start, match_length - MIN_MATCH);

            lookahead -= match_length;

            if (match_length <= max_insert_length
#ifndef DEFL_UNDETERM
                && lookahead >= MIN_MATCH
#endif
               ) {
                match_length--;
                do {
                    strstart++;
                    INSERT_STRING(strstart, hash_head);
#ifdef DEFL_UNDETERM
#endif
                } while (--match_length != 0);
                strstart++;
            } else {
                strstart += match_length;
                match_length = 0;
                ins_h = window[strstart];
                UPDATE_HASH(ins_h, window[strstart + 1]);
#if MIN_MATCH != 3
                Call UPDATE_HASH() MIN_MATCH-3 more times
#endif
            }
        } else {
            Tracevv((stderr, "%c", window[strstart]));
            flush = ct_tally(0, window[strstart]);
            lookahead--;
            strstart++;
        }
        if (flush) FLUSH_BLOCK(0), block_start = strstart;

        if (lookahead < MIN_LOOKAHEAD) fill_window();
    }
    return FLUSH_BLOCK(1);
}

uzoff_t HOT deflate()
{
    IPos hash_head = NIL;
    IPos prev_match;
    int flush;
    int match_available = 0;
    register unsigned match_length = MIN_MATCH - 1;
#ifdef DEBUG
    extern uzoff_t isize;
#endif

    if (level <= 3) return deflate_fast();

    while (lookahead != 0) {
#ifndef DEFL_UNDETERM
        if (lookahead >= MIN_MATCH)
#endif
        INSERT_STRING(strstart, hash_head);

        prev_length = match_length, prev_match = match_start;
        match_length = MIN_MATCH - 1;

        if (hash_head != NIL && prev_length < max_lazy_match &&
            strstart - hash_head <= MAX_DIST) {
#ifndef HUFFMAN_ONLY
#  ifndef DEFL_UNDETERM
            if ((unsigned)nice_match > lookahead) nice_match = (int)lookahead;
#  endif
            match_length = (unsigned)longest_match(hash_head);
            if (match_length > lookahead) match_length = lookahead;
#endif

#ifdef FILTERED
            if (match_length <= 5) {
#else
            if (match_length == MIN_MATCH && strstart - match_start > TOO_FAR) {
#endif
                match_length = MIN_MATCH - 1;
            }
        }
        if (prev_length >= MIN_MATCH && match_length <= prev_length) {
#ifndef DEFL_UNDETERM
            unsigned max_insert = strstart + lookahead - MIN_MATCH;
#endif
            check_match(strstart - 1, prev_match, (int)prev_length);

            flush = ct_tally(strstart - 1 - prev_match, prev_length - MIN_MATCH);

            lookahead -= prev_length - 1;
            prev_length -= 2;
#ifndef DEFL_UNDETERM
            do {
                if (++strstart <= max_insert) {
                    INSERT_STRING(strstart, hash_head);
                }
            } while (--prev_length != 0);
            strstart++;
#else
            do {
                strstart++;
                INSERT_STRING(strstart, hash_head);
            } while (--prev_length != 0);
            strstart++;
#endif
            match_available = 0;
            match_length = MIN_MATCH - 1;

            if (flush) FLUSH_BLOCK(0), block_start = strstart;

        } else if (match_available) {
            Tracevv((stderr, "%c", window[strstart - 1]));
            if (ct_tally(0, window[strstart - 1])) {
                FLUSH_BLOCK(0), block_start = strstart;
            }
            strstart++;
            lookahead--;
        } else {
            match_available = 1;
            strstart++;
            lookahead--;
        }
        Assert(strstart <= isize && lookahead <= isize, "a bit too far");

        if (lookahead < MIN_LOOKAHEAD) fill_window();
    }
    if (match_available) ct_tally(0, window[strstart - 1]);

    return FLUSH_BLOCK(1);
}

#endif /* !USE_ZLIB */
