/*
  Copyright (c) 1990-2009 Info-ZIP  All rights reserved

  See the accompanying file LICENSE, version 2009-Jan-02 or later
  (the contents of which are also included in unzip.h) for terms of use
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*---------------------------------------------------------------------------

  list.c

  This file contains the non-ZipInfo-specific listing routines for UnZip
  Linux/UNIX-only build with all Windows and OS/2 paths removed

  Contains:  list_files()
             get_time_stamp()   [optional feature]
             ratio()
             fnprint()

  ---------------------------------------------------------------------------*/

#define UNZIP_INTERNAL
#include "unzip.h"

#ifdef TIMESTAMP
   static int  fn_is_dir   OF((__GPRO));
#endif

/* unix-only header strings */

static ZCONST char Far CompFactorStr[] = "%c%d%%";
static ZCONST char Far CompFactor100[] = "100%%";

static ZCONST char Far HeadersS[]  =
  "  Length      Date    Time    Name";
static ZCONST char Far HeadersS1[] =
  "---------  ---------- -----   ----";

static ZCONST char Far HeadersL[]  =
  " Length   Method    Size  Cmpr    Date    Time   CRC-32   Name";
static ZCONST char Far HeadersL1[] =
  "--------  ------  ------- ---- ---------- ----- --------  ----";
static ZCONST char Far *Headers[][2] =
  { {HeadersS, HeadersS1}, {HeadersL, HeadersL1} };

static ZCONST char Far CaseConversion[] =
  "%s (\"^\" ==> case\n%s   conversion)\n";
static ZCONST char Far LongHdrStats[] =
  "%s  %-7s%s %4s %02u%c%02u%c%02u %02u:%02u %08lx %c";
static ZCONST char Far LongFileTrailer[] =
  "--------          -------  ---                       \
     -------\n%s         %s %4s                            %lu file%s\n";
static ZCONST char Far ShortHdrStats[] =
  "%s  %02u%c%02u%c%02u %02u:%02u  %c";
static ZCONST char Far ShortFileTrailer[] =
  "---------                     -------\n%s\
                     %lu file%s\n";

/*************************/
/* Function list_files() */
/*************************/

