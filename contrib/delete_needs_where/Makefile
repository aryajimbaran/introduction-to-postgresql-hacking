# contrib/delete_needs_where/Makefile

MODULES = delete_needs_where
PGFILEDESC = "delete_needs_where - require DELETE to have a WHERE clause"

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/delete_needs_where
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
