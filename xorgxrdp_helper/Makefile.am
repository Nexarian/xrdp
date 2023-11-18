AM_CPPFLAGS = \
  -I$(top_srcdir)/common

EXTRA_DIST = xorgxrdp_helper_shaders.c

XRDP_EXTRA_LIBS =
XRDP_EXTRA_SOURCES =

if XRDP_NVENC
AM_CPPFLAGS += -DXRDP_NVENC
AM_CPPFLAGS += $(XRDP_NVENC_CFLAGS)
XRDP_EXTRA_LIBS += $(XRDP_NVENC_LIBS)
XRDP_EXTRA_SOURCES += xorgxrdp_helper_nvenc.c xorgxrdp_helper_nvenc.h \
encoder_headers/nvEncodeAPI_12_1_14.h
endif

if XRDP_YAMI
AM_CPPFLAGS += -DXRDP_YAMI
AM_CPPFLAGS += $(XRDP_YAMI_CFLAGS)
XRDP_EXTRA_LIBS += $(XRDP_YAMI_LIBS)
XRDP_EXTRA_SOURCES += xorgxrdp_helper_yami.c xorgxrdp_helper_yami.h encoder_headers/yami_inf.h
endif

if XRDP_AVC444
AM_CPPFLAGS += -DXRDP_AVC444
endif

bin_PROGRAMS = \
  xorgxrdp_helper

xorgxrdp_helper_SOURCES = \
  xorgxrdp_helper.c \
  xorgxrdp_helper.h \
  xorgxrdp_helper_x11.c \
  xorgxrdp_helper_x11.h \
  xorgxrdp_helper_egl.c \
  xorgxrdp_helper_egl.h \
  xorgxrdp_helper_glx.c \
  xorgxrdp_helper_glx.h \
  $(XRDP_EXTRA_SOURCES)

xorgxrdp_helper_LDADD = \
  $(top_builddir)/common/libcommon.la \
  $(XRDP_EXTRA_LIBS) \
  -lX11 -lepoxy

