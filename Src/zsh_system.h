/*
 * system.h - system configuration header file
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#if 0
/*
 * Setting _XPG_IV here is actually wrong and is not needed
 * with currently supported versions (5.43C20 and above)
 */
#ifdef sinix
# define _XPG_IV 1
#endif
#endif

#if defined(__linux) || defined(__GNU__) || defined(__GLIBC__) || defined(LIBC_MUSL) || defined(__CYGWIN__)
/*
 * Turn on numerous extensions.
 * This is in order to get the functions for manipulating /dev/ptmx.
 */
#define _GNU_SOURCE 1
#endif
#ifdef LIBC_MUSL
#define _POSIX_C_SOURCE 200809L
#endif

/* NeXT has half-implemented POSIX support *
 * which currently fools configure         */
#ifdef __NeXT__
# undef HAVE_TERMIOS_H
# undef HAVE_SYS_UTSNAME_H
#endif

#ifndef ZSH_NO_XOPEN
# ifdef ZSH_CURSES_SOURCE
#  define _XOPEN_SOURCE_EXTENDED 1
# else
#  ifdef MULTIBYTE_SUPPORT
/*
 * Needed for wcwidth() which is part of XSI.
 * Various other uses of the interface mean we can't get away with just
 * _XOPEN_SOURCE.
 */
#   define _XOPEN_SOURCE_EXTENDED 1
#  endif /* MULTIBYTE_SUPPORT */
# endif /* ZSH_CURSES_SOURCE */
#endif /* ZSH_NO_XOPEN */

/*
 * Solaris by default zeroes all elements of the tm structure in
 * strptime().  Unfortunately that gives us no way of telling whether
 * the tm_isdst element has been set from the input pattern.  If it
 * hasn't we want it to be -1 (undetermined) on input to mktime().  So
 * we stop strptime() zeroing the struct tm and instead set all the
 * elements ourselves.
 *
 * This is likely to be harmless everywhere else.
 */
#define _STRPTIME_DONTZERO

#ifndef HAVE_ALLOCA
# define alloca zhalloc
#else
# ifdef __GNUC__
#  define alloca __builtin_alloca
# else
#  if HAVE_ALLOCA_H
#   include <alloca.h>
#  else
#   ifdef _AIX
 #   pragma alloca
#   else
#    ifndef alloca
char *alloca (size_t);
#    endif
#   endif
#  endif
# endif
#endif

/*
 * libc.h in an optional package for Debian Linux is broken (it
 * defines dup() as a synonym for dup2(), which has a different
 * number of arguments), so just include it for next.
 */
#ifdef __NeXT__
# ifdef HAVE_LIBC_H
#  include <libc.h>
# endif
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

# include <unistd.h>

#ifdef HAVE_STDDEF_H
/*
 * Seen on Solaris 8 with gcc: stddef defines offsetof, which clashes
 * with system.h's definition of the symbol unless we include this
 * first.  Otherwise, this will be hooked in by wchar.h, too late
 * for comfort.
 */
#include <stddef.h>
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <signal.h>
#include <setjmp.h>

#ifdef HAVE_PWD_H
# include <pwd.h>
#endif

#ifdef HAVE_GRP_H
# include <grp.h>
#endif
# include <dirent.h>

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif

#include <sys/time.h>
#include <time.h>

/* This is needed by some old SCO unices */
#if !defined(HAVE_STRUCT_TIMEZONE) && !defined(ZSH_OOT_MODULE)
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};
#endif

/* Used to provide compatibility with clock_gettime() */
#if !defined(HAVE_STRUCT_TIMESPEC) && !defined(ZSH_OOT_MODULE)
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