int list_files(__G)    /* return PK-type error code */
    __GDEF
{
    int do_this_file = FALSE;
    int cfactor, error, error_in_archive = PK_COOL;
    char sgn, cfactorstr[12];
    int longhdr = (uO.vflag > 1);
    int date_format;
    char dt_sepchar;
    ulg members = 0L;
    zusz_t j;
    unsigned methnum;
#ifdef USE_EF_UT_TIME
    iztimes z_utime;
    struct tm *t;
#endif
    unsigned yr, mo, dy, hh, mm;
    unsigned disp_y, disp_m, disp_d; /* explicit output order */
    zusz_t csiz, tot_csize = 0L, tot_ucsize = 0L;
    min_info info;
    char methbuf[8];
    static ZCONST char dtype[] = "NXFS";  /* see zi_short() */
    static ZCONST char Far method[NUM_METHODS+1][8] =
        {"Stored", "Shrunk", "Reduce1", "Reduce2", "Reduce3", "Reduce4",
         "Implode", "Token", "Defl:#", "Def64#", "ImplDCL", "BZip2",
         "LZMA", "Terse", "IBMLZ77", "WavPack", "PPMd", "Unk:###"};

    /* see notes below for output format example */

    G.pInfo = &info;
    date_format = DATE_FORMAT;
    dt_sepchar = DATE_SEPCHAR;

    if (uO.qflag < 2) {
        if (uO.L_flag)
            Info(slide, 0, ((char *)slide, LoadFarString(CaseConversion),
              LoadFarStringSmall(Headers[longhdr][0]),
              LoadFarStringSmall2(Headers[longhdr][1])));
        else
            Info(slide, 0, ((char *)slide, "%s\n%s\n",
               LoadFarString(Headers[longhdr][0]),
               LoadFarStringSmall(Headers[longhdr][1])));
    }

    for (j = 1L;; j++) {

        if (readbuf(__G__ G.sig, 4) == 0)
            return PK_EOF;

        if (memcmp(G.sig, central_hdr_sig, 4)) {
            if (((j - 1) &
                 (ulg)(G.ecrec.have_ecr64 ? MASK_ZUCN64 : MASK_ZUCN16))
                == (ulg)G.ecrec.total_entries_central_dir)
            {
                break;
            } else {
                Info(slide, 0x401,
                     ((char *)slide, LoadFarString(CentSigMsg), j));
                Info(slide, 0x401,
                     ((char *)slide, "%s", LoadFarString(ReportMsg)));
                return PK_BADERR;
            }
        }

        if ((error = process_cdir_file_hdr(__G)) != PK_COOL)
            return error;

        if ((error = do_string(__G__ G.crec.filename_length, DS_FN)) != PK_COOL)
        {
            error_in_archive = error;
            if (error > PK_WARN)
                return error;
        }

        if (G.extra_field != (uch *)NULL) {
            free(G.extra_field);
            G.extra_field = (uch *)NULL;
        }
        if ((error = do_string(__G__ G.crec.extra_field_length, EXTRA_FIELD)) != 0)
        {
            error_in_archive = error;
            if (error > PK_WARN)
                return error;
        }

        if (!G.process_all_files) {
            unsigned i;

            if (G.filespecs == 0)
                do_this_file = TRUE;
            else {
                do_this_file = FALSE;
                for (i = 0; i < G.filespecs; i++)
                    if (match(G.filename, G.pfnames[i], uO.C_flag WISEP)) {
                        do_this_file = TRUE;
                        break;
                    }
            }
            if (do_this_file) {
                for (i = 0; i < G.xfilespecs; i++)
                    if (match(G.filename, G.pxnames[i], uO.C_flag WISEP)) {
                        do_this_file = FALSE;
                        break;
                    }
            }
        }

        if (G.process_all_files || do_this_file) {

#ifdef USE_EF_UT_TIME
            if (G.extra_field &&
#ifdef IZ_CHECK_TZ
                G.tz_is_valid &&
#endif
                (ef_scan_for_izux(G.extra_field, G.crec.extra_field_length, 1,
                                  G.crec.last_mod_dos_datetime, &z_utime, NULL)
                 & EB_UT_FL_MTIME))
            {
                TIMET_TO_NATIVE(z_utime.mtime)
                t = localtime(&(z_utime.mtime));
            } else
                t = (struct tm *)NULL;

            if (t != (struct tm *)NULL) {
                mo = (unsigned)(t->tm_mon + 1);
                dy = (unsigned)(t->tm_mday);
                yr = (unsigned)(t->tm_year + 1900);
                hh = (unsigned)(t->tm_hour);
                mm = (unsigned)(t->tm_min);
            } else
#endif /* USE_EF_UT_TIME */
            {
                yr = (unsigned)(((G.crec.last_mod_dos_datetime >> 25) & 0x7f) + 1980);
                mo = (unsigned)((G.crec.last_mod_dos_datetime >> 21) & 0x0f);
                dy = (unsigned)((G.crec.last_mod_dos_datetime >> 16) & 0x1f);
                hh = (unsigned)((G.crec.last_mod_dos_datetime >> 11) & 0x1f);
                mm = (unsigned)((G.crec.last_mod_dos_datetime >> 5)  & 0x3f);
            }

            /* map to display order without mutating yr/mo/dy */
            switch (date_format) {
                case DF_YMD:
                    disp_y = yr; disp_m = mo; disp_d = dy;
                    break;
                case DF_DMY:
                    disp_y = yr; disp_m = dy; disp_d = mo;
                    break;
                case DF_MDY:
                default:
                    disp_y = yr; disp_m = mo; disp_d = dy;
                    break;
            }

            csiz = G.crec.csize;
            if (G.crec.general_purpose_bit_flag & 1)
                csiz -= 12;   /* discount encryption header */

            if ((cfactor = ratio(G.crec.ucsize, csiz)) < 0) {
                sgn = '-';
                cfactor = (-cfactor + 5) / 10;
            } else {
                sgn = ' ';
                cfactor = (cfactor + 5) / 10;
            }

            methnum = find_compr_idx(G.crec.compression_method);

            /* fill method safely into fixed 7-char column, null-terminated */
            if (methnum < NUM_METHODS) {
                zfstrcpy(methbuf, method[methnum]);
            } else {
                /* unknown method number in decimal up to 999 or hex otherwise */
                if (G.crec.compression_method <= 999)
                    snprintf(methbuf, sizeof(methbuf), "Unk:%03u",
                             G.crec.compression_method);
                else
                    snprintf(methbuf, sizeof(methbuf), "Unk:%04X",
                             G.crec.compression_method);
            }

            if (G.crec.compression_method == DEFLATED ||
                G.crec.compression_method == ENHDEFLATED) {
                /* overwrite the trailing # with deflate type code from dtype */
                size_t len = strlen(methbuf);
                if (len >= 6) {
                    methbuf[5] = dtype[(G.crec.general_purpose_bit_flag >> 1) & 3];
                    methbuf[6] = '\0';
                }
            }

            if (cfactor == 100)
                snprintf(cfactorstr, sizeof(cfactorstr),
                         LoadFarString(CompFactor100));
            else
                snprintf(cfactorstr, sizeof(cfactorstr),
                         LoadFarString(CompFactorStr), sgn, cfactor);

            if (longhdr)
                Info(slide, 0, ((char *)slide, LoadFarString(LongHdrStats),
                  FmZofft(G.crec.ucsize, "8", "u"), methbuf,
                  FmZofft(csiz, "8", "u"), cfactorstr,
                  disp_m, dt_sepchar, disp_d, dt_sepchar, disp_y, hh, mm,
                  G.crec.crc32, (G.pInfo->lcflag ? '^' : ' ')));
            else
                Info(slide, 0, ((char *)slide, LoadFarString(ShortHdrStats),
                  FmZofft(G.crec.ucsize, "9", "u"),
                  disp_m, dt_sepchar, disp_d, dt_sepchar, disp_y, hh, mm,
                  (G.pInfo->lcflag ? '^' : ' ')));

            fnprint(__G);

            if ((error = do_string(__G__ G.crec.file_comment_length,
                                   QCOND ? DISPL_8 : SKIP)) != 0)
            {
                error_in_archive = error;
                if (error > PK_WARN)
                    return error;
            }

            tot_ucsize += G.crec.ucsize;
            tot_csize  += csiz;
            ++members;

        } else {
            SKIP_(G.crec.file_comment_length)
        }
    } /* for each central directory entry */

    if (uO.qflag < 2) {
        if ((cfactor = ratio(tot_ucsize, tot_csize)) < 0) {
            sgn = '-';
            cfactor = (-cfactor + 5) / 10;
        } else {
            sgn = ' ';
            cfactor = (cfactor + 5) / 10;
        }

        if (cfactor == 100)
            snprintf(cfactorstr, sizeof(cfactorstr),
                     LoadFarString(CompFactor100));
        else
            snprintf(cfactorstr, sizeof(cfactorstr),
                     LoadFarString(CompFactorStr), sgn, cfactor);

        if (longhdr) {
            Info(slide, 0, ((char *)slide, LoadFarString(LongFileTrailer),
              FmZofft(tot_ucsize, "8", "u"), FmZofft(tot_csize, "8", "u"),
              cfactorstr, members, members == 1 ? "" : "s"));
        } else
            Info(slide, 0, ((char *)slide, LoadFarString(ShortFileTrailer),
              FmZofft(tot_ucsize, "9", "u"),
              members, members == 1 ? "" : "s"));
    }

    if (error_in_archive <= PK_WARN) {
        if ( (memcmp(G.sig,
                     (G.ecrec.have_ecr64 ?
                      end_central64_sig : end_central_sig), 4) != 0)
            && (!G.ecrec.is_zip64_archive)
            && (memcmp(G.sig, end_central_sig, 4) != 0)
           ) {
            Info(slide, 0x401, ((char *)slide, "%s", LoadFarString(EndSigMsg)));
            error_in_archive = PK_WARN;
        }

        if (members == 0L && error_in_archive <= PK_WARN)
            error_in_archive = PK_FIND;
    }

    return error_in_archive;

} /* end function list_files() */

