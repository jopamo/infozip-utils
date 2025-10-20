/* inflate.c

   Inflate deflated (PKZIP's method 8 compressed) data.  The compression
   method searches for as much of the current string of bytes (up to a
   length of 258) in the previous 32K bytes.  If it doesn't find any
   matches (of at least length 3), it codes the next byte.  Otherwise, it
   codes the length of the matched string and its distance backwards from
   the current position.  There is a single Huffman code that codes both
   single bytes (called "literals") and match lengths.  A second Huffman
   code codes the distance information, which follows a length code.  Each
   length or distance code actually represents a base value and a number
   of "extra" (sometimes zero) bits to get to add to the base value.  At
   the end of each deflated block is a special end-of-block (EOB) literal/
   length code.  The decoding process is basically: get a literal/length
   code; if EOB then done; if a literal, emit the decoded byte; if a
   length then get the distance and emit the referred-to bytes from the
   sliding window of previously emitted data.

   There are (currently) three kinds of inflate blocks: stored, fixed, and
   dynamic.  The compressor outputs a chunk of data at a time and decides
   which method to use on a chunk-by-chunk basis.  A chunk might typically
   be 32K to 64K, uncompressed.  If the chunk is uncompressible, then the
   "stored" method is used.  In this case, the bytes are simply stored as
   is, eight bits per byte, with none of the above coding.  The bytes are
   preceded by a count, since there is no longer an EOB code.

   If the data are compressible, then either the fixed or dynamic methods
   are used.  In the dynamic method, the compressed data are preceded by
   an encoding of the literal/length and distance Huffman codes that are
   to be used to decode this block.  The representation is itself Huffman
   coded, and so is preceded by a description of that code.  These code
   descriptions take up a little space, and so for small blocks, there is
   a predefined set of codes, called the fixed codes.  The fixed method is
   used if the block ends up smaller that way (usually for quite small
   chunks); otherwise the dynamic method is used.  In the latter case, the
   codes are customized to the probabilities in the current block and so
   can code it much better than the pre-determined fixed codes can.

   The Huffman codes themselves are decoded using a multi-level table
   lookup, in order to maximize the speed of decoding plus the speed of
   building the decoding tables.  See the comments below that precede the
   lbits and dbits tuning parameters.

   GRR:  return values(?)
           0  OK
           1  incomplete table
           2  bad input
           3  not enough memory
         the following return codes are passed through from FLUSH() errors
           50 (PK_DISK)   "overflow of output space"
           80 (IZ_CTRLC)  "canceled by user's request"
 */

/*
   Notes beyond the 1.93a appnote.txt:
   ... (unchanged large explanatory comment) ...
 */

#define PKZIP_BUG_WORKAROUND /* PKZIP 1.93a problem--live with it */

#define __INFLATE_C /* identifies this source module */

/* #define DEBUG */
#define INFMOD /* tell inflate.h to include code to be compiled */
#include "inflate.h"

#include <string.h> /* memcpy, memset */

/* Branch prediction hints for modern GCC/Clang. */
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

/* marker for "unused" huft code, and corresponding check macro */
#define INVALID_CODE 99
#define IS_INVALID_CODE(c) ((c) == INVALID_CODE)

#ifndef WSIZE /* default is 32K resp. 64K */
#ifdef USE_DEFLATE64
#define WSIZE 65536L /* window size--must be a power of two, and */
#else                /* at least 64K for PKZip's deflate64 method */
#define WSIZE 0x8000 /* window size--must be a power of two, and */
#endif               /* at least 32K for zip's deflate method */
#endif

/* some buffer counters must be capable of holding 64k for Deflate64 */
#if (defined(USE_DEFLATE64) && defined(INT_16BIT))
#define UINT_D64 ulg
#else
#define UINT_D64 unsigned
#endif

#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
#define wsize G._wsize /* wsize is a variable */
#else
#define wsize WSIZE /* wsize is a constant */
#endif

#ifndef NEXTBYTE /* default is to simply get a byte from stdin */
#define NEXTBYTE getchar()
#endif

#ifndef MESSAGE /* only used twice, for fixed strings--NOT general-purpose */
#define MESSAGE(str, len, flag) fprintf(stderr, (char*)(str))
#endif

#ifndef FLUSH /* default is to simply write the buffer to stdout */
#define FLUSH(n) (((extent)fwrite(redirSlide, 1, (extent)(n), stdout) == (extent)(n)) ? 0 : PKDISK)
#endif

#ifndef Trace
#ifdef DEBUG
#define Trace(x) fprintf x
#else
#define Trace(x)
#endif
#endif

/* Ensure memzero maps to memset on modern systems. */
#ifndef memzero
#define memzero(p, n) memset((p), 0, (n))
#endif

/*---------------------------------------------------------------------------*/
#ifdef USE_ZLIB