/* There's more than one non-standard way to get at this data */
#if !defined(HAVE_STRUCT_DIRENT_D_INO) && defined(HAVE_STRUCT_DIRENT_D_STAT)
# define d_ino d_stat.st_ino
# define HAVE_STRUCT_DIRENT_D_INO HAVE_STRUCT_DIRENT_D_STAT
#endif /* !HAVE_STRUCT_DIRENT_D_INO && HAVE_STRUCT_DIRENT_D_STAT */

/* Sco needs the following include for struct utimbuf *
 * which is strange considering we do not use that    *
 * anywhere in the code                               */
#ifdef __sco
# include <utime.h>
#endif

#ifdef HAVE_SYS_TIMES_H
# include <sys/times.h>
#endif

# include <string.h>

#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#ifdef USE_STACK_ALLOCATION
#ifdef HAVE_VARIABLE_LENGTH_ARRAYS
# define VARARR(X,Y,Z)	X (Y)[Z]
#else
# define VARARR(X,Y,Z)	X *(Y) = (X *) alloca(sizeof(X) * (Z))
#endif
#else
# define VARARR(X,Y,Z)	X *(Y) = (X *) zhalloc(sizeof(X) * (Z))
#endif

/* we should handle unlimited sizes from pathconf(_PC_PATH_MAX) */
/* but this is too much trouble                                 */
#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX MAXPATHLEN
# else
#  ifdef _POSIX_PATH_MAX
#   define PATH_MAX _POSIX_PATH_MAX
#  else
    /* so we will just pick something */
#   define PATH_MAX 1024
#  endif
# endif
#endif

/*
 * The number of file descriptors we'll allocate initially.
 * We will reallocate later if necessary.
 */
#define ZSH_INITIAL_OPEN_MAX 64
#ifndef OPEN_MAX
# ifdef NOFILE
#  define OPEN_MAX NOFILE
# else
   /* so we will just pick something */
#  define OPEN_MAX ZSH_INITIAL_OPEN_MAX
# endif
#endif
#ifndef HAVE_SYSCONF
# define zopenmax() ((long) (OPEN_MAX > ZSH_INITIAL_OPEN_MAX ? \
			     ZSH_INITIAL_OPEN_MAX : OPEN_MAX))
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

# include <sys/wait.h>

#ifdef HAVE_SYS_SELECT_H
# ifndef TIME_H_SELECT_H_CONFLICTS
#  include <sys/select.h>
# endif
#endif

#if defined(__APPLE__) && defined(HAVE_SELECT)
/*
 * Prefer select() to poll() on MacOS X since poll() is known
 * to be problematic in 10.4
 */
#undef HAVE_POLL
#undef HAVE_POLL_H
#endif

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#ifdef HAVE_TERMIOS_H
# ifdef __sco
   /* termios.h includes sys/termio.h instead of sys/termios.h; *
    * hence the declaration for struct termios is missing       */
#  include <sys/termios.h>
# else
#  include <termios.h>
# endif
# ifdef _POSIX_VDISABLE
#  define VDISABLEVAL _POSIX_VDISABLE
# else
#  define VDISABLEVAL 0
# endif
# define HAS_TIO 1
#else    /* not TERMIOS */
# ifdef HAVE_TERMIO_H
#  include <termio.h>
#  define VDISABLEVAL -1
#  define HAS_TIO 1
# else   /* not TERMIOS and TERMIO */
#  include <sgtty.h>
# endif  /* HAVE_TERMIO_H  */
#endif   /* HAVE_TERMIOS_H */

#if defined(GWINSZ_IN_SYS_IOCTL) || defined(IOCTL_IN_SYS_IOCTL)
# include <sys/ioctl.h>
#endif
#ifdef WINSIZE_IN_PTEM
# include <sys/stream.h>
# include <sys/ptem.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef HAVE_SYS_UTSNAME_H
# include <sys/utsname.h>
#endif

#define DEFAULT_WORDCHARS "*?_-.[]~=/&;!#$%^(){}<>"
#define DEFAULT_TIMEFMT   "%J  %U user %S system %P cpu %*E total"

