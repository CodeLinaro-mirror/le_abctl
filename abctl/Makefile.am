ACLOCAL_AMFLAGS = -I m4

# Build libabctl.so
AM_CPPFLAGS  = $(GLIB_CFLAGS) -include glib.h -Dstrlcpy=g_strlcpy
lib_LTLIBRARIES        = libabctl.la

libabctl_la_SOURCES    = src/libabctl.cpp

libabctl_la_CPPFLAGS   = -I$(top_srcdir)/inc
libabctl_la_CPPFLAGS  += -std=c++11 -Wall -Wno-unused-parameter
libabctl_la_CPPFLAGS  += $(AM_CPPFLAGS) -Wno-deprecated-declarations

libabctl_la_LDFLAGS    = -lgpt
libabctl_la_LIBADD     = $(GLIB_LIBS)

# Build abctl utility
root_sbindir           = "/sbin"
root_sbin_PROGRAMS     = abctl

abctl_SOURCES  = src/abctl.cpp

abctl_CPPFLAGS  = -I$(top_srcdir)/inc
abctl_CPPFLAGS += -std=c++11 -Wall -Wno-unused-parameter
abctl_LDADD     = libabctl.la

# Export header
abctlheaders_includedir      = $(includedir)
abctlheaders_include_HEADERS = $(top_srcdir)/inc/libabctl.h
