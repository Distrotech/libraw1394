# process this file with automake to create a Makefile.in

SUBDIRS = src tools doc

pkgconfigdir = @libdir@/pkgconfig
pkgconfig_DATA = libraw1394.pc

EXTRA_DIST = libraw1394.pc libraw1394.spec

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config.h.in stamp-h.in \
	depcomp install-sh missing mkinstalldirs config.guess ltmain.sh

doc: psdoc pdfdoc htmldoc

psdoc:
	$(MAKE) -C doc psdoc

pdfdoc:
	$(MAKE) -C doc pdfdoc

htmldoc:
	$(MAKE) -C doc htmldoc

.PHONY: doc psdoc pdfdoc htmldoc

dist-hook:
	cd $(distdir) && gitlog2changelog.py && cd -
