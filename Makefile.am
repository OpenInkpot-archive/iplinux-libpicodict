AM_CFLAGS = -Wall --std=gnu99

include_HEADERS = libpicodict.h

lib_LTLIBRARIES = libpicodict.la
libpicodict_la_LDFLAGS = -no-undefined -version-info 1:0:0
libpicodict_la_SOURCES = libpicodict.c

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libpicodict.pc

noinst_PROGRAMS = picodict-test picodict-verify
picodict_test_LDADD = libpicodict.la
picodict_verify_LDADD = libpicodict.la
