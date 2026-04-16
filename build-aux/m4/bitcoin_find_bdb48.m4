dnl Copyright (c) 2013-2015 The Bitcoin Core developers
dnl Distributed under the MIT software license, see the accompanying
dnl file COPYING or http://www.opensource.org/licenses/mit-license.php.

AC_DEFUN([BITCOIN_FIND_BDB48],[
  AC_ARG_VAR(BDB_CFLAGS, [C compiler flags for BerkeleyDB, bypasses autodetection])
  AC_ARG_VAR(BDB_LIBS, [Linker flags for BerkeleyDB, bypasses autodetection])

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
      dnl Ubuntu 20.04+ ships BDB 5.3 - accept it automatically with a warning
      AC_MSG_WARN([Found Berkeley DB other than 4.8 (likely 5.3 on Ubuntu 20.04+). Wallets may not be portable across versions.])
    else
      BITCOIN_SUBDIR_TO_INCLUDE(BDB_CPPFLAGS,[${bdb48path}],db_cxx)
      bdbpath="${bdb48path}"
    fi
  else
    BDB_CPPFLAGS=${BDB_CFLAGS}
  fi
  AC_SUBST(BDB_CPPFLAGS)

  if test "x$BDB_LIBS" = "x"; then
    for searchlib in db_cxx-4.8 db_cxx-5.3 db_cxx; do
      AC_CHECK_LIB([$searchlib],[main],[
        BDB_LIBS="-l${searchlib}"
        break
      ])
    done
    if test "x$BDB_LIBS" = "x"; then
        AC_MSG_ERROR([libdb_cxx missing, ]AC_PACKAGE_NAME[ requires this library for wallet functionality (--disable-wallet to disable wallet functionality)])
    fi
  fi
  AC_SUBST(BDB_LIBS)
])
