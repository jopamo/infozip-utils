/*
  unix/unix.c - Zip 3
*/
#include "zip.h"

#ifndef UTIL    /* the companion #endif is a bit of ways down ... */

#include <time.h>
#include <limits.h>


#if (!defined(S_IWRITE) && defined(S_IWUSR))
#  define S_IWRITE S_IWUSR
#endif

#  include <dirent.h>

#define PAD 0
#define PATH_END '/'

/* Library functions not in (most) header files */

#  include <utime.h>

extern char *label;
local ulg label_time = 0;
local ulg label_mode = 0;
local time_t label_utim = 0;

/* Local functions */
local char *readd OF((DIR *));




local char *readd(d)
DIR *d;                 /* directory stream to read from */
/* Return a pointer to the next name in the directory stream d, or NULL if
   no more entries or an error occurs. */
{
  struct dirent *e;

  e = readdir(d);
  return e == NULL ? (char *) NULL : e->d_name;
}

int procname(n, caseflag)
char *n;                /* name to process */
int caseflag;           /* true to force case-sensitive match */
/* Process a name or sh expression to operate on (or exclude).  Return
   an error code in the ZE_ class. */
{
  char *a;              /* path and name for recursion */
  DIR *d;               /* directory stream from opendir() */
  char *e;              /* pointer to name from readd() */
  int m;                /* matched flag */
  char *p;              /* path for recursion */
  z_stat s;             /* result of stat() */
  struct zlist far *z;  /* steps through zfiles list */

  if (strcmp(n, "-") == 0)   /* if compressing stdin */
    return newname(n, 0, caseflag);
  else if (LSSTAT(n, &s))
  {
    /* Not a file or directory--search for shell expression in zip file */
    p = ex2in(n, 0, (int *)NULL);       /* shouldn't affect matching chars */
    m = 1;
    for (z = zfiles; z != NULL; z = z->nxt) {
      if (MATCH(p, z->iname, caseflag))
      {
        z->mark = pcount ? filter(z->zname, caseflag) : 1;
        if (verbose)
            fprintf(mesg, "zip diagnostic: %scluding %s\n",
               z->mark ? "in" : "ex", z->name);
        m = 0;
      }
    }
    free((zvoid *)p);
    return m ? ZE_MISS : ZE_OK;
  }

  /* Live name--use if file, recurse if directory */
  if (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode))
  {
    /* add or remove name of file */
    if ((m = newname(n, 0, caseflag)) != ZE_OK)
      return m;
  }
  else if (S_ISDIR(s.st_mode))
  {
    /* Add trailing / to the directory name */
    if ((p = malloc(strlen(n)+2)) == NULL)
      return ZE_MEM;
    if (strcmp(n, ".") == 0) {
      *p = '\0';  /* avoid "./" prefix and do not create zip entry */
    } else {
      strcpy(p, n);
      a = p + strlen(p);
      if (a[-1] != '/')
        strcpy(a, "/");
      if (dirnames && (m = newname(p, 1, caseflag)) != ZE_OK) {
        free((zvoid *)p);
        return m;
      }
    }
    /* recurse into directory */
    if (recurse && (d = opendir(n)) != NULL)
    {
      while ((e = readd(d)) != NULL) {
        if (strcmp(e, ".") && strcmp(e, ".."))
        {
          if ((a = malloc(strlen(p) + strlen(e) + 1)) == NULL)
          {
            closedir(d);
            free((zvoid *)p);
            return ZE_MEM;
          }
          strcat(strcpy(a, p), e);
          if ((m = procname(a, caseflag)) != ZE_OK)   /* recurse on name */
          {
            if (m == ZE_MISS)
              zipwarn("name not matched: ", a);
            else
              ziperr(m, a);
          }
          free((zvoid *)a);
        }
      }
      closedir(d);
    }
    free((zvoid *)p);
  } /* (s.st_mode & S_IFDIR) */
  else if ((s.st_mode & S_IFIFO) == S_IFIFO)
  {
    if (allow_fifo) {
      /* FIFO (Named Pipe) - handle as normal file */
      /* add or remove name of FIFO */
      /* zip will stop if FIFO is open and wait for pipe to be fed and closed */
      if (noisy) zipwarn("Reading FIFO (Named Pipe): ", n);
      if ((m = newname(n, 0, caseflag)) != ZE_OK)
        return m;
    } else {
      zipwarn("ignoring FIFO (Named Pipe) - use -FI to read: ", n);
      return ZE_OK;
    }
  } /* S_IFIFO */
  else
    zipwarn("ignoring special file: ", n);
  return ZE_OK;
}

