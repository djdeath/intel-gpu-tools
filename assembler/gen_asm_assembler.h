#ifndef __GEN_ASM_ASSEMBLER_H__
#define __GEN_ASM_ASSEMBLER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GEN_ASM_TYPE_ASSEMBLER gen_asm_assembler_get_type ()
G_DECLARE_FINAL_TYPE(GenAsmAssembler, gen_asm_assembler, GEN_ASM, ASSEMBLER, GObject)

GBytes *gen_asm_assembler_assemble (GenAsmAssembler *assembler, const gchar *source);

G_END_DECLS

#endif /* __GEN_ASM_ASSEMBLER_H__ */
