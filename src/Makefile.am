if DEBUG
   AM_CPPFLAGS = @VMOD_INCLUDES@ -Wall -Werror -DDEBUG
else
   AM_CPPFLAGS = @VMOD_INCLUDES@ -Wall -Werror
endif

vmoddir = @VMOD_DIR@
vmod_LTLIBRARIES = libvmod_headerproxy.la

libvmod_headerproxy_la_CFLAGS = $(VMOD_INCLUDES) $(CURL_CFLAGS)
libvmod_headerproxy_la_LDFLAGS = -module -export-dynamic -avoid-version -shared $(CURL_LIBS)

libvmod_headerproxy_la_SOURCES = \
	vcc_if.c vcc_if.h \
	proxy.c proxy.hf \
	jsmn.c jsmn.h \
	vmod_headerproxy.c

vmod_headerproxy.lo: vcc_if.c vcc_if.h

vcc_if.c: vcc_if.h

vcc_if.h: @VMODTOOL@ $(top_srcdir)/src/vmod_headerproxy.vcc
	@VMODTOOL@ $(top_srcdir)/src/vmod_headerproxy.vcc

VMOD_TESTS = $(top_srcdir)/src/tests/*.vtc
.PHONY: $(VMOD_TESTS)

$(top_srcdir)/src/tests/*.vtc: libvmod_headerproxy.la
	@VARNISHTEST@ -Dvarnishd=@VARNISHD@ -Dvmod_topbuild=$(abs_top_builddir) $@

check: $(VMOD_TESTS)

EXTRA_DIST = \
	vmod_headerproxy.vcc \
	$(VMOD_TESTS)

CLEANFILES = \
	$(builddir)/vcc_if.c \
	$(builddir)/vcc_if.h \
	$(builddir)/vmod_headerproxy.rst \
	$(builddir)/vmod_headerproxy.man.rst
