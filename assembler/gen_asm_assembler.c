#include "gen_asm_assembler.h"

enum
{
  PROP_0,

  /* PROP_COMPACT_MODE, */
  PROP_GEN_VERSION,
};

struct _GenAsmAssembler
{
  GObject parent_instance;

  guint gen_version;
};

G_DEFINE_TYPE(GenAsmAssembler, gen_asm_assembler, G_TYPE_OBJECT);

static void
gen_asm_assembler_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GenAsmAssembler *assembler = GEN_ASM_ASSEMBLER (object);

  switch (property_id)
    {
    case PROP_GEN_VERSION:
      g_value_set_uint (value, assembler->gen_version);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gen_asm_assembler_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GenAsmAssembler *assembler = GEN_ASM_ASSEMBLER (object);

  switch (property_id)
    {
    case PROP_GEN_VERSION:
      assembler->gen_version = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gen_asm_assembler_dispose (GObject *object)
{
  G_OBJECT_CLASS (gen_asm_assembler_parent_class)->dispose (object);
}

static void
gen_asm_assembler_class_init (GenAsmAssemblerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  object_class->get_property = gen_asm_assembler_get_property;
  object_class->set_property = gen_asm_assembler_set_property;
  object_class->dispose = gen_asm_assembler_dispose;

  /* /\** */
  /*  * GenAsmAssembler:compact-mode: */
  /*  * */
  /*  * Since: 0.1 */
  /*  *\/ */
  /* pspec = g_param_spec_boolean ("compact-mode", */
  /*                               "Compact Mode", */
  /*                               "Compact Mode", */
  /*                               FALSE, */
  /*                               G_PARAM_READWRITE); */
  /* g_object_class_install_property (object_class, PROP_COMPACT_MODE, pspec); */

  /**
   * GenAsmAssembler:gen-version:
   *
   * Since: 0.1
   */
  pspec = g_param_spec_uint ("gen-version",
                             "Gen Version",
                             "Gen Version",
                             40,
                             90,
                             40,
                             G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_GEN_VERSION, pspec);

}

static void
gen_asm_assembler_init (GenAsmAssembler *assembler)
{
  assembler->gen_version = 40;
}

/**
 * gen_asm_assmbler_assemble:
 *
 * Returns: (transfer full): a new #GBytes containing a Gen binary
 */
GBytes *
gen_asm_assembler_assemble (GenAsmAssembler *assembler, const gchar *source)
{
  g_return_val_if_fail (GEN_ASM_IS_ASSEMBLER (assembler), NULL);
  g_return_val_if_fail (source != NULL, NULL);

  brw_init_context(&genasm_brw_context, gen_level);
  mem_ctx = ralloc_context(NULL);
  brw_init_compile(&genasm_brw_context, &genasm_compile, mem_ctx);

  err = yyparse();

  yylex_destroy();


  return NULL;
}
