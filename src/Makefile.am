libdir = @XMMS_INPUT_PLUGIN_DIR@

lib_LTLIBRARIES = libadplugxmms.la

libadplugxmms_la_LDFLAGS = -lstdc++ -module -avoid-version

libadplugxmms_la_SOURCES = adplug-xmms.cc

libadplugxmms_la_LIBADD = @XMMS_LIBS@ @PTHREAD_LIBS@

AM_CPPFLAGS = @XMMS_CFLAGS@ @PTHREAD_CFLAGS@
