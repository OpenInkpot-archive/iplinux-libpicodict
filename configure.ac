AC_PREREQ(2.57)
AC_INIT(libpicodict, 0.1, dottedmag@dottedmag.net)
AM_INIT_AUTOMAKE(foreign)

AC_PROG_CC_C99
AM_PROG_LIBTOOL

AC_CHECK_LIB([z], [inflate])
AC_REPLACE_FUNCS([le16toh])

AC_OUTPUT([Makefile libpicodict.pc])
