AC_PREREQ([2.69])
AC_INIT(mask, 1.0, johan.gunnarsson@gmail.com)
AC_CONFIG_SRCDIR([src/mask.c])

AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 0.0.0)
PKG_CHECK_MODULES(GST, gstreamer-1.0 >= 0.0.0)
PKG_CHECK_MODULES(GST_APP, gstreamer-app-1.0 >= 0.0.0)

AC_OUTPUT(Makefile src/Makefile)
