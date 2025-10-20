/*---------------------------------------------------------------------------

  unshrink.c                     version 1.22                     19 Mar 2008

  see original header for licensing notes

  ---------------------------------------------------------------------------*/

#define __UNSHRINK_C /* identifies this source module */
#define UNZIP_INTERNAL
#include "unzip.h"

#ifndef LZW_CLEAN

#include <string.h>    /* memcpy */
#if defined(__GNUC__) || defined(__clang__)
#  define LIKELY(x)   __builtin_expect(!!(x), 1)
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define HOT         __attribute__((hot))
#  define COLD        __attribute__((cold))
#else
#  define LIKELY(x)   (x)
#  define UNLIKELY(x) (x)
#  define HOT
#  define COLD
#endif

static void partial_clear OF((__GPRO__ int lastcodeused));

#ifdef DEBUG
#define OUTDBG(c)                         \
    if ((c) < 32 || (c) >= 127)           \
        fprintf(stderr, "\\x%02x", (c));  \
    else                                  \
        putc((c), stderr)
#else
#define OUTDBG(c) do { } while (0)
#endif

/* HSIZE is defined as 2^13 (8192) in unzip.h or unzpriv.h */
#define BOGUSCODE 256
#define FLAG_BITS parent       /* upper bits of parent[] used as flag bits */
#define CODE_MASK (HSIZE - 1)  /* 0x1fff lower bits are parent's index */
#define FREE_CODE HSIZE        /* 0x2000 code is unused or was cleared */
#define HAS_CHILD (HSIZE << 1) /* 0x4000 code has a child do not clear */

#define parent G.area.shrink.Parent
#define Value  G.area.shrink.value
#define stack  G.area.shrink.Stack

/* write as much as possible to outbuf with a single flush check */
static inline int HOT
emit_bytes(__GPRO__ const uch *src, unsigned len, uch **outp, unsigned *cnt,
           uch *realbuf, unsigned outbufsiz)
{
    /* fast path when everything fits */
    if (LIKELY(*cnt + len <= outbufsiz)) {
        memcpy(*outp, src, len);
        *outp += len;
        *cnt  += len;
        return 0;
    }

    /* fill remainder, flush, then write the rest */
    unsigned room = outbufsiz - *cnt;
    if (room) {
        memcpy(*outp, src, room);
        *outp += room;
        *cnt  += room;
        if (UNLIKELY(flush(__G__ realbuf, *cnt, TRUE) != 0))
            return PK_ERR;
        *outp = realbuf;
        *cnt  = 0;
        src  += room;
        len  -= room;
    }

    /* for large leftover, chunk in whole buffer sized blocks to reduce loop overhead */
    while (UNLIKELY(len >= outbufsiz)) {
        memcpy(*outp, src, outbufsiz);
        *outp += outbufsiz;
        *cnt  += outbufsiz;
        if (UNLIKELY(flush(__G__ realbuf, *cnt, TRUE) != 0))
            return PK_ERR;
        *outp = realbuf;
        *cnt  = 0;
        src  += outbufsiz;
        len  -= outbufsiz;
    }

    if (len) {
        memcpy(*outp, src, len);
        *outp += len;
        *cnt  += len;
    }
    return 0;
}

/***********************/
/* Function unshrink() */
/***********************/