/* Posix getpgrp takes no argument, while the BSD version *
 * takes the process ID as an argument                    */
#ifdef GETPGRP_VOID
# define GETPGRP() getpgrp()
#else
# define GETPGRP() getpgrp(0)
#endif

#ifndef HAVE_GETLOGIN
# define getlogin() cuserid(NULL)
#endif

/* compatibility wrappers */

/* Our strategy is as follows:
 *
 * - Ensure that either setre[ug]id() or set{e,}[ug]id() is available.
 * - If setres[ug]id() are missing, provide them in terms of either
 *   setre[ug]id() or set{e,}[ug]id(), whichever is available.
 * - Provide replacement setre[ug]id() or set{e,}[ug]id() if they are not
 *   available natively.
 *
 * There isn't a circular dependency because, right off the bat, we check that
 * there's an end condition, and #error out otherwise.
 */
#if !defined(HAVE_SETREUID) && !(defined(HAVE_SETEUID) && defined(HAVE_SETUID))
  /*
   * If you run into this error, you have two options:
   * - Teach zsh how to do the equivalent of setreuid() on your system
   * - Remove support for PRIVILEGED option, and then remove the #error.
   */
# error "Don't know how to change UID"
#endif
#if !defined(HAVE_SETREGID) && !(defined(HAVE_SETEGID) && defined(HAVE_SETGID))
  /* See above comment. */
# error "Don't know how to change GID"
#endif

/* Provide setresuid(). */
#ifndef HAVE_SETRESUID
int	setresuid(uid_t, uid_t, uid_t);
# define HAVE_SETRESUID
# define ZSH_IMPLEMENT_SETRESUID
# ifdef HAVE_SETREUID
#  define ZSH_HAVE_NATIVE_SETREUID
# endif
#endif

/* Provide setresgid(). */
#ifndef HAVE_SETRESGID
int	setresgid(gid_t, gid_t, gid_t);
# define HAVE_SETRESGID
# define ZSH_IMPLEMENT_SETRESGID
# ifdef HAVE_SETREGID
#  define ZSH_HAVE_NATIVE_SETREGID
# endif
#endif

/* Provide setreuid(). */
#ifndef HAVE_SETREUID
# define setreuid(X, Y) setresuid((X), (Y), -1)
# define HAVE_SETREUID
#endif

/* Provide setregid(). */
#ifndef HAVE_SETREGID
# define setregid(X, Y) setresgid((X), (Y), -1)
# define HAVE_SETREGID
#endif

/* Provide setuid(). */
/* ### TODO: Either remove this (this function has been standard since 1985),
 * ###       or rewrite this without multiply-evaluating the argument */
#ifndef HAVE_SETUID
# define setuid(X) setreuid((X), (X))
# define HAVE_SETUID
#endif

/* Provide setgid(). */
#ifndef HAVE_SETGID
/* ### TODO: Either remove this (this function has been standard since 1985),
 * ###       or rewrite this without multiply-evaluating the argument */
#  define setgid(X) setregid((X), (X))
#  define HAVE_SETGID
#endif

/* Provide seteuid(). */
#ifndef HAVE_SETEUID
# define seteuid(X) setreuid(-1, (X))
# define HAVE_SETEUID
#endif

/* Provide setegid(). */
#ifndef HAVE_SETEGID
# define setegid(X) setregid(-1, (X))
# define HAVE_SETEGID
#endif

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
# if defined(__hpux) && !defined(RLIMIT_CPU)
/* HPUX does have the BSD rlimits in the kernel.  Officially they are *
 * unsupported but quite a few of them like RLIMIT_CORE seem to work. *
 * All the following are in the <sys/resource.h> but made visible     *
 * only for the kernel.                                               */