#ifdef USE_ZLIB_INFLATCB
#undef USE_ZLIB_INFLATCB
#endif
#if (defined(ZLIB_VERNUM) && ZLIB_VERNUM >= 0x1200 && !defined(NO_ZLIBCALLBCK))
#define USE_ZLIB_INFLATCB 1
#else
#define USE_ZLIB_INFLATCB 0
#endif

#if defined(USE_DEFLATE64)
#if !USE_ZLIB_INFLATCB
#error Deflate64 is incompatible with traditional (pre-1.2.x) zlib interface!
#else
#include "infback9.h"
#endif
#endif /* USE_DEFLATE64 */

#if USE_ZLIB_INFLATCB

static unsigned zlib_inCB OF((void FAR* pG, unsigned char FAR * FAR * pInbuf));
static int zlib_outCB OF((void FAR* pG, unsigned char FAR* outbuf, unsigned outcnt));

static unsigned zlib_inCB(pG, pInbuf) void FAR* pG;
unsigned char FAR * FAR * pInbuf;
{
    *pInbuf = G.inbuf;
    return fillinbuf(__G);
}

static int zlib_outCB(pG, outbuf, outcnt) void FAR* pG;
unsigned char FAR* outbuf;
unsigned outcnt;
{
#ifdef FUNZIP
    return flush(__G__(ulg)(outcnt));
#else
    return ((G.mem_mode) ? memflush(__G__ outbuf, (ulg)(outcnt)) : flush(__G__ outbuf, (ulg)(outcnt), 0));
#endif
}
#endif /* USE_ZLIB_INFLATCB */

