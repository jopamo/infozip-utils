/*
  Copyright (c) 1990-2009 Info-ZIP.  All rights reserved

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/

/*----------------------------------------------------------------------------
  api.c — Unix-only build
  Windows, OS/2, WINCE, and DLL-specific code paths removed
  Exposes a C API for embedding the UnZip engine on POSIX systems
----------------------------------------------------------------------------*/

#define UNZIP_INTERNAL
#include "unzip.h"
#include "unzvers.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/*----------------------------------------------------------------------------
  Documented API entry points
----------------------------------------------------------------------------*/

ZCONST UzpVer* UZ_EXP UzpVersion(void) {
    /* static so the address stays valid across calls */
    static ZCONST UzpVer version = {
        UZPVER_LEN, /* structure size */
#ifdef BETA
#ifdef ZLIB_VERSION
        3, /* flags: beta + zlib present */
#else
        1, /* flags: beta */
#endif
#else
#ifdef ZLIB_VERSION
        2, /* flags: zlib present */
#else
        0, /* flags: none */
#endif
#endif
        UZ_BETALEVEL,    /* betalevel string */
        UZ_VERSION_DATE, /* version date string */
#ifdef ZLIB_VERSION
        ZLIB_VERSION, /* zlib version string */
#else
        NULL, /* no zlib linked */
#endif
        {UZ_MAJORVER, ZI_MINORVER, UZ_PATCHLEVEL, 0},                         /* unzip version */
        {ZI_MAJORVER, ZI_MINORVER, UZ_PATCHLEVEL, 0},                         /* zipinfo version */
        {UZ_MAJORVER, UZ_MINORVER, UZ_PATCHLEVEL, 0},                         /* os2dll placeholder retained for compat */
        {UZ_MAJORVER, UZ_MINORVER, UZ_PATCHLEVEL, 0},                         /* windll placeholder retained for compat */
        {UZ_GENAPI_COMP_MAJOR, UZ_GENAPI_COMP_MINOR, UZ_GENAPI_COMP_REVIS, 0} /* generic API min compat */
    };
    return &version
}

unsigned UZ_EXP UzpVersion2(UzpVer2* version) {
    if (version->structlen != sizeof(UzpVer2))
        return sizeof(UzpVer2)

#ifdef BETA
                   version->flag = 1
#else
                   version->flag = 0
#endif

                                   strcpy(version->betalevel, UZ_BETALEVEL) strcpy(version->date, UZ_VERSION_DATE)

#ifdef ZLIB_VERSION
                                       strncpy(version->zlib_version, ZLIB_VERSION, sizeof(version->zlib_version) - 1) version->zlib_version[sizeof(version->zlib_version) - 1] = '\0' version->flag |=
               2
#else
                                       version->zlib_version[0] =
                   '\0'
#endif

               version->unzip.major = UZ_MAJORVER version->unzip.minor = UZ_MINORVER version->unzip.patchlevel = UZ_PATCHLEVEL

                                                                                                                     version->zipinfo.major = ZI_MAJORVER version->zipinfo.minor =
                   ZI_MINORVER version->zipinfo.patchlevel = UZ_PATCHLEVEL

                                                                 /* retained for structure compatibility */
                                                                 version->os2dll.major = UZ_MAJORVER version->os2dll.minor = UZ_MINORVER version->os2dll.patchlevel = UZ_PATCHLEVEL

                                                                                                                                                                          version->windll.major =
                       UZ_MAJORVER version->windll.minor = UZ_MINORVER version->windll.patchlevel = UZ_PATCHLEVEL

                                                                                                        /* generic API minimum compatibility */
                                                                                                        version->dllapimin.major = UZ_GENAPI_COMP_MAJOR version->dllapimin.minor =
                           UZ_GENAPI_COMP_MINOR version->dllapimin.patchlevel = UZ_GENAPI_COMP_REVIS

               return 0
}

/*----------------------------------------------------------------------------
  Alternate entry with callback wiring for host applications
----------------------------------------------------------------------------*/