char *ex2in(x, isdir, pdosflag)
char *x;                /* external file name */
int isdir;              /* input: x is a directory */
int *pdosflag;          /* output: force MSDOS file attributes? */
/* Convert the external file name to a zip file name, returning the malloc'ed
   string or NULL if not enough memory. */
{
  char *n;              /* internal file name (malloc'ed) */
  char *t = NULL;       /* shortened name */
  int dosflag;

  dosflag = dosify;     /* default for non-DOS and non-OS/2 */

  /* Find starting point in name before doing malloc */
  /* Strip "//host/share/" part of a UNC name */
  if (!strncmp(x,"//",2) && (x[2] != '\0' && x[2] != '/')) {
    n = x + 2;
    while (*n != '\0' && *n != '/')
      n++;              /* strip host name */
    if (*n != '\0') {
      n++;
      while (*n != '\0' && *n != '/')
        n++;            /* strip `share' name */
    }
    if (*n != '\0')
      t = n + 1;
  } else
      t = x;
  while (*t == '/')
    t++;                /* strip leading '/' chars to get a relative path */
  while (*t == '.' && t[1] == '/')
    t += 2;             /* strip redundant leading "./" sections */

  /* Make changes, if any, to the copied name (leave original intact) */
  if (!pathput)
    t = last(t, PATH_END);

  /* Malloc space for internal name and copy it */
  if ((n = malloc(strlen(t) + 1)) == NULL)
    return NULL;
  strcpy(n, t);

  if (dosify)
    msname(n);


  /* Returned malloc'ed name */
  if (pdosflag)
    *pdosflag = dosflag;

  if (isdir) return n;  /* avoid warning on unused variable */
  return n;
}

char *in2ex(n)
char *n;                /* internal file name */
/* Convert the zip file name to an external file name, returning the malloc'ed
   string or NULL if not enough memory. */
{
  char *x;              /* external file name */

  if ((x = malloc(strlen(n) + 1 + PAD)) == NULL)
    return NULL;
  strcpy(x, n);
  return x;
}

/*
 * XXX use ztimbuf in both POSIX and non POSIX cases ?
 */
void stamp(f, d)
char *f;                /* name of file to change */
ulg d;                  /* dos-style time to change it to */
/* Set last updated and accessed time of file f to the DOS time d. */
{
  struct utimbuf u;     /* argument for utime()  const ?? */

  /* Convert DOS time to time_t format in u */
  u.actime = u.modtime = dos2unixtime(d);
  utime(f, &u);

}

ulg filetime(f, a, n, t)
  char *f;                /* name of file to get info on */
  ulg *a;                 /* return value: file attributes */
  zoff_t *n;              /* return value: file size */
  iztimes *t;             /* return value: access, modific. and creation times */
/* If file *f does not exist, return 0.  Else, return the file's last
   modified date and time as an MSDOS date and time.  The date and
   time is returned in a long with the date most significant to allow
   unsigned integer comparison of absolute times.  Also, if a is not
   a NULL pointer, store the file attributes there, with the high two
   bytes being the Unix attributes, and the low byte being a mapping
   of that to DOS attributes.  If n is not NULL, store the file size
   there.  If t is not NULL, the file's access, modification and creation
   times are stored there as UNIX time_t values.
   If f is "-", use standard input as the file. If f is a device, return
   a file size of -1 */
{
  z_stat s;         /* results of stat() */
  /* converted to pointer from using FNMAX - 11/8/04 EG */
  char *name;
  int len = strlen(f);

  if (f == label) {
    if (a != NULL)
      *a = label_mode;
    if (n != NULL)
      *n = -2L; /* convention for a label name */
    if (t != NULL)
      t->atime = t->mtime = t->ctime = label_utim;
    return label_time;
  }
  if ((name = malloc(len + 1)) == NULL) {
    ZIPERR(ZE_MEM, "filetime");
  }
  strcpy(name, f);
  if (name[len - 1] == '/')
    name[len - 1] = '\0';
  /* not all systems allow stat'ing a file with / appended */
  if (strcmp(f, "-") == 0) {
    if (zfstat(fileno(stdin), &s) != 0) {
      free(name);
      error("fstat(stdin)");
    }
  }
  else if (LSSTAT(name, &s) != 0) {
    /* Accept about any file kind including directories
     * (stored with trailing / with -r option)
     */
    free(name);
    return 0;
  }
  free(name);

  if (a != NULL) {
    *a = ((ulg)s.st_mode << 16) | !(s.st_mode & S_IWRITE);
    if ((s.st_mode & S_IFMT) == S_IFDIR) {
      *a |= MSDOS_DIR_ATTR;
    }
  }
  if (n != NULL)
    *n = (s.st_mode & S_IFMT) == S_IFREG ? s.st_size : -1L;
  if (t != NULL) {
    t->atime = s.st_atime;
    t->mtime = s.st_mtime;
    t->ctime = t->mtime;   /* best guess, (s.st_ctime: last status change!!) */
  }
  return unix2dostime(&s.st_mtime);
}



