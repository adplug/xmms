libdir = @xmms_input_plugin_dir@

lib_LTLIBRARIES = libadplugxmms.la

libadplugxmms_la_LDFLAGS = -lstdc++ -module -avoid-version

libadplugxmms_la_SOURCES = adplug-xmms.cc

libadplugxmms_la_LIBADD = $(adplug_LIBS) @XMMS_LIBS@ @PTHREAD_LIBS@

adplug_data_dir = $(sharedstatedir)/adplug

AM_CPPFLAGS = $(adplug_CFLAGS) @XMMS_CFLAGS@ @PTHREAD_CFLAGS@ \
	-DADPLUG_DATA_DIR=\"$(adplug_data_dir)\"