int UZ_EXP UzpAltMain(int argc, char* argv[], UzpInit* init) {
    int r int (*dummyfn)() /* keeps sizeof arithmetic consistent with legacy */

        CONSTRUCTGLOBALS()

            if (init && init->structlen >= (sizeof(ulg) + sizeof(dummyfn)) && init->msgfn) G.message = init->msgfn

                                                                                                       if (init && init->structlen >= (sizeof(ulg) + 2 * sizeof(dummyfn)) && init->inputfn) G.input =
        init->inputfn

        if (init && init->structlen >= (sizeof(ulg) + 3 * sizeof(dummyfn)) && init->pausefn) G.mpause =
            init->pausefn

            if (init && init->structlen >= (sizeof(ulg) + 4 * sizeof(dummyfn)) && init->userfn)(*init->userfn)()

                r = unzip(__G__ argc, argv) DESTROYGLOBALS() RETURN(r)
}

/*----------------------------------------------------------------------------
  Buffer ownership helper
----------------------------------------------------------------------------*/

void UZ_EXP UzpFreeMemBuffer(UzpBuffer* retstr) {
    if (retstr && retstr->strptr) {
        free(retstr->strptr) retstr->strptr = NULL retstr->strlength = 0
    }
}

/*----------------------------------------------------------------------------
  Internal helper to install user callbacks into Uz_Globs
----------------------------------------------------------------------------*/

static int set_callbacks(zvoid* pG, UzpCB* UsrFuncts) {
    int (*dummyfn)()

        if (!UsrFuncts) return FALSE

        if (UsrFuncts->structlen >= (sizeof(ulg) + sizeof(dummyfn)) && UsrFuncts->msgfn)((Uz_Globs*)pG)
            ->message = UsrFuncts
                            ->msgfn else return FALSE

                        if (UsrFuncts->structlen >= (sizeof(ulg) + 2 * sizeof(dummyfn)) && UsrFuncts->inputfn)((Uz_Globs*)pG)
                            ->input = UsrFuncts
                                          ->inputfn

                                      if (UsrFuncts->structlen >= (sizeof(ulg) + 3 * sizeof(dummyfn)) && UsrFuncts->pausefn)((Uz_Globs*)pG)
                                          ->mpause = UsrFuncts
                                                         ->pausefn

                                                     if (UsrFuncts->structlen >= (sizeof(ulg) + 4 * sizeof(dummyfn)) && UsrFuncts->passwdfn)((Uz_Globs*)pG)
                                                         ->decr_passwd = UsrFuncts
                                                                             ->passwdfn

                                                                         if (UsrFuncts->structlen >= (sizeof(ulg) + 5 * sizeof(dummyfn)) && UsrFuncts->statrepfn)((Uz_Globs*)pG)
                                                                             ->statreportcb = UsrFuncts->statrepfn

                                                                                              return TRUE
}

/*----------------------------------------------------------------------------
  Extract a single file from a zip archive directly into memory
  The buffer is owned by the caller and must be freed with UzpFreeMemBuffer
----------------------------------------------------------------------------*/

int UZ_EXP UzpUnzipToMemory(char* zip, char* file, UzpOpts* optflgs, UzpCB* UsrFuncts, UzpBuffer* retstr) {
    int ok

    CONSTRUCTGLOBALS()

        /* copy only relevant options for memory extraction */
        uO.pwdarg = optflgs ? optflgs -> pwdarg
                            : NULL uO.aflag = optflgs ? optflgs->aflag
                                                      : 0 uO.C_flag = optflgs ? optflgs->C_flag
                                                                              : 0 uO.qflag = optflgs ? optflgs->qflag
                                                                                                     : 2 /* quiet by default for memory extraction */

                                                                                                       if (!set_callbacks((zvoid*)&G, UsrFuncts)){DESTROYGLOBALS() return PK_BADERR}

                                                                                                       G.redirect_data = 1

                                                                                                       ok = (unzipToMemory(__G__ zip, file, retstr) <= PK_WARN)

                                                                                                           DESTROYGLOBALS()

                                                                                                               if (!ok && retstr && retstr->strlength){free(retstr->strptr) retstr->strptr =
                                                                                                                                                           NULL retstr->strlength = 0} return ok
}