int set_new_unix_extra_field(struct zlist far *z, z_stat *s)
  /* New unix extra field. Currently only UIDs and GIDs are stored. */
{
  size_t uid_size;
  size_t gid_size;
  size_t ef_data_size;
  size_t need_local;
  size_t need_central;
  char *extra = NULL;
  char *cextra = NULL;
  unsigned long id;
  size_t b;
  size_t i;

  uid_size = sizeof(s->st_uid);
  gid_size = sizeof(s->st_gid);

  /* New extra field layout:
     tag       (2 bytes) = 'u''x'
     size      (2 bytes) = len(data), little-endian
     version   (1 byte)  = 1
     uid_size  (1 byte)
     uid       (uid_size bytes, little-endian)
     gid_size  (1 byte)
     gid       (gid_size bytes, little-endian)
   */
  ef_data_size = 1 + 1 + uid_size + 1 + gid_size;

  /* Guard and size computations (treat sentinel values as 0 when no data yet). */
  if (z->ext == (ush)USHRT_MAX && z->extra == NULL)
    z->ext = 0;
  if (z->cext == (ush)USHRT_MAX && z->cextra == NULL)
    z->cext = 0;

  need_local   = (size_t)z->ext  + 4u + ef_data_size;
  need_central = (size_t)z->cext + 4u + ef_data_size;

  /* Allocate fresh buffers, preserving existing contents. */
  extra = (char *)calloc(1, need_local);
  if (extra == NULL)
    return ZE_MEM;
  cextra = (char *)calloc(1, need_central);
  if (cextra == NULL) {
    free(extra);
    return ZE_MEM;
  }

  if (z->ext  && z->extra)  memcpy(extra,  z->extra,  (size_t)z->ext);
  if (z->cext && z->cextra) memcpy(cextra, z->cextra, (size_t)z->cext);

  free(z->extra);
  z->extra = extra;
  free(z->cextra);
  z->cextra = cextra;

  /* Local header extra field: 'ux' + size (LE) + version. */
  z->extra[z->ext + 0] = 'u';
  z->extra[z->ext + 1] = 'x';
  z->extra[z->ext + 2] = (char)(ef_data_size & 0xFF);
  z->extra[z->ext + 3] = (char)((ef_data_size >> 8) & 0xFF);
  z->extra[z->ext + 4] = 1;

  /* UID */
  z->extra[z->ext + 5] = (char)uid_size;
  b = (size_t)z->ext + 6;
  id = (unsigned long)(s->st_uid);
  for (i = 0; i < uid_size; ++i) {
    z->extra[b++] = (char)(id & 0xFF);
    id >>= 8;
  }

  /* GID */
  z->extra[b++] = (char)gid_size;
  id = (unsigned long)(s->st_gid);
  for (i = 0; i < gid_size; ++i) {
    z->extra[b++] = (char)(id & 0xFF);
    id >>= 8;
  }

  /* Mirror local extra field into central directory extra field. */
  memcpy(z->cextra + z->cext, z->extra + z->ext, (size_t)(4 + ef_data_size));

  z->ext  += (unsigned)(4 + ef_data_size);
  z->cext += (unsigned)(4 + ef_data_size);

  return ZE_OK;
}