#  define	RLIMIT_CPU	0
#  define	RLIMIT_FSIZE	1
#  define	RLIMIT_DATA	2
#  define	RLIMIT_STACK	3
#  define	RLIMIT_CORE	4
#  define	RLIMIT_RSS	5
#  define	RLIMIT_NOFILE   6
#  define	RLIMIT_OPEN_MAX	RLIMIT_NOFILE
#  define	RLIM_NLIMITS	7
#  define	RLIM_INFINITY	0x7fffffff
# endif
#endif

/* we use the SVR4 constant instead of the BSD one */
#if !defined(RLIMIT_NOFILE) && defined(RLIMIT_OFILE)
# define RLIMIT_NOFILE RLIMIT_OFILE
#endif
#if !defined(RLIMIT_VMEM) && defined(RLIMIT_AS)
# define RLIMIT_VMEM RLIMIT_AS
#endif

#if defined(HAVE_SYS_CAPABILITY_H) && defined(HAVE_CAP_GET_PROC)
# include <sys/capability.h>
#endif

/* DIGBUFSIZ is the length of a buffer which can hold the -LONG_MAX-1 *
 * (or with ZSH_64_BIT_TYPE maybe -LONG_LONG_MAX-1)                   *
 * converted to printable decimal form including the sign and the     *
 * terminating null character. Below 0.30103 > lg 2.                  *
 * BDIGBUFSIZE is for a number converted to printable binary form.    */
#define DIGBUFSIZE ((int)(((sizeof(zlong) * 8) - 1) * 30103/100000) + 3)
#define BDIGBUFSIZE ((int)((sizeof(zlong) * 8) + 4))

/* file mode permission bits */

#ifndef S_IXUGO
# define S_IXUGO (S_IXUSR|S_IXGRP|S_IXOTH)
#endif


#ifndef HAVE_READLINK
# define readlink(PATH, BUF, BUFSZ) \
    ((void)(PATH), (void)(BUF), (void)(BUFSZ), errno = ENOSYS, -1)
#endif

#ifndef F_OK          /* missing macros for access() */
# define F_OK 0
# define X_OK 1
# define W_OK 2
# define R_OK 4
#endif

#ifndef HAVE_LCHOWN
# define lchown chown
#endif

#ifndef HAVE_MEMCPY
# define memcpy memmove
#endif

#ifndef HAVE_MEMMOVE
# ifndef memmove
static char *zmmv;
# define memmove(dest, src, len) (bcopy((src), zmmv = (dest), (len)), zmmv)
# endif
#endif

#ifndef offsetof
# define offsetof(TYPE, MEM) ((char *)&((TYPE *)0)->MEM - (char *)(TYPE *)0)
#endif

extern char **environ;

/*
 * We always need setenv and unsetenv in pairs, because
 * we don't know how to do memory management on the values set.
 */
#if defined(HAVE_SETENV) && defined(HAVE_UNSETENV) \
    && !defined(SETENV_MANGLES_EQUAL)
# define USE_SET_UNSET_ENV
#endif


/* These variables are sometimes defined in, *
 * and needed by, the termcap library.       */
#if MUST_DEFINE_OSPEED
extern char PC, *BC, *UP;
extern short ospeed;
#endif

#ifndef O_NOCTTY
# define O_NOCTTY 0
#endif

#ifdef _LARGEFILE_SOURCE
#ifdef HAVE_FSEEKO
#define fseek fseeko
#endif
#ifdef HAVE_FTELLO
#define ftell ftello
#endif
#endif

/* Can we do locale stuff? */
#undef USE_LOCALE
#if defined(CONFIG_LOCALE) && defined(HAVE_SETLOCALE) && defined(LC_ALL)
# define USE_LOCALE 1
#endif /* CONFIG_LOCALE && HAVE_SETLOCALE && LC_ALL */

#ifndef MAILDIR_SUPPORT
#define mailstat(X,Y) stat(X,Y)
#endif

#ifdef __CYGWIN__
# include <sys/cygwin.h>
# define IS_DIRSEP(c) ((c) == '/' || (c) == '\\')
#else
# define IS_DIRSEP(c) ((c) == '/')
#endif

