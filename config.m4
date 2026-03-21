PHP_REQUIRE_CXX()
CXXFLAGS="$CXXFLAGS -Wall -std=c++11 -Wno-deprecated-register"

PHP_ARG_ENABLE([skywalking],
  [whether to enable skywalking support],
  [AS_HELP_STRING([--enable-skywalking],
    [Enable skywalking support])],
  [yes])

if test "$PHP_THREAD_SAFETY" == "yes"; then
  AC_MSG_ERROR([skywalking does not support ZTS])
fi

AC_LANG_PUSH(C++)
AX_CHECK_COMPILE_FLAG([-std=c++0x], , [AC_MSG_ERROR([compiler not accept c++11])])
AC_LANG_POP()

AC_MSG_CHECKING([for php_json.h])
json_inc_path=""
if test -f "$abs_srcdir/include/php/ext/json/php_json.h"; then
  json_inc_path="$abs_srcdir/include/php"
elif test -f "$abs_srcdir/ext/json/php_json.h"; then
  json_inc_path="$abs_srcdir"
elif test -f "$phpincludedir/ext/json/php_json.h"; then
  json_inc_path="$phpincludedir"
else
  for i in php php7; do
    if test -f "$prefix/include/$i/ext/json/php_json.h"; then
      json_inc_path="$prefix/include/$i"
    fi
  done
fi

if test "$json_inc_path" = ""; then
  AC_MSG_ERROR([Could not find php_json.h, please reinstall the php-json extension])
else
  AC_MSG_RESULT([found in $json_inc_path])
fi

# 检查 SQLite3
AC_MSG_CHECKING([for sqlite3])
have_sqlite3="no"
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.7.0], [
  have_sqlite3="yes"
  PHP_EVAL_LIBLINE($SQLITE3_LIBS, SKYWALKING_SHARED_LIBADD)
  PHP_EVAL_INCLINE($SQLITE3_CFLAGS)
  AC_DEFINE([HAVE_SQLITE3], 1, [Enable SQLite3 support])
], [
  # 回退：使用系统自带的 sqlite3
  AC_CHECK_LIB([sqlite3], [sqlite3_open], [
    have_sqlite3="yes"
    PHP_ADD_LIBRARY(sqlite3,,SKYWALKING_SHARED_LIBADD)
    AC_DEFINE([HAVE_SQLITE3], 1, [Enable SQLite3 support])
  ], [
    have_sqlite3="no"
  ])
])

if test "$have_sqlite3" = "no"; then
  AC_MSG_WARN([SQLite3 library not found, SQLite storage will be disabled])
fi

if test "$PHP_SKYWALKING" != "no"; then

  LIBS="-lpthread $LIBS"
  SKYWALKING_SHARED_LIBADD="-lpthread $SKYWALKING_SHARED_LIBADD"
  PHP_ADD_LIBRARY(pthread)
  PHP_ADD_LIBRARY(dl,,SKYWALKING_SHARED_LIBADD)
  PHP_ADD_LIBRARY(dl)

  case $host in
    *darwin*)
      PHP_ADD_LIBRARY(c++,1,SKYWALKING_SHARED_LIBADD)
      ;;
    *)
      # Linux: 链接 C++ 标准库（解决 GCC 4.8 std::regex 问题）
      PHP_ADD_LIBRARY(stdc++,,SKYWALKING_SHARED_LIBADD)
      PHP_ADD_LIBRARY(stdc++)
      PHP_ADD_LIBRARY(rt,,SKYWALKING_SHARED_LIBADD)
      PHP_ADD_LIBRARY(rt)
      ;;
  esac

  PHP_SUBST(SKYWALKING_SHARED_LIBADD)

  PHP_ADD_INCLUDE(src)

  PHP_NEW_EXTENSION(skywalking, \
      skywalking.cc \
      src/base64.cc \
      src/cross_process_bag.cc \
      src/manager.cc \
      src/segment.cc \
      src/segment_reference.cc \
      src/sky_core_span_log.cc \
      src/sky_execute.cc \
      src/sky_log.cc \
      src/sky_module.cc \
      src/sky_plugin_mysqli.cc \
      src/sky_plugin_curl.cc \
      src/sky_plugin_error.cc \
      src/sky_plugin_hyperf_guzzle.cc \
      src/sky_plugin_predis.cc \
      src/sky_plugin_rabbit_mq.cc \
      src/sky_plugin_redis.cc \
      src/sky_plugin_memcached.cc \
      src/sky_plugin_yar.cc \
      src/sky_plugin_swoole_curl.cc \
      src/sky_rate_limit.cc \
      src/sky_shm.cc \
      src/sky_utils.cc \
      src/span.cc \
      src/tag.cc \
      src/json_builder.cc \
      src/storage/json_storage.cc \
      src/storage/sqlite_storage.cc \
  , $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1, cxx)
fi

if test -r $phpincludedir/ext/mysqli/mysqli_mysqlnd.h; then
    AC_DEFINE([MYSQLI_USE_MYSQLND], 1, [Whether mysqlnd is enabled])
fi
