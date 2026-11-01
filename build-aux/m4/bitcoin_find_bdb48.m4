dnl Copyright (c) 2013-2015 The Bitcoin Core developers
dnl Distributed under the MIT software license, see the accompanying
dnl file COPYING or http://www.opensource.org/licenses/mit-license.php.

AC_DEFUN([BITCOIN_FIND_BDB48],[
  AC_ARG_VAR(BDB_CFLAGS, [C compiler flags for BerkeleyDB, bypasses autodetection])
  AC_ARG_VAR(BDB_LIBS, [Linker flags for BerkeleyDB, bypasses autodetection])
  AC_ARG_WITH([incompatible-bdb],
    [AS_HELP_STRING([--with-incompatible-bdb], [allow non-portable wallets using Berkeley DB other than 4.8])],
    [use_incompatible_bdb=$withval], [use_incompatible_bdb=no])
  AS_CASE([$use_incompatible_bdb], [yes|no], [],
    [AC_MSG_ERROR([--with-incompatible-bdb accepts only yes or no])])

  if test "x$BDB_CFLAGS" = "x"; then
    AC_MSG_CHECKING([for Berkeley DB C++ headers])
    BDB_CPPFLAGS=
    bdbpath=X
    bdb48path=X
    bdbdirlist=
    for _vn in 4.8 48 4 5 5.3 ''; do
      for _pfx in b lib ''; do
        bdbdirlist="$bdbdirlist ${_pfx}db${_vn}"
      done
    done
    for searchpath in $bdbdirlist ''; do
      test -n "${searchpath}" && searchpath="${searchpath}/"
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
        #include <${searchpath}db_cxx.h>
      ]],[[
        #if !((DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 8) || DB_VERSION_MAJOR > 4)
          #error "failed to find bdb 4.8+"
        #endif
      ]])],[
        if test "x$bdbpath" = "xX"; then
          bdbpath="${searchpath}"
        fi
      ],[
        continue
      ])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
        #include <${searchpath}db_cxx.h>
      ]],[[
        #if !(DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR == 8)
          #error "failed to find bdb 4.8"
        #endif
      ]])],[
        bdb48path="${searchpath}"
        break
      ],[])
    done
    if test "x$bdbpath" = "xX"; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([libdb_cxx headers missing, ]AC_PACKAGE_NAME[ requires this library for wallet functionality (--disable-wallet to disable wallet functionality)])
    elif test "x$bdb48path" = "xX"; then
      BITCOIN_SUBDIR_TO_INCLUDE(BDB_CPPFLAGS,[${bdbpath}],db_cxx)
    else
      BITCOIN_SUBDIR_TO_INCLUDE(BDB_CPPFLAGS,[${bdb48path}],db_cxx)
      bdbpath="${bdb48path}"
    fi
  else
    BDB_CPPFLAGS=${BDB_CFLAGS}
  fi
  AC_SUBST(BDB_CPPFLAGS)

  dnl Enforce portability even when BDB_CFLAGS was supplied explicitly.
  bdb_save_CPPFLAGS=$CPPFLAGS
  bdb_save_CXXFLAGS=$CXXFLAGS
  CPPFLAGS="$CPPFLAGS $BDB_CPPFLAGS"
  CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"
  AC_COMPUTE_INT([bdb_major], [DB_VERSION_MAJOR], [[#include <db_cxx.h>]],
    [AC_MSG_ERROR([cannot determine Berkeley DB header version])])
  AC_COMPUTE_INT([bdb_minor], [DB_VERSION_MINOR], [[#include <db_cxx.h>]],
    [AC_MSG_ERROR([cannot determine Berkeley DB header version])])
  if test "$bdb_major.$bdb_minor" != "4.8"; then
    if test "x$use_incompatible_bdb" != "xyes"; then
      AC_MSG_ERROR([Berkeley DB 4.8 is required for portable wallets. Build it with contrib/build-db4.sh, or explicitly use --with-incompatible-bdb for an isolated non-portable build.])
    fi
    AC_MSG_WARN([Berkeley DB $bdb_major.$bdb_minor: wallets opened by this build may not be usable by older releases. Back up wallets before use.])
  fi

  if test "x$BDB_LIBS" = "x"; then
    dnl Match the selected headers; do not silently link 4.8 headers to 5.3.
    bdb_save_LIBS=$LIBS
    for searchlib in db_cxx-$bdb_major.$bdb_minor db_cxx; do
      LIBS="-l${searchlib} $PTHREAD_LIBS $bdb_save_LIBS"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <db_cxx.h>]], [[Db db(nullptr, 0); db.close(0);]])],[
        BDB_LIBS="-l$searchlib"
        break
      ])
    done
    LIBS=$bdb_save_LIBS
    if test "x$BDB_LIBS" = "x"; then
        AC_MSG_ERROR([libdb_cxx missing, ]AC_PACKAGE_NAME[ requires this library for wallet functionality (--disable-wallet to disable wallet functionality)])
    fi
  fi
  bdb_save_LIBS=$LIBS
  LIBS="$BDB_LIBS $PTHREAD_LIBS $LIBS"
  AC_MSG_CHECKING([whether Berkeley DB C++ headers and library link together])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <db_cxx.h>]], [[Db db(nullptr, 0); db.close(0);]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_ERROR([Berkeley DB C++ library does not link with the selected headers])])
  AC_MSG_CHECKING([whether Berkeley DB library version matches its headers])
  AC_RUN_IFELSE([AC_LANG_PROGRAM([[#include <db_cxx.h>]], [[
    int major = 0, minor = 0;
    db_version(&major, &minor, nullptr);
    return major != DB_VERSION_MAJOR || minor != DB_VERSION_MINOR;
  ]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_ERROR([Berkeley DB library version does not match its headers])],
    [AC_MSG_RESULT([not executable while cross-compiling])
     AC_MSG_WARN([Cross-compiling: verify Berkeley DB library/header versions and wallet portability on the target before use.])])
  LIBS=$bdb_save_LIBS
  CPPFLAGS=$bdb_save_CPPFLAGS
  CXXFLAGS=$bdb_save_CXXFLAGS
  AC_SUBST(BDB_LIBS)
])
