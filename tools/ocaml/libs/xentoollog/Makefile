OCAML_TOPLEVEL=$(CURDIR)/../..
XEN_ROOT=$(OCAML_TOPLEVEL)/../..
include $(OCAML_TOPLEVEL)/common.make

# allow mixed declarations and code
CFLAGS += -Wno-declaration-after-statement

CFLAGS += $(CFLAGS_libxentoollog)
CFLAGS += $(APPEND_CFLAGS)
OCAMLINCLUDE +=

OBJS = xentoollog
INTF = xentoollog.cmi
LIBS = xentoollog.cma xentoollog.cmxa

LIBS_xentoollog = $(LDLIBS_libxentoollog)

xentoollog_OBJS = $(OBJS)
xentoollog_C_OBJS = xentoollog_stubs

OCAML_LIBRARY = xentoollog

GENERATED_FILES += xentoollog.ml xentoollog.ml.tmp xentoollog.mli xentoollog.mli.tmp
GENERATED_FILES += _xtl_levels.mli.in _xtl_levels.ml.in _xtl_levels.inc META

all: $(INTF) $(LIBS)

xentoollog.ml: xentoollog.ml.in _xtl_levels.ml.in
	$(Q)sed -e '1i\
(*\
 * AUTO-GENERATED FILE DO NOT EDIT\
 * Generated from xentoollog.ml.in and _xtl_levels.ml.in\
 *)\
' \
	    -e '/^(\* @@XTL_LEVELS@@ \*)$$/r_xtl_levels.ml.in' \
	  < xentoollog.ml.in > xentoollog.ml.tmp
	$(Q)mv xentoollog.ml.tmp xentoollog.ml

xentoollog.mli: xentoollog.mli.in _xtl_levels.mli.in
	$(Q)sed -e '1i\
(*\
 * AUTO-GENERATED FILE DO NOT EDIT\
 * Generated from xentoollog.mli.in and _xtl_levels.mli.in\
 *)\
' \
	    -e '/^(\* @@XTL_LEVELS@@ \*)$$/r_xtl_levels.mli.in' \
	  < xentoollog.mli.in > xentoollog.mli.tmp
	$(Q)mv xentoollog.mli.tmp xentoollog.mli

libs: $(LIBS)

_xtl_levels.ml.in _xtl_levels.mli.in _xtl_levels.inc: genlevels.py $(XEN_INCLUDE)/xentoollog.h
	$(PYTHON) genlevels.py _xtl_levels.mli.in _xtl_levels.ml.in _xtl_levels.inc

.PHONY: install
install: $(LIBS) META
	mkdir -p $(OCAMLDESTDIR)
	ocamlfind remove -destdir $(OCAMLDESTDIR) xentoollog
	ocamlfind install -destdir $(OCAMLDESTDIR) -ldconf ignore xentoollog META $(INTF) $(LIBS) *.a *.so *.cmx

.PHONY: uninstall
uninstall:
	ocamlfind remove -destdir $(OCAMLDESTDIR) xentoollog

include $(OCAML_TOPLEVEL)/Makefile.rules