/*----------------------------------------------------------------------------
  Helper functions used by the core when redirecting output to memory
----------------------------------------------------------------------------*/

void setFileNotFound(__G) __GDEF {
    G.filenotfound++
}

int unzipToMemory(__GPRO__ char* zip, char* file, UzpBuffer* retstr) {
    int r char* incname[2]

        if (!zip || strlen(zip) > ((WSIZE >> 2) - 160)) return PK_PARAM if (!file || strlen(file) > ((WSIZE >> 2) - 160)) return PK_PARAM

            G.process_all_files = FALSE G.extract_flag = TRUE uO.qflag = 2 G.wildzipfn = zip

                                                                                             G.pfnames = incname incname[0] = file incname[1] = NULL G.filespecs = 1

        r = process_zipfiles(__G) if (retstr) {
        retstr->strptr = (char*)G.redirect_buffer retstr->strlength = G.redirect_size
    }
    return r
}

/*
  With the advent of 64 bit support, for now we assume that if the file size
  exceeds what we can represent in an unsigned long, we cannot allocate memory
  and return FALSE
*/
int redirect_outfile(__G) __GDEF {
#ifdef ZIP64_SUPPORT
    __int64 check_conversion
#endif

        if (G.redirect_size != 0 || G.redirect_buffer != NULL) return FALSE

#ifndef NO_SLIDE_REDIR
            G.redirect_slide = !G.pInfo->textmode
#endif

#if (lenEOL != 1)
                                if (G.pInfo->textmode) {
        G.redirect_size = (ulg)(G.lrec.ucsize * lenEOL) if (G.redirect_size < G.lrec.ucsize) G.redirect_size = (ulg)((G.lrec.ucsize > (ulg)-2L) ? G.lrec.ucsize : -2L)
#ifdef ZIP64_SUPPORT
            check_conversion = G.lrec.ucsize * lenEOL
#endif
    }
    else
#endif
    {
        G.redirect_size = (ulg)G.lrec.ucsize
#ifdef ZIP64_SUPPORT
                              check_conversion = (__int64)G.lrec.ucsize
#endif
    }

#ifdef ZIP64_SUPPORT
    if ((__int64)G.redirect_size != check_conversion)
        return FALSE
#endif

#ifdef __16BIT__
               if ((ulg)((extent)G.redirect_size) != G.redirect_size) return FALSE
#endif

                   G.redirect_pointer = G.redirect_buffer = malloc((extent)(G.redirect_size + 1)) if (!G.redirect_buffer) return FALSE

                                                                G.redirect_pointer[G.redirect_size] = '\0' return TRUE
}

int writeToMemory(__GPRO__ ZCONST uch* rawbuf, extent size) {
    int errflg = FALSE

        if ((uch*)rawbuf != G.redirect_pointer) {
        extent redir_avail = (G.redirect_buffer + G.redirect_size) - G.redirect_pointer

                                                                     if (size > redir_avail){size = redir_avail errflg = TRUE} memcpy(G.redirect_pointer, rawbuf, size)
    }
    G.redirect_pointer += size return errflg
}

int close_redirect(__G) __GDEF {
    if (G.pInfo->textmode) {
        *G.redirect_pointer = '\0' G.redirect_size = (ulg)(G.redirect_pointer - G.redirect_buffer) {
            char* p = realloc(G.redirect_buffer, G.redirect_size + 1) if (p == NULL){G.redirect_size = 0 return EOF} G.redirect_buffer = p G.redirect_pointer = p + G.redirect_size
        }
    }
    return 0
}