int set_extra_field(z, z_utim)
  struct zlist far *z;
  iztimes *z_utim;
  /* store full data in local header but just modification time stamp info
     in central header */
{
  (void)z_utim;
  z_stat s;
  char *name;
  int len = strlen(z->name);

  /* For the full sized UT local field including the UID/GID fields, we
   * have to stat the file again. */

  if ((name = malloc(len + 1)) == NULL) {
    ZIPERR(ZE_MEM, "set_extra_field");
  }
  strcpy(name, z->name);
  if (name[len - 1] == '/')
    name[len - 1] = '\0';
  /* not all systems allow stat'ing a file with / appended */
  if (LSSTAT(name, &s)) {
    free(name);
    return ZE_OPEN;
  }
  free(name);

#define EB_L_UT_SIZE    (EB_HEADSIZE + EB_UT_LEN(2))
#define EB_C_UT_SIZE    (EB_HEADSIZE + EB_UT_LEN(1))

/* The flag UIDGID_NOT_16BIT should be set by the pre-compile configuration
   script when it detects st_uid or st_gid sizes differing from 16-bit.
 */
  /* The following "second-level" check for st_uid and st_gid members being
     16-bit wide is only added as a safety precaution in case the "first-level"
     check failed to define the UIDGID_NOT_16BIT symbol.
     The first-level check should have been implemented in the automatic
     compile configuration process.
   */
# ifdef UIDGID_ARE_16B
#  undef UIDGID_ARE_16B
# endif
  /* The following expression is a compile-time constant and should (hopefully)
     get optimized away by any sufficiently intelligent compiler!
   */
# define UIDGID_ARE_16B  (sizeof(s.st_uid) == 2 && sizeof(s.st_gid) == 2)

# define EB_L_UX2_SIZE   (EB_HEADSIZE + EB_UX2_MINLEN)
# define EB_C_UX2_SIZE   EB_HEADSIZE
# define EF_L_UNIX_SIZE  (EB_L_UT_SIZE + (UIDGID_ARE_16B ? EB_L_UX2_SIZE : 0))
# define EF_C_UNIX_SIZE  (EB_C_UT_SIZE + (UIDGID_ARE_16B ? EB_C_UX2_SIZE : 0))

  if ((z->extra = (char *)malloc(EF_L_UNIX_SIZE)) == NULL)
    return ZE_MEM;
  if ((z->cextra = (char *)malloc(EF_C_UNIX_SIZE)) == NULL)
    return ZE_MEM;

  z->extra[0]  = 'U';
  z->extra[1]  = 'T';
  z->extra[2]  = (char)EB_UT_LEN(2);    /* length of data part of local e.f. */
  z->extra[3]  = 0;
  z->extra[4]  = EB_UT_FL_MTIME | EB_UT_FL_ATIME;    /* st_ctime != creation */
  z->extra[5]  = (char)(s.st_mtime);
  z->extra[6]  = (char)(s.st_mtime >> 8);
  z->extra[7]  = (char)(s.st_mtime >> 16);
  z->extra[8]  = (char)(s.st_mtime >> 24);
  z->extra[9]  = (char)(s.st_atime);
  z->extra[10] = (char)(s.st_atime >> 8);
  z->extra[11] = (char)(s.st_atime >> 16);
  z->extra[12] = (char)(s.st_atime >> 24);

  /* Only store the UID and GID in the old Ux extra field if the runtime
     system provides them in 16-bit wide variables.  */
  if (UIDGID_ARE_16B) {
    z->extra[13] = 'U';
    z->extra[14] = 'x';
    z->extra[15] = (char)EB_UX2_MINLEN; /* length of data part of local e.f. */
    z->extra[16] = 0;
    z->extra[17] = (char)(s.st_uid);
    z->extra[18] = (char)(s.st_uid >> 8);
    z->extra[19] = (char)(s.st_gid);
    z->extra[20] = (char)(s.st_gid >> 8);
  }

  z->ext = EF_L_UNIX_SIZE;

  memcpy(z->cextra, z->extra, EB_C_UT_SIZE);
  z->cextra[EB_LEN] = (char)EB_UT_LEN(1);
  if (UIDGID_ARE_16B) {
    /* Copy header of Ux extra field from local to central */
    memcpy(z->cextra+EB_C_UT_SIZE, z->extra+EB_L_UT_SIZE, EB_C_UX2_SIZE);
    z->cextra[EB_LEN+EB_C_UT_SIZE] = 0;
  }
  z->cext = EF_C_UNIX_SIZE;

  /* new unix extra field */
  set_new_unix_extra_field(z, &s);

  return ZE_OK;
}



int deletedir(char *d)
/* Delete the directory *d if it is empty, do nothing otherwise.
   Return the result of rmdir(). */
{
  return rmdir(d);
}

#endif /* !UTIL */


/******************************/
/*  Function version_local()  */
/******************************/

void version_local(void)
{
  printf("Compiled with gcc %s for Linux.\n\n", __VERSION__);
}