int HOT unshrink(__G) __GDEF
{
    uch *stacktop = stack + (HSIZE - 1);
    register uch *newstr;
    uch finalval;
    int codesize = 9, len, error;
    shrint code, oldcode, curcode;
    shrint lastfreecode;
    unsigned int outbufsiz;
#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    /* Normally realbuf and outbuf will be the same
     * if redirected to a large memory buffer, realbuf points there while outbuf remains malloc'd */
    uch *realbuf = G.outbuf;
#else
#   define realbuf G.outbuf
#endif

    lastfreecode = BOGUSCODE;

#ifndef VMS
#ifndef SMALL_MEM
    /* allocate second large buffer for textmode conversion only when needed */
    if (G.pInfo->textmode && !G.outbuf2 && (G.outbuf2 = (uch *)malloc(TRANSBUFSIZ)) == (uch *)NULL)
        return PK_MEM3;
#endif
#endif /* !VMS */

    for (code = 0; code < BOGUSCODE; ++code) {
        Value[code]  = (uch)code;
        parent[code] = BOGUSCODE;
    }
    for (code = BOGUSCODE + 1; code < HSIZE; ++code)
        parent[code] = FREE_CODE;

#if (defined(DLL) && !defined(NO_SLIDE_REDIR))
    if (G.redirect_slide) {
        realbuf   = G.redirect_buffer;
        outbufsiz = (unsigned)G.redirect_size;
    } else
#endif
#ifdef DLL
        if (G.pInfo->textmode && !G.redirect_data)
#else
    if (G.pInfo->textmode)
#endif
        outbufsiz = RAWBUFSIZ;
    else
        outbufsiz = OUTBUFSIZ;

    G.outptr = realbuf;
    G.outcnt = 0L;

    /* get and output first code, then loop */
    READBITS(codesize, oldcode);
    if (G.zipeof)
        return PK_OK;

    finalval = (uch)oldcode;
    OUTDBG(finalval);
    *G.outptr++ = finalval;
    ++G.outcnt;

    while (1) {
        READBITS(codesize, code);
        if (G.zipeof)
            break;
        if (UNLIKELY(code == BOGUSCODE)) {
            READBITS(codesize, code);
            if (G.zipeof)
                break;
            if (code == 1) {
                ++codesize;
                Trace((stderr, " (codesize now %d bits)\n", codesize));
                if (UNLIKELY(codesize > MAX_BITS))
                    return PK_ERR;
            } else if (code == 2) {
                Trace((stderr, " (partial clear code)\n"));
                partial_clear(__G__ lastfreecode);
                Trace((stderr, " (done with partial clear)\n"));
                lastfreecode = BOGUSCODE;
            }
            continue;
        }

        /* translate code by walking parent links to the root, filling LIFO stack */
        newstr  = stacktop;
        curcode = code;

        if (UNLIKELY(parent[code] == FREE_CODE)) {
            Trace((stderr, " (found a KwKwK code %d; oldcode = %d)\n", code, oldcode));
            *newstr-- = finalval;
            code = oldcode;
        }

        while (code != BOGUSCODE) {
            if (UNLIKELY(newstr < stack)) {
                Trace((stderr, "unshrink stack overflow\n"));
                return PK_ERR;
            }
            if (UNLIKELY(parent[code] == FREE_CODE)) {
                Trace((stderr, " (found a KwKwK code %d; oldcode = %d)\n", code, oldcode));
                *newstr-- = finalval;
                code = oldcode;
            } else {
                *newstr-- = Value[code];
                code = (shrint)(parent[code] & CODE_MASK);
            }
        }

        len      = (int)(stacktop - newstr++);
        finalval = *newstr;

        Trace((stderr,
               "code %4d; oldcode %4d; char %3d (%c); len %d; string [",
               curcode, oldcode, (int)(*newstr),
               (*newstr < 32 || *newstr >= 127) ? ' ' : *newstr, len));

        /* write expanded string in forward order using memcpy when possible */
        if (UNLIKELY(len <= 0)) {
            /* nothing to emit */
        } else {
            /* fast bulk copy with single flush check in emit_bytes */
            error = emit_bytes(__G__ newstr, (unsigned)len,
                               &G.outptr, (unsigned *)&G.outcnt,
                               realbuf, outbufsiz);
            if (UNLIKELY(error != 0)) {
                Trace((stderr, "unshrink:  flush error %d\n", error));
                return error;
            }
#ifdef DEBUG
            {
                const uch *p;
                for (p = newstr; p < newstr + len; ++p) OUTDBG(*p);
            }
#endif
        }

        Trace((stderr, "]\n"));

        /* add new leaf first character of newstr as child of oldcode */
        code = (shrint)(lastfreecode + 1);
        while ((code < HSIZE) && (parent[code] != FREE_CODE))
            ++code;
        lastfreecode = code;
        Trace((stderr, "newcode %d\n", code));
        if (UNLIKELY(code >= HSIZE))
            return PK_ERR;

        Value[code]  = finalval;
        parent[code] = oldcode;
        oldcode      = curcode;
    }

    /* final pending flush */
    if (G.outcnt > 0L) {
        Trace((stderr, "final flush outcnt = %lu\n", G.outcnt));
        if (UNLIKELY((error = flush(__G__ realbuf, G.outcnt, TRUE)) != 0)) {
            Trace((stderr, "unshrink:  final flush error %d\n", error));
            return error;
        }
    }

    return PK_OK;
} /* end function unshrink */

/****************************/
/* Function partial_clear() */ /* no longer recursive */
/****************************/

static void COLD partial_clear(__G__ lastcodeused) __GDEF
int lastcodeused;
{
    register shrint code;

    /* mark all parents */
    for (code = BOGUSCODE + 1; code <= lastcodeused; ++code) {
        register shrint cparent = (shrint)(parent[code] & CODE_MASK);
        if (cparent > BOGUSCODE)
            FLAG_BITS[cparent] |= HAS_CHILD;  /* set parent child bit */
    }

    /* clear all nodes not marked as parents and reset flags */
    for (code = BOGUSCODE + 1; code <= lastcodeused; ++code) {
        if (FLAG_BITS[code] & HAS_CHILD)      /* just clear child bit */
            FLAG_BITS[code] &= ~HAS_CHILD;
        else {
            Trace((stderr, "%d\n", code));
            parent[code] = FREE_CODE;
        }
    }
}

#endif /* !LZW_CLEAN */