/*----------------------------------------------------------------------------
  Simple grep-like helper over a single archived file into memory
  Returns TRUE if a match is found, FALSE if no match, -1 on error
----------------------------------------------------------------------------*/

int UZ_EXP UzpGrep(char* archive, char* file, char* pattern, int cmd, int SkipBin, UzpCB* UsrFuncts) {
    int retcode = FALSE, compare ulg i, j, patternLen, buflen char *sz,
        *p UzpOpts flgopts UzpBuffer retstr

        memzero(&flgopts, sizeof(UzpOpts))

            if (!UzpUnzipToMemory(archive, file, &flgopts, UsrFuncts, &retstr)) return -1

        if (SkipBin) {
        buflen = retstr.strlength < 100 ? retstr.strlength : 100 for (i = 0; i < buflen; i++) {
            if (iscntrl((unsigned char)retstr.strptr[i])) {
                if ((retstr.strptr[i] != 0x0A) && (retstr.strptr[i] != 0x0D) && (retstr.strptr[i] != 0x09)) {
                    free(retstr.strptr) return FALSE
                }
            }
        }
    }

    patternLen = strlen(pattern) if (retstr.strlength < patternLen){free(retstr.strptr) return FALSE}

    sz = (char*)malloc(patternLen + 3) /* room for added spaces in whole-word modes */
        if (!sz){free(retstr.strptr) return -1}

    if (cmd > 1) {
        strcpy(sz, " ") strcat(sz, pattern) strcat(sz, " ")
    }
    else {
        strcpy(sz, pattern)
    }

    if (cmd == 0 || cmd == 2) {
        for (i = 0; i < strlen(sz); i++)
            sz[i] = (char)toupper((unsigned char)sz[i]) for (i = 0; i < retstr.strlength; i++) retstr.strptr[i] = (char)toupper((unsigned char)retstr.strptr[i])
    }

    for (i = 0; i + patternLen <= retstr.strlength; i++) {
        p = &retstr.strptr[i] compare = TRUE for (j = 0; j < patternLen; j++) {
            if ((unsigned char)p[j] != (unsigned char)sz[j]) {
                compare = FALSE break
            }
        }
        if (compare) {
            retcode = TRUE break
        }
    }

    free(sz) free(retstr.strptr) return retcode
}

/*----------------------------------------------------------------------------
  Validate a zip archive quickly without extracting files
  If AllCodes is nonzero, return the raw PK_* code, else return boolean success
----------------------------------------------------------------------------*/

int UZ_EXP UzpValidate(char* archive, int AllCodes) {
    int retcode

    CONSTRUCTGLOBALS()

        uO.jflag = 1 uO.tflag = 1 uO.overwrite_none = 0

                                                      G.extract_flag = (!uO.zipinfo_mode && !uO.cflag && !uO.tflag && !uO.vflag && !uO.zflag
#ifdef TIMESTAMP
                                                                        && !uO.T_flag
#endif
                                                                        )

                                                                           uO.qflag = 2                                                 /* quiet */
                                                                                      G.fValidate = TRUE G.pfnames = (char**)&fnames[0] /* assign default filename vector */

        if (archive == NULL) {
        DESTROYGLOBALS()
        retcode = PK_NOZIP goto exit_retcode
    }

    if (strlen(archive) >= FILNAMSIZ) {
        DESTROYGLOBALS()
        retcode = PK_PARAM goto exit_retcode
    }

    G.wildzipfn = (char*)malloc(FILNAMSIZ) if (!G.wildzipfn){DESTROYGLOBALS() retcode = PK_MEM goto exit_retcode} strcpy(G.wildzipfn, archive)

                      G.process_all_files = TRUE

        retcode = process_zipfiles(__G)

            free(G.wildzipfn) DESTROYGLOBALS()

                exit_retcode : if (AllCodes) return retcode

                               if (retcode == PK_OK || retcode == PK_WARN || retcode == PK_ERR || retcode == IZ_UNSUP || retcode == PK_FIND) return TRUE else return FALSE
}