#if defined(__GNUC__) && (!defined(__APPLE__) || defined(__clang__))
/* Does the OS X port of gcc still gag on __attribute__? */
#define UNUSED(x) x __attribute__((__unused__))
#else
#define UNUSED(x) x
#endif

/*
 * The MULTIBYTE_SUPPORT configure-define specifies that we want to enable
 * complete Unicode conversion between wide characters and multibyte strings.
 */
#if defined MULTIBYTE_SUPPORT \
 || (defined HAVE_WCHAR_H && defined HAVE_WCTOMB && defined __STDC_ISO_10646__)
/*
 * If MULTIBYTE_SUPPORT is not defined, these includes provide a subset of
 * Unicode support that makes the \u and \U printf escape sequences work.
 */

#if defined(__hpux) && !defined(_INCLUDE__STDC_A1_SOURCE)
#define _INCLUDE__STDC_A1_SOURCE
#endif

# include <wchar.h>
# include <wctype.h>
#endif
#ifdef HAVE_LANGINFO_H
#  include <langinfo.h>
#  ifdef HAVE_ICONV
#    include <iconv.h>
#  endif
#endif

#if defined(HAVE_INITGROUPS) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_INITGROUPS
#endif

#if defined(HAVE_GETGRGID) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_GETGRGID
#endif

#if defined(HAVE_GETGRNAM) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_GETGRNAM
#endif

#if defined(HAVE_GETPWENT) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_GETPWENT
#endif

#if defined(HAVE_GETPWNAM) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_GETPWNAM
#endif

#if defined(HAVE_GETPWUID) && !defined(DISABLE_DYNAMIC_NSS)
# define USE_GETPWUID
#endif

#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
# define GET_ST_ATIME_NSEC(st) (st).st_atim.tv_nsec
#elif HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC
# define GET_ST_ATIME_NSEC(st) (st).st_atimespec.tv_nsec
#elif HAVE_STRUCT_STAT_ST_ATIMENSEC
# define GET_ST_ATIME_NSEC(st) (st).st_atimensec
#endif
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
# define GET_ST_MTIME_NSEC(st) (st).st_mtim.tv_nsec
#elif HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
# define GET_ST_MTIME_NSEC(st) (st).st_mtimespec.tv_nsec
#elif HAVE_STRUCT_STAT_ST_MTIMENSEC
# define GET_ST_MTIME_NSEC(st) (st).st_mtimensec
#endif
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
# define GET_ST_CTIME_NSEC(st) (st).st_ctim.tv_nsec
#elif HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC
# define GET_ST_CTIME_NSEC(st) (st).st_ctimespec.tv_nsec
#elif HAVE_STRUCT_STAT_ST_CTIMENSEC
# define GET_ST_CTIME_NSEC(st) (st).st_ctimensec
#endif

#if defined(HAVE_TGETENT) && !defined(ZSH_NO_TERM_HANDLING)
# if defined(ZSH_HAVE_CURSES_H) && defined(ZSH_HAVE_TERM_H)
#  define USES_TERM_H 1
# else
#  ifdef HAVE_TERMCAP_H
#   define USES_TERMCAP_H 1
#  endif
# endif

# ifdef USES_TERM_H
#  ifdef HAVE_TERMIO_H
#   include <termio.h>
#  endif
#  ifdef ZSH_HAVE_CURSES_H
#   include "zshcurses.h"
#  endif
#  include "zshterm.h"
# else
#  ifdef USES_TERMCAP_H
#   include <termcap.h>
#  endif
# endif
#endif

#ifdef HAVE_SRAND_DETERMINISTIC
# define srand srand_deterministic
#endif

#ifdef ZSH_VALGRIND
# include "valgrind/valgrind.h"
# include "valgrind/memcheck.h"
#endif
