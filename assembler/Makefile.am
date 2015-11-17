SUBDIRS = doc test

noinst_LTLIBRARIES = libbrw.la

bin_PROGRAMS = intel-gen4asm intel-gen4disasm

libbrw_la_SOURCES =		\
	brw_compat.h		\
	brw_context.c		\
	brw_context.h		\
	brw_disasm.c		\
	brw_defines.h		\
	brw_eu.h		\
	brw_eu.c		\
	brw_eu_compact.c	\
	brw_eu_debug.c		\
	brw_eu_emit.c		\
	brw_eu_util.c		\
	brw_reg.h		\
	brw_structs.h		\
	gen8_disasm.c		\
	gen8_instruction.h	\
	gen8_instruction.c	\
	ralloc.c		\
	ralloc.h		\
	$(NULL)

AM_YFLAGS = -d --warnings=all
AM_CFLAGS= $(ASSEMBLER_WARN_CFLAGS)

LEX = flex -i
BUILT_SOURCES = gram.h gram.c lex.c
gram.h: gram.c

intel_gen4asm_SOURCES =	\
	gen4asm.h	\
	gram.y		\
	lex.l		\
	main.c		\
	$(NULL)

intel_gen4asm_LDADD = libbrw.la

intel_gen4disasm_SOURCES =  disasm-main.c
intel_gen4disasm_LDADD = libbrw.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = intel-gen4asm.pc

CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = \
	README \
	TODO \
	intel-gen4asm.pc.in

if HAVE_INTROSPECTION

lib_LTLIBRARIES = libgenasm.la

libgenasm_la_SOURCES =		\
	gen_asm_assembler.h	\
	gen_asm_assembler.c	\
	$(NULL)

libgenasm_la_CFLAGS =	\
	$(GLIB_CFLAGS)	\
	$(NULL)
libgenasm_la_LDFLAGS =	\
	-export-dynamic	\
	$(NULL)
libgenasm_la_LIBADD =	\
	$(GLIB_LIBS)	\
	libbrw.la	\
	$(NULL)


BUILT_GIRSOURCES =

GenAsm-0.1.gir: $(INTROSPECTION_SCANNER) libgenasm.la
	$(QUIET_GEN) \
		$(INTROSPECTION_SCANNER) -v \
		--namespace GenAsm --nsversion=0.1 \
		--warn-all \
		--add-include-path=$(srcdir) --add-include=path=. \
		--include=GObject-2.0 \
		--library=libgenasm.la \
		--libtool="${LIBTOOL}" \
		--output $@ \
		--pkg gobject-2.0 \
		--pkg-export genasm-0.1 \
		$(libgenasm_la_SOURCES)

BUILT_GIRSOURCES += GenAsm-0.1.gir

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/
typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(QUIET_GEN)$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=. \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(builddir)/$(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