int UZinflate(__G__ is_defl64) __GDEF int is_defl64;
/* decompress an inflated entry using the zlib routines */
{
    int retval = 0; /* return code: 0 = "no error" */
    int err = Z_OK;
#if USE_ZLIB_INFLATCB

#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    if (G.redirect_slide)
        wsize = G.redirect_size, redirSlide = G.redirect_buffer;
    else
        wsize = WSIZE, redirSlide = slide;
#endif

    if (!G.inflInit) {
        ZCONST char* zlib_RtVersion = zlibVersion();
        if ((zlib_RtVersion[0] != ZLIB_VERSION[0]) || (zlib_RtVersion[2] != ZLIB_VERSION[2])) {
            Info(slide, 0x21, ((char*)slide, "error:  incompatible zlib version (expected %s, found %s)\n", ZLIB_VERSION, zlib_RtVersion));
            return 3;
        }
        else if (strcmp(zlib_RtVersion, ZLIB_VERSION) != 0) {
            Info(slide, 0x21, ((char*)slide, "warning:  different zlib version (expected %s, using %s)\n", ZLIB_VERSION, zlib_RtVersion));
        }
        G.dstrm.zalloc = (alloc_func)Z_NULL;
        G.dstrm.zfree = (free_func)Z_NULL;
        G.inflInit = 1;
    }

#ifdef USE_DEFLATE64
    if (is_defl64) {
        Trace((stderr, "initializing inflate9()\n"));
        err = inflateBack9Init(&G.dstrm, redirSlide);
        if (err == Z_MEM_ERROR)
            return 3;
        else if (err != Z_OK) {
            Trace((stderr, "oops!  (inflateBack9Init() err = %d)\n", err));
            return 2;
        }

        G.dstrm.next_in = G.inptr;
        G.dstrm.avail_in = G.incnt;

        err = inflateBack9(&G.dstrm, zlib_inCB, &G, zlib_outCB, &G);
        if (err != Z_STREAM_END) {
            if (err == Z_DATA_ERROR || err == Z_STREAM_ERROR) {
                Trace((stderr, "oops!  (inflateBack9() err = %d)\n", err));
                retval = 2;
            }
            else if (err == Z_MEM_ERROR) {
                retval = 3;
            }
            else if (err == Z_BUF_ERROR) {
                Trace((stderr, "oops!  (inflateBack9() err = %d)\n", err));
                if (G.dstrm.next_in == Z_NULL) {
                    Trace((stderr, "  inflateBack9() input failure\n"));
                    retval = 2;
                }
                else {
                    retval = (G.disk_full != 0 ? PK_DISK : IZ_CTRLC);
                }
            }
            else {
                Trace((stderr, "oops!  (inflateBack9() err = %d)\n", err));
                retval = 2;
            }
        }
        if (G.dstrm.next_in != NULL) {
            G.inptr = (uch*)G.dstrm.next_in;
            G.incnt = G.dstrm.avail_in;
        }

        err = inflateBack9End(&G.dstrm);
        if (err != Z_OK) {
            Trace((stderr, "oops!  (inflateBack9End() err = %d)\n", err));
            if (retval == 0)
                retval = 2;
        }
    }
    else
#endif /* USE_DEFLATE64 */
    {
        unsigned i;
        int windowBits;
        for (i = (unsigned)wsize, windowBits = 0; !(i & 1); i >>= 1, ++windowBits) { /* log2(wsize) */
        }
        if ((unsigned)windowBits > (unsigned)15)
            windowBits = 15;
        else if (windowBits < 8)
            windowBits = 8;

        Trace((stderr, "initializing inflate()\n"));
        err = inflateBackInit(&G.dstrm, windowBits, redirSlide);
        if (err == Z_MEM_ERROR)
            return 3;
        else if (err != Z_OK) {
            Trace((stderr, "oops!  (inflateBackInit() err = %d)\n", err));
            return 2;
        }

        G.dstrm.next_in = G.inptr;
        G.dstrm.avail_in = G.incnt;

        err = inflateBack(&G.dstrm, zlib_inCB, &G, zlib_outCB, &G);
        if (err != Z_STREAM_END) {
            if (err == Z_DATA_ERROR || err == Z_STREAM_ERROR) {
                Trace((stderr, "oops!  (inflateBack() err = %d)\n", err));
                retval = 2;
            }
            else if (err == Z_MEM_ERROR) {
                retval = 3;
            }
            else if (err == Z_BUF_ERROR) {
                Trace((stderr, "oops!  (inflateBack() err = %d)\n", err));
                if (G.dstrm.next_in == Z_NULL) {
                    Trace((stderr, "  inflateBack() input failure\n"));
                    retval = 2;
                }
                else {
                    retval = (G.disk_full != 0 ? PK_DISK : IZ_CTRLC);
                }
            }
            else {
                Trace((stderr, "oops!  (inflateBack() err = %d)\n", err));
                retval = 2;
            }
        }
        if (G.dstrm.next_in != NULL) {
            G.inptr = (uch*)G.dstrm.next_in;
            G.incnt = G.dstrm.avail_in;
        }

        err = inflateBackEnd(&G.dstrm);
        if (err != Z_OK) {
            Trace((stderr, "oops!  (inflateBackEnd() err = %d)\n", err));
            if (retval == 0)
                retval = 2;
        }
    }

#else /* !USE_ZLIB_INFLATCB */
    int repeated_buf_err;

#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    if (G.redirect_slide)
        wsize = G.redirect_size, redirSlide = G.redirect_buffer;
    else
        wsize = WSIZE, redirSlide = slide;
#endif

    G.dstrm.next_out = redirSlide;
    G.dstrm.avail_out = wsize;

    G.dstrm.next_in = G.inptr;
    G.dstrm.avail_in = G.incnt;

    if (!G.inflInit) {
        unsigned i;
        int windowBits;
        ZCONST char* zlib_RtVersion = zlibVersion();

        if (zlib_RtVersion[0] != ZLIB_VERSION[0]) {
            Info(slide, 0x21, ((char*)slide, "error:  incompatible zlib version (expected %s, found %s)\n", ZLIB_VERSION, zlib_RtVersion));
            return 3;
        }
        else if (strcmp(zlib_RtVersion, ZLIB_VERSION) != 0) {
            Info(slide, 0x21, ((char*)slide, "warning:  different zlib version (expected %s, using %s)\n", ZLIB_VERSION, zlib_RtVersion));
        }

        for (i = (unsigned)wsize, windowBits = 0; !(i & 1); i >>= 1, ++windowBits) { /* log2(wsize) */
        }
        if ((unsigned)windowBits > (unsigned)15)
            windowBits = 15;
        else if (windowBits < 8)
            windowBits = 8;

        G.dstrm.zalloc = (alloc_func)Z_NULL;
        G.dstrm.zfree = (free_func)Z_NULL;

        Trace((stderr, "initializing inflate()\n"));
        err = inflateInit2(&G.dstrm, -windowBits);
        if (err == Z_MEM_ERROR)
            return 3;
        else if (err != Z_OK)
            Trace((stderr, "oops!  (inflateInit2() err = %d)\n", err));
        G.inflInit = 1;
    }

#ifdef FUNZIP
    while (err != Z_STREAM_END) {
#else
    while (G.csize > 0) {
        Trace((stderr, "first loop:  G.csize = %ld\n", G.csize));
#endif
        while (G.dstrm.avail_out > 0) {
            err = inflate(&G.dstrm, Z_PARTIAL_FLUSH);

            if (err == Z_DATA_ERROR) {
                retval = 2;
                goto uzinflate_cleanup_exit;
            }
            else if (err == Z_MEM_ERROR) {
                retval = 3;
                goto uzinflate_cleanup_exit;
            }
            else if (err != Z_OK && err != Z_STREAM_END) {
                Trace((stderr, "oops!  (inflate(first loop) err = %d)\n", err));
            }

#ifdef FUNZIP
            if (err == Z_STREAM_END) /* "END-of-entry-condition" ? */
#else
            if (G.csize <= 0L) /* "END-of-entry-condition" ? */
#endif
                break;

            if (G.dstrm.avail_in == 0) {
                if (fillinbuf(__G) == 0) {
                    retval = 2;
                    goto uzinflate_cleanup_exit;
                }
                G.dstrm.next_in = G.inptr;
                G.dstrm.avail_in = G.incnt;
            }
            Trace((stderr, "     avail_in = %u\n", G.dstrm.avail_in));
        }
        if ((retval = FLUSH(wsize - G.dstrm.avail_out)) != 0)
            goto uzinflate_cleanup_exit;
        Trace((stderr, "inside loop:  flushing %ld bytes (ptr diff = %ld)\n", (long)(wsize - G.dstrm.avail_out), (long)(G.dstrm.next_out - (Bytef*)redirSlide)));
        G.dstrm.next_out = redirSlide;
        G.dstrm.avail_out = wsize;
    }

    Trace((stderr, "beginning final loop:  err = %d\n", err));
    repeated_buf_err = FALSE;
    while (err != Z_STREAM_END) {
        err = inflate(&G.dstrm, Z_PARTIAL_FLUSH);
        if (err == Z_DATA_ERROR) {
            retval = 2;
            goto uzinflate_cleanup_exit;
        }
        else if (err == Z_MEM_ERROR) {
            retval = 3;
            goto uzinflate_cleanup_exit;
        }
        else if (err == Z_BUF_ERROR) {
#ifdef FUNZIP
            Trace((stderr, "zlib inflate() did not detect stream end\n"));
#else
            Trace((stderr, "zlib inflate() did not detect stream end (%s, %s)\n", G.zipfn, G.filename));
#endif
            if ((!repeated_buf_err) && (G.dstrm.avail_in == 0)) {
                G.dstrm.next_in = (Bytef*)"";
                G.dstrm.avail_in = 1;
                repeated_buf_err = TRUE;
            }
            else {
                break;
            }
        }
        else if (err != Z_OK && err != Z_STREAM_END) {
            Trace((stderr, "oops!  (inflate(final loop) err = %d)\n", err));
            DESTROYGLOBALS();
            EXIT(PK_MEM3);
        }
        if ((retval = FLUSH(wsize - G.dstrm.avail_out)) != 0)
            goto uzinflate_cleanup_exit;
        Trace((stderr, "final loop:  flushing %ld bytes (ptr diff = %ld)\n", (long)(wsize - G.dstrm.avail_out), (long)(G.dstrm.next_out - (Bytef*)redirSlide)));
        G.dstrm.next_out = redirSlide;
        G.dstrm.avail_out = wsize;
    }
    Trace((stderr, "total in = %lu, total out = %lu\n", G.dstrm.total_in, G.dstrm.total_out));

    G.inptr = (uch*)G.dstrm.next_in;
    G.incnt -= G.inptr - G.inbuf; /* reset for other routines */

uzinflate_cleanup_exit:
    err = inflateReset(&G.dstrm);
    if (err != Z_OK)
        Trace((stderr, "oops!  (inflateReset() err = %d)\n", err));
#endif /* ?USE_ZLIB_INFLATCB */
    return retval;
}

/*---------------------------------------------------------------------------*/
#else /* !USE_ZLIB */

/* Function prototypes */
#ifndef OF
#ifdef __STDC__
#define OF(a) a
#else
#define OF(a) ()
#endif
#endif /* !OF */
int inflate_codes OF((__GPRO__ struct huft * tl, struct huft* td, unsigned bl, unsigned bd));
static int inflate_stored OF((__GPRO));
static int inflate_fixed OF((__GPRO));
static int inflate_dynamic OF((__GPRO));
static int inflate_block OF((__GPRO__ int* e));

/* unsigned wp; moved to globals.h */

/* Tables for deflate from PKZIP's appnote.txt. */
static ZCONST unsigned border[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

#ifdef USE_DEFLATE64
static ZCONST ush cplens64[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 3, 0, 0};
#else
#define cplens32 cplens
#endif
static ZCONST ush cplens32[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};

#ifdef USE_DEFLATE64
static ZCONST uch cplext64[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 16, INVALID_CODE, INVALID_CODE};
#else
#define cplext32 cplext
#endif
static ZCONST uch cplext32[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, INVALID_CODE, INVALID_CODE};

static ZCONST ush cpdist[] = {1,    2,     3,     4,     5,     7,    9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
#if (defined(USE_DEFLATE64) || defined(PKZIP_BUG_WORKAROUND))
                              8193, 12289, 16385, 24577, 32769, 49153};
#else
                              8193, 12289, 16385, 24577};
#endif

#ifdef USE_DEFLATE64
static ZCONST uch cpdext64[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14};
#else
#define cpdext32 cpdext
#endif
static ZCONST uch cpdext32[] = {0,           0,  0,  0,  1,
                                1,           2,  2,  3,  3,
                                4,           4,  5,  5,  6,
                                6,           7,  7,  8,  8,
                                9,           9,  10, 10, 11,
                                11,
#ifdef PKZIP_BUG_WORKAROUND
                                12,          12, 13, 13, INVALID_CODE,
                                INVALID_CODE
#else
                                12, 12, 13, 13
#endif
};

#ifdef PKZIP_BUG_WORKAROUND
#define MAXLITLENS 288
#else
#define MAXLITLENS 286
#endif
#if (defined(USE_DEFLATE64) || defined(PKZIP_BUG_WORKAROUND))
#define MAXDISTS 32
#else
#define MAXDISTS 30
#endif

/* NEEDBITS / DUMPBITS macros with explicit semicolons at call sites */
#ifndef CHECK_EOF
#define CHECK_EOF
#endif

#ifndef CHECK_EOF
#define NEEDBITS(n)                    \
    {                                  \
        while (k < (n)) {              \
            b |= ((ulg)NEXTBYTE) << k; \
            k += 8;                    \
        }                              \
    }
#else
#ifdef FIX_PAST_EOB_BY_TABLEADJUST
#define NEEDBITS(n)                    \
    {                                  \
        while (k < (n)) {              \
            int c = NEXTBYTE;          \
            if (c == EOF) {            \
                retval = 1;            \
                goto cleanup_and_exit; \
            }                          \
            b |= ((ulg)c) << k;        \
            k += 8;                    \
        }                              \
    }
#else
#define NEEDBITS(n)                    \
    {                                  \
        while ((int)k < (int)(n)) {    \
            int c = NEXTBYTE;          \
            if (c == EOF) {            \
                if ((int)k >= 0)       \
                    break;             \
                retval = 1;            \
                goto cleanup_and_exit; \
            }                          \
            b |= ((ulg)c) << k;        \
            k += 8;                    \
        }                              \
    }
#endif
#endif

#define DUMPBITS(n) \
    {               \
        b >>= (n);  \
        k -= (n);   \
    }

/* bits in base literal/length lookup table */
static ZCONST unsigned lbits = 9;
/* bits in base distance lookup table */
static ZCONST unsigned dbits = 6;

#ifndef ASM_INFLATECODES
int inflate_codes(__G__ tl, td, bl, bd)
__GDEF
struct huft *tl, *td; /* literal/length and distance decoder tables */
unsigned bl, bd;      /* number of bits decoded by tl[] and td[] */
{
    register unsigned e; /* table entry flag/number of extra bits */
    unsigned d;          /* index for copy */
    UINT_D64 n;          /* length for copy (deflate64: might be 64k+2) */
    UINT_D64 w;          /* current window position (deflate64: up to 64k) */
    struct huft* t;      /* pointer to table entry */
    unsigned ml, md;     /* masks for bl and bd bits */
    register ulg b;      /* bit buffer */
    register unsigned k; /* number of bits in bit buffer */
    int retval = 0;      /* error code returned: initialized to "no error" */

    /* make local copies of globals */
    b = G.bb;
    k = G.bk;
    w = G.wp;

    /* inflate the coded data */
    ml = mask_bits[bl];
    md = mask_bits[bd];
    while (1) {
        NEEDBITS(bl);
        t = tl + ((unsigned)b & ml);
        while (1) {
            DUMPBITS(t->b);

            if ((e = t->e) == 32) /* literal */
            {
                redirSlide[w++] = (uch)t->v.n;
                if (w == wsize) {
                    if ((retval = FLUSH(w)) != 0)
                        goto cleanup_and_exit;
                    w = 0;
                }
                break;
            }

            if (e < 31) /* length */
            {
                NEEDBITS(e);
                n = t->v.n + ((unsigned)b & mask_bits[e]);
                DUMPBITS(e);

                NEEDBITS(bd);
                t = td + ((unsigned)b & md);
                while (1) {
                    DUMPBITS(t->b);
                    if ((e = t->e) < 32)
                        break;
                    if (IS_INVALID_CODE(e))
                        return 1;
                    e &= 31;
                    NEEDBITS(e);
                    t = t->v.t + ((unsigned)b & mask_bits[e]);
                }
                NEEDBITS(e);
                d = (unsigned)w - t->v.n - ((unsigned)b & mask_bits[e]);
                DUMPBITS(e);

                do {
#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
                    if (G.redirect_slide) {
                        if ((UINT_D64)d >= wsize)
                            return 1;
                        e = (unsigned)(wsize - (d > (unsigned)w ? (UINT_D64)d : w));
                    }
                    else
#endif
                        e = (unsigned)(wsize - ((d &= (unsigned)(wsize - 1)) > (unsigned)w ? (UINT_D64)d : w));
                    if ((UINT_D64)e > n)
                        e = (unsigned)n;
                    n -= e;
#ifndef NOMEMCPY
                    if ((unsigned)w - d >= e) {
                        memcpy(redirSlide + (unsigned)w, redirSlide + d, e);
                        w += e;
                        d += e;
                    }
                    else
#endif
                    {
                        do {
                            redirSlide[w++] = redirSlide[d++];
                        } while (--e);
                    }
                    if (w == wsize) {
                        if ((retval = FLUSH(w)) != 0)
                            goto cleanup_and_exit;
                        w = 0;
                    }
                } while (n);
                break;
            }

            if (e == 31) /* EOB */
                goto cleanup_decode;

            if (IS_INVALID_CODE(e))
                return 1;

            e &= 31;
            NEEDBITS(e);
            t = t->v.t + ((unsigned)b & mask_bits[e]);
        }
    }
cleanup_decode:
    G.wp = (unsigned)w;
    G.bb = b;
    G.bk = k;

cleanup_and_exit:
    return retval;
}
#endif /* ASM_INFLATECODES */

static int inflate_stored(__G) __GDEF {
    UINT_D64 w;
    unsigned n;
    register ulg b;
    register unsigned k;
    int retval = 0;

    Trace((stderr, "\nstored block"));
    b = G.bb;
    k = G.bk;
    w = G.wp;

    n = k & 7;
    DUMPBITS(n);

    NEEDBITS(16);
    n = ((unsigned)b & 0xffff);
    DUMPBITS(16);
    NEEDBITS(16);
    if (n != (unsigned)((~b) & 0xffff))
        return 1;
    DUMPBITS(16);

    while (n--) {
        NEEDBITS(8);
        redirSlide[w++] = (uch)b;
        if (w == wsize) {
            if ((retval = FLUSH(w)) != 0)
                goto cleanup_and_exit;
            w = 0;
        }
        DUMPBITS(8);
    }

    G.wp = (unsigned)w;
    G.bb = b;
    G.bk = k;

cleanup_and_exit:
    return retval;
}

static int inflate_fixed(__G) __GDEF {
    Trace((stderr, "\nliteral block"));
    if (G.fixed_tl == (struct huft*)NULL) {
        int i;
        unsigned l[288];

        for (i = 0; i < 144; i++)
            l[i] = 8;
        for (; i < 256; i++)
            l[i] = 9;
        for (; i < 280; i++)
            l[i] = 7;
        for (; i < 288; i++)
            l[i] = 8;
        G.fixed_bl = 7;
#ifdef USE_DEFLATE64
        if ((i = huft_build(__G__ l, 288, 257, G.cplens, G.cplext, &G.fixed_tl, &G.fixed_bl)) != 0)
#else
        if ((i = huft_build(__G__ l, 288, 257, cplens, cplext, &G.fixed_tl, &G.fixed_bl)) != 0)
#endif
        {
            G.fixed_tl = (struct huft*)NULL;
            return i;
        }

        for (i = 0; i < MAXDISTS; i++)
            l[i] = 5;
        G.fixed_bd = 5;
#ifdef USE_DEFLATE64
        if ((i = huft_build(__G__ l, MAXDISTS, 0, cpdist, G.cpdext, &G.fixed_td, &G.fixed_bd)) > 1)
#else
        if ((i = huft_build(__G__ l, MAXDISTS, 0, cpdist, cpdext, &G.fixed_td, &G.fixed_bd)) > 1)
#endif
        {
            huft_free(G.fixed_tl);
            G.fixed_td = G.fixed_tl = (struct huft*)NULL;
            return i;
        }
    }

    return inflate_codes(__G__ G.fixed_tl, G.fixed_td, G.fixed_bl, G.fixed_bd);
}

static int inflate_dynamic(__G) __GDEF {
    unsigned i;
    unsigned j;
    unsigned l;
    unsigned m;
    unsigned n;
    struct huft* tl = (struct huft*)NULL;
    struct huft* td = (struct huft*)NULL;
    struct huft* th;
    unsigned bl;
    unsigned bd;
    unsigned nb;
    unsigned nl;
    unsigned nd;
    unsigned ll[MAXLITLENS + MAXDISTS];
    register ulg b;
    register unsigned k;
    int retval = 0;

    Trace((stderr, "\ndynamic block"));
    b = G.bb;
    k = G.bk;

    NEEDBITS(5);
    nl = 257 + ((unsigned)b & 0x1f);
    DUMPBITS(5);
    NEEDBITS(5);
    nd = 1 + ((unsigned)b & 0x1f);
    DUMPBITS(5);
    NEEDBITS(4);
    nb = 4 + ((unsigned)b & 0xf);
    DUMPBITS(4);
    if (nl > MAXLITLENS || nd > MAXDISTS)
        return 1;

    for (j = 0; j < nb; j++) {
        NEEDBITS(3);
        ll[border[j]] = (unsigned)b & 7;
        DUMPBITS(3);
    }
    for (; j < 19; j++)
        ll[border[j]] = 0;

    bl = 7;
    retval = huft_build(__G__ ll, 19, 19, NULL, NULL, &tl, &bl);
    if (bl == 0)
        retval = 1;
    if (retval) {
        if (retval == 1)
            huft_free(tl);
        return retval;
    }

    n = nl + nd;
    m = mask_bits[bl];
    i = l = 0;
    while (i < n) {
        NEEDBITS(bl);
        j = (th = tl + ((unsigned)b & m))->b;
        DUMPBITS(j);
        j = th->v.n;
        if (j < 16)
            ll[i++] = l = j;
        else if (j == 16) {
            NEEDBITS(2);
            j = 3 + ((unsigned)b & 3);
            DUMPBITS(2);
            if ((unsigned)i + j > n) {
                huft_free(tl);
                return 1;
            }
            while (j--)
                ll[i++] = l;
        }
        else if (j == 17) {
            NEEDBITS(3);
            j = 3 + ((unsigned)b & 7);
            DUMPBITS(3);
            if ((unsigned)i + j > n) {
                huft_free(tl);
                return 1;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
        else { /* j == 18 */
            NEEDBITS(7);
            j = 11 + ((unsigned)b & 0x7f);
            DUMPBITS(7);
            if ((unsigned)i + j > n) {
                huft_free(tl);
                return 1;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
    }

    huft_free(tl);

    G.bb = b;
    G.bk = k;

    bl = lbits;
#ifdef USE_DEFLATE64
    retval = huft_build(__G__ ll, nl, 257, G.cplens, G.cplext, &tl, &bl);
#else
    retval = huft_build(__G__ ll, nl, 257, cplens, cplext, &tl, &bl);
#endif
    if (bl == 0)
        retval = 1;
    if (retval) {
        if (retval == 1) {
            if (!uO.qflag)
                MESSAGE((uch*)"(incomplete l-tree)  ", 21L, 1);
            huft_free(tl);
        }
        return retval;
    }
#ifdef FIX_PAST_EOB_BY_TABLEADJUST
    bd = (dbits <= bl + 1 ? dbits : bl + 1);
#else
    bd = dbits;
#endif
#ifdef USE_DEFLATE64
    retval = huft_build(__G__ ll + nl, nd, 0, cpdist, G.cpdext, &td, &bd);
#else
    retval = huft_build(__G__ ll + nl, nd, 0, cpdist, cpdext, &td, &bd);
#endif
#ifdef PKZIP_BUG_WORKAROUND
    if (retval == 1)
        retval = 0;
#endif
    if (bd == 0 && nl > 257)
        retval = 1;
    if (retval) {
        if (retval == 1) {
            if (!uO.qflag)
                MESSAGE((uch*)"(incomplete d-tree)  ", 21L, 1);
            huft_free(td);
        }
        huft_free(tl);
        return retval;
    }

    retval = inflate_codes(__G__ tl, td, bl, bd);

cleanup_and_exit:
    if (tl != (struct huft*)NULL)
        huft_free(tl);
    if (td != (struct huft*)NULL)
        huft_free(td);
    return retval;
}

static int inflate_block(__G__ e) __GDEF int* e;
{
    unsigned t;
    register ulg b;
    register unsigned k;
    int retval = 0;

    b = G.bb;
    k = G.bk;

    NEEDBITS(1);
    *e = (int)b & 1;
    DUMPBITS(1);

    NEEDBITS(2);
    t = (unsigned)b & 3;
    DUMPBITS(2);

    G.bb = b;
    G.bk = k;

    if (t == 2)
        return inflate_dynamic(__G);
    if (t == 0)
        return inflate_stored(__G);
    if (t == 1)
        return inflate_fixed(__G);

    retval = 2;
cleanup_and_exit:
    return retval;
}

int inflate(__G__ is_defl64) __GDEF int is_defl64;
{
    int e;
    int r;
#ifdef DEBUG
    unsigned h = 0;
#endif

#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    if (G.redirect_slide)
        wsize = G.redirect_size, redirSlide = G.redirect_buffer;
    else
        wsize = WSIZE, redirSlide = slide;
#endif

    G.wp = 0;
    G.bk = 0;
    G.bb = 0;

#ifdef USE_DEFLATE64
    if (is_defl64) {
        G.cplens = cplens64;
        G.cplext = cplext64;
        G.cpdext = cpdext64;
        G.fixed_tl = G.fixed_tl64;
        G.fixed_bl = G.fixed_bl64;
        G.fixed_td = G.fixed_td64;
        G.fixed_bd = G.fixed_bd64;
    }
    else {
        G.cplens = cplens32;
        G.cplext = cplext32;
        G.cpdext = cpdext32;
        G.fixed_tl = G.fixed_tl32;
        G.fixed_bl = G.fixed_bl32;
        G.fixed_td = G.fixed_td32;
        G.fixed_bd = G.fixed_bd32;
    }
#else
    if (is_defl64) {
        Trace((stderr, "\nThis inflate() cannot handle Deflate64!\n"));
        return 2;
    }
#endif

    do {
#ifdef DEBUG
        G.hufts = 0;
#endif
        if ((r = inflate_block(__G__ & e)) != 0)
            return r;
#ifdef DEBUG
        if (G.hufts > h)
            h = G.hufts;
#endif
    } while (!e);

    Trace((stderr, "\n%u bytes in Huffman tables (%u/entry)\n", h * (unsigned)sizeof(struct huft), (unsigned)sizeof(struct huft)));

#ifdef USE_DEFLATE64
    if (is_defl64) {
        G.fixed_tl64 = G.fixed_tl;
        G.fixed_bl64 = G.fixed_bl;
        G.fixed_td64 = G.fixed_td;
        G.fixed_bd64 = G.fixed_bd;
    }
    else {
        G.fixed_tl32 = G.fixed_tl;
        G.fixed_bl32 = G.fixed_bl;
        G.fixed_td32 = G.fixed_td;
        G.fixed_bd32 = G.fixed_bd;
    }
#endif

    return (FLUSH(G.wp));
}

int inflate_free(__G) __GDEF {
    if (G.fixed_tl != (struct huft*)NULL) {
        huft_free(G.fixed_td);
        huft_free(G.fixed_tl);
        G.fixed_td = G.fixed_tl = (struct huft*)NULL;
    }
    return 0;
}

#endif /* ?USE_ZLIB */

/*
 * GRR:  moved huft_build() and huft_free() down here; used by explode()
 *       and fUnZip regardless of whether USE_ZLIB defined or not
 */

#define BMAX 16
#define N_MAX 288

int huft_build(__G__ b, n, s, d, e, t, m)
__GDEF
ZCONST unsigned* b;
unsigned n;
unsigned s;
ZCONST ush* d;
ZCONST uch* e;
struct huft** t;
unsigned* m;
{
    unsigned a;
    unsigned c[BMAX + 1];
    unsigned el;
    unsigned f;
    int g;
    int h;
    register unsigned i;
    register unsigned j;
    register int k;
    int lx[BMAX + 1];
    int* l = lx + 1;
    register unsigned* p;
    register struct huft* q;
    struct huft r;
    struct huft* u[BMAX];
    unsigned v[N_MAX];
    register int w;
    unsigned x[BMAX + 1];
    unsigned* xp;
    int y;
    unsigned z;

    el = n > 256 ? b[256] : BMAX;
    memzero((char*)c, sizeof(c));
    p = (unsigned*)b;
    i = n;
    do {
        c[*p]++;
        p++;
    } while (--i);
    if (c[0] == n) {
        *t = (struct huft*)NULL;
        *m = 0;
        return 0;
    }

    for (j = 1; j <= BMAX; j++)
        if (c[j])
            break;
    k = j;
    if (*m < j)
        *m = j;
    for (i = BMAX; i; i--)
        if (c[i])
            break;
    g = i;
    if (*m > i)
        *m = i;

    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= c[j]) < 0)
            return 2;
    if ((y -= c[i]) < 0)
        return 2;
    c[i] += y;

    x[1] = j = 0;
    p = c + 1;
    xp = x + 2;
    while (--i) {
        *xp++ = (j += *p++);
    }

    memzero((char*)v, sizeof(v));
    p = (unsigned*)b;
    i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    n = x[g];

    x[0] = i = 0;
    p = v;
    h = -1;
    w = l[-1] = 0;
    u[0] = (struct huft*)NULL;
    q = (struct huft*)NULL;
    z = 0;

    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            while (k > w + l[h]) {
                w += l[h++];

                z = (z = g - w) > *m ? *m : z;
                if ((f = 1 << (j = k - w)) > a + 1) {
                    f -= a + 1;
                    xp = c + k;
                    while (++j < z) {
                        if ((f <<= 1) <= *++xp)
                            break;
                        f -= *xp;
                    }
                }
                if ((unsigned)w + j > el && (unsigned)w < el)
                    j = el - w;
                z = 1 << j;
                l[h] = j;

                if ((q = (struct huft*)malloc((z + 1) * sizeof(struct huft))) == (struct huft*)NULL) {
                    if (h)
                        huft_free(u[0]);
                    return 3;
                }
#ifdef DEBUG
                G.hufts += z + 1;
#endif
                *t = q + 1;
                *(t = &(q->v.t)) = (struct huft*)NULL;
                u[h] = ++q;

                if (h) {
                    x[h] = i;
                    r.b = (uch)l[h - 1];
                    r.e = (uch)(32 + j);
                    r.v.t = q;
                    j = (i & ((1 << w) - 1)) >> (w - l[h - 1]);
                    u[h - 1][j] = r;
                }
            }

            r.b = (uch)(k - w);
            if (p >= v + n)
                r.e = INVALID_CODE;
            else if (*p < s) {
                r.e = (uch)(*p < 256 ? 32 : 31);
                r.v.n = (ush)*p++;
            }
            else {
                r.e = e[*p - s];
                r.v.n = d[*p++ - s];
            }

            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            while ((i & ((1 << w) - 1)) != x[h])
                w -= l[--h];
        }
    }

    *m = l[0];
    return y != 0 && g != 1;
}

int huft_free(t)
struct huft* t;
{
    register struct huft *p, *q;
    p = t;
    while (p != (struct huft*)NULL) {
        q = (--p)->v.t;
        free((zvoid*)p);
        p = q;
    }
    return 0;
}