#ifdef TIMESTAMP

/************************/
/* Function fn_is_dir() */
/************************/

static int fn_is_dir(__G)    /* returns TRUE if G.filename is directory */
    __GDEF
{
    extent fn_len = strlen(G.filename);
    char endc;

    return fn_len > 0 && ((endc = lastchar(G.filename, fn_len)) == '/');
}

/*****************************/
/* Function get_time_stamp() */
/*****************************/

int get_time_stamp(__G__ last_modtime, nmember)  /* return PK-type error code */
    __GDEF
    time_t *last_modtime;
    ulg *nmember;
{
    int do_this_file = FALSE, error, error_in_archive = PK_COOL;
    ulg j;
#ifdef USE_EF_UT_TIME
    iztimes z_utime;
#endif
    min_info info;

    *last_modtime = 0L;
    *nmember = 0L;
    G.pInfo = &info;

    for (j = 1L;; j++) {

        if (readbuf(__G__ G.sig, 4) == 0)
            return PK_EOF;

        if (memcmp(G.sig, central_hdr_sig, 4)) {
            if (((unsigned)(j - 1) & (unsigned)0xFFFF) ==
                (unsigned)G.ecrec.total_entries_central_dir) {
                break;
            } else {
                Info(slide, 0x401,
                     ((char *)slide, LoadFarString(CentSigMsg), j));
                Info(slide, 0x401,
                     ((char *)slide, "%s", LoadFarString(ReportMsg)));
                return PK_BADERR;
            }
        }

        if ((error = process_cdir_file_hdr(__G)) != PK_COOL)
            return error;

        if ((error = do_string(__G__ G.crec.filename_length, DS_FN)) != PK_OK)
        {
            error_in_archive = error;
            if (error > PK_WARN)
                return error;
        }

        if (G.extra_field != (uch *)NULL) {
            free(G.extra_field);
            G.extra_field = (uch *)NULL;
        }
        if ((error = do_string(__G__ G.crec.extra_field_length, EXTRA_FIELD)) != 0)
        {
            error_in_archive = error;
            if (error > PK_WARN)
                return error;
        }

        if (!G.process_all_files) {
            unsigned i;

            if (G.filespecs == 0)
                do_this_file = TRUE;
            else {
                do_this_file = FALSE;
                for (i = 0; i < G.filespecs; i++)
                    if (match(G.filename, G.pfnames[i], uO.C_flag WISEP)) {
                        do_this_file = TRUE;
                        break;
                    }
            }
            if (do_this_file) {
                for (i = 0; i < G.xfilespecs; i++)
                    if (match(G.filename, G.pxnames[i], uO.C_flag WISEP)) {
                        do_this_file = FALSE;
                        break;
                    }
            }
        }

        if ((G.process_all_files || do_this_file) && !fn_is_dir(__G)) {
#ifdef USE_EF_UT_TIME
            if (G.extra_field &&
#ifdef IZ_CHECK_TZ
                G.tz_is_valid &&
#endif
                (ef_scan_for_izux(G.extra_field, G.crec.extra_field_length, 1,
                                  G.crec.last_mod_dos_datetime, &z_utime, NULL)
                 & EB_UT_FL_MTIME))
            {
                if (*last_modtime < z_utime.mtime)
                    *last_modtime = z_utime.mtime;
            } else
#endif /* USE_EF_UT_TIME */
            {
                time_t modtime = dos_to_unix_time(G.crec.last_mod_dos_datetime);

                if (*last_modtime < modtime)
                    *last_modtime = modtime;
            }
            ++*nmember;
        }

        SKIP_(G.crec.file_comment_length)

    } /* for each central directory entry */

    if (memcmp(G.sig, end_central_sig, 4)) {
        Info(slide, 0x401, ((char *)slide, "%s", LoadFarString(EndSigMsg)));
        error_in_archive = PK_WARN;
    }
    if (*nmember == 0L && error_in_archive <= PK_WARN)
        error_in_archive = PK_FIND;

    return error_in_archive;

} /* end function get_time_stamp() */

#endif /* TIMESTAMP */

/********************/
/* Function ratio() */    /* also used by ZipInfo routines */
/********************/

int ratio(zusz_t uc, zusz_t c)
{
    zusz_t denom;

    if (uc == 0)
        return 0;
    if (uc > 2000000L) {
        denom = uc / 1000L;
        return ((uc >= c) ?
            (int)((uc - c + (denom >> 1)) / denom) :
           -(int)((c - uc + (denom >> 1)) / denom));
    } else {
        denom = uc;
        return ((uc >= c) ?
            (int)((1000L * (uc - c) + (denom >> 1)) / denom) :
           -(int)((1000L * (c - uc) + (denom >> 1)) / denom));
    }
}

/************************/
/*  Function fnprint()  */    /* also used by ZipInfo routines */
/************************/

void fnprint(__G)    /* print filename (after filtering) and newline */
    __GDEF
{
    char *name = fnfilter(G.filename, slide, (extent)(WSIZE >> 1));

    (*G.message)((zvoid *)&G, (uch *)name, (ulg)strlen(name), 0);
    (*G.message)((zvoid *)&G, (uch *)"\n", 1L, 0);
}

