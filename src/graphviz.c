#include "graphviz.h"
#include "common.h"
#include "analyze-reg-src.h"
#include "analyze-max-stack.h"
#include "color.h"
#include <qemu/libtcg/libtcg.h>
#include <assert.h>
#include <string.h>

typedef enum ColorName {
    COLOR_INSTRUCTION = 0,
    COLOR_REGISTER,
    COLOR_CONSTANT,
    COLOR_COMMENT,
    COLOR_BASE,
    COLOR_BORDER,
} ColorName;

typedef struct ColorData {
    ColorHSL hsl;
    float alpha;
    const char *str;
} ColorData;

static ColorData colors_default[] = {
    [COLOR_INSTRUCTION] = {{0.0f,   0.5f,  0.45f}, 1.0f, NULL},
    [COLOR_REGISTER]    = {{183.0f, 0.55f, 0.40f}, 1.0f, NULL},
    [COLOR_CONSTANT]    = {{28.0f,  0.50f, 0.45f}, 1.0f, NULL},
    [COLOR_COMMENT]     = {{120.0f, 0.50f, 0.35f}, 1.0f, NULL},
    [COLOR_BASE]        = {{183.0f, 0.55f, 0.20f}, 1.0f, NULL},
    [COLOR_BORDER]      = {{183.0f, 0.55f, 0.20f}, 1.0f, NULL},
};

static ColorData colors_dim[] = {
    [COLOR_INSTRUCTION] = {{0.0f,   0.5f,  0.7f*0.45f}, 0.25f, NULL},
    [COLOR_REGISTER]    = {{183.0f, 0.55f, 0.7f*0.40f}, 0.25f, NULL},
    [COLOR_CONSTANT]    = {{28.0f,  0.50f, 0.7f*0.45f}, 0.25f, NULL},
    [COLOR_COMMENT]     = {{120.0f, 0.50f, 0.7f*0.35f}, 0.25f, NULL},
    [COLOR_BASE]        = {{183.0f, 0.55f, 0.7f*0.20f}, 0.25f, NULL},
    [COLOR_BORDER]      = {{183.0f, 0.55f, 0.20f},      0.25f, NULL},
};

static inline float reg_src_hue(uint32_t index) {
    return fmodf(index * 360.0f/7.123f, 360.0f);
}

static const char *hsl_to_str(StackAllocator *stack,
                              ColorHSL hsl,
                              float alpha) {
    ColorRGB rgb = hsl_to_rgb(hsl);
    char *buf = stack_alloc(stack, 10*sizeof(char));
    uint8_t a = 255.0f*alpha;
    snprintf(buf, 10, "#%02x%02x%02x%02x", rgb.r, rgb.g, rgb.b, a);
    return buf;
}

static const char *color_str(StackAllocator *stack,
                             ColorData *colors,
                             ColorName name) {
    if (colors[name].str == NULL) {
        colors[name].str = hsl_to_str(stack, colors[name].hsl, colors[name].alpha);
    }
    return colors[name].str;
}

static void font_begin(FILE *fd, bool bold, const char *str_col) {
    fprintf(fd, "<font color=\"%s\">", str_col);
    if (bold) {
        fputs("<b>", fd);
    }
}

static void font_end(FILE *fd, bool bold) {
    if (bold) {
        fputs("</b>", fd);
    }
    fputs("</font>", fd);
}

void graphviz_output(LibTcgInterface *libtcg, StackAllocator *stack,
                     GraphvizSettings settings,
                     FILE *fd, TbNode *root, bool analyze_max_stack,
                     CmdLineRegTuple analyze_reg_src, TbNode *reg_src_node,
                     int reg_src_index) {
    assert(fd != NULL);

    LibTcgArchInfo arch_info = libtcg->get_arch_info();

    ColorData *colors = colors_default;

    fputs("digraph {\n", fd);
    fprintf(fd, "nodesep = %f\n", settings.nodesep);
    fprintf(fd, "ranksep = %f\n", settings.ranksep);
    fputs("graph [fontname = \"inconsolata\"];\n", fd);
    fprintf(fd, "node [fontname = \"inconsolata\" pencolor=\"%s\"];\n", color_str(stack, colors, COLOR_BORDER));
    fprintf(fd, "edge [fontname = \"inconsolata\", penwidth=2, color=\"%s\"];\n", color_str(stack, colors, COLOR_BORDER));

    char buf[256] = {0};
    for (TbNode *n = root; n != NULL; n = n->next) {
        fprintf(fd, "\"%lx\" [shape = \"none\", label=<\n", n->address);

        fprintf(fd, "<table border=\"2\" cellborder=\"0\" cellspacing=\"0\">");
        for (size_t i = 0; i < n->tb.instruction_count; ++i) {
            LibTcgInstruction *inst = &n->tb.list[i];

            bool is_src_inst = (n == reg_src_node && i == (size_t)reg_src_index);

            fputs("<tr>\n", fd);

            SrcInfo *src_info = NULL;
            if (analyze_reg_src.present) {
                colors = colors_dim;

                if (is_src_inst) {
                    colors = colors_default;
                    src_info = n->reg_src_info[i];
                } else if (n->reg_src_info != NULL &&
                    n->reg_src_info[i] != NULL) {
                    src_info = n->reg_src_info[i];
                    colors = colors_default;
                }
            }

            if (analyze_max_stack) {
                int64_t r = n->stack_state[i].max_ld_size;
                int64_t w = n->stack_state[i].max_st_size;

                fprintf(fd, "<td cellspacing=\"3\" border=\"0\" align=\"left\">");
                font_begin(fd, false, color_str(stack, colors, COLOR_COMMENT));
                if (inst->opcode == LIBTCG_op_insn_start) {
                    fprintf(fd, "r");
                } else if (r == STACK_SIZE_TOP) {
                    fprintf(fd, "?");
                } else {
                    fprintf(fd, "%ld", r);
                }
                font_end(fd, false);
                fprintf(fd, "</td>");

                fprintf(fd, "<td cellspacing=\"3\" border=\"0\" align=\"left\">");
                font_begin(fd, false, color_str(stack, colors, COLOR_COMMENT));
                if (inst->opcode == LIBTCG_op_insn_start) {
                    fprintf(fd, "w");
                } else if (r == STACK_SIZE_TOP) {
                    fprintf(fd, "?");
                } else {
                    fprintf(fd, "%ld", w);
                }
                font_end(fd, false);
                fprintf(fd, "</td>");
            }

            fputs("<td cellspacing=\"3\" align=\"left\"", fd);
            if (src_info != NULL) {
                LibTcgArgument *arg;
                if (src_info->op_index >= 0) {
                    arg = &inst->output_args[src_info->op_index];
                } else if (is_src_inst) {
                    int index = analyze_reg_src.operand_index;
                    if (index >= inst->nb_oargs) {
                         index -= inst->nb_oargs;
                    }
                    arg = &inst->output_args[index];
                } else {
                    arg = &inst->input_args[1];
                }
                fprintf(fd, " bgcolor=\"%s\"", hsl_to_str(stack, (ColorHSL){
                    .h = reg_src_hue(arg->temp->index),
                    .s = 0.5f,
                    .l = 0.9f,
                }, 1.0f));
            }
            int border = (is_src_inst) ? 2 : 0;
            fprintf(fd, " sides=\"tb\" border=\"%d\"", border);
            fputs(">\n", fd);

            if (inst->opcode == LIBTCG_op_insn_start) {
                font_begin(fd, false, color_str(stack, colors, COLOR_COMMENT));
                memset(buf, 0, 256);
                libtcg->dump_instruction_to_buffer(inst, buf, 256);
                fputs(buf, fd);
                font_end(fd, false);
            } else {
                font_begin(fd, false, color_str(stack, colors, COLOR_INSTRUCTION));
                memset(buf, 0, 256);
                libtcg->dump_instruction_name_to_buffer(inst, buf, 256);
                fputs(buf, fd);
                fputs(" ", fd);
                font_end(fd, false);

                if (inst->opcode == LIBTCG_op_call) {
                    LibTcgHelperInfo info = libtcg->get_helper_info(inst);
                    font_begin(fd, false, color_str(stack, colors, COLOR_REGISTER));
                    fputs(info.func_name, fd);
                    fputs(" ", fd);
                    font_end(fd, false);
                }

                if (inst->nb_oargs > 0) {
                    for (int i = 0; i < inst->nb_oargs; ++i) {
                        if (i > 0) {
                            font_begin(fd, false, color_str(stack, colors, COLOR_BASE));
                            fprintf(fd, ", ");
                            font_end(fd, false);
                        }
                        LibTcgTempKind kind = inst->output_args[i].temp->kind;
                        ColorName color_name;
                        switch (kind) {
                        case LIBTCG_TEMP_CONST:
                            color_name = COLOR_CONSTANT;
                            break;
                        case LIBTCG_TEMP_GLOBAL:
                            color_name = COLOR_REGISTER;
                            break;
                        default:
                            color_name = COLOR_BASE;
                        }

                        bool highlight = unlikely(src_info != NULL && src_info->op_index == i);
                        bool is_src_op = unlikely(is_src_inst && src_info != NULL && analyze_reg_src.operand_index == i);
                        const char *str_col = NULL;
                        if (highlight) {
                            str_col = hsl_to_str(stack, (ColorHSL){
                                .h = reg_src_hue(inst->output_args[i].temp->index),
                                .s = 0.7f,
                                .l = 0.5f,
                            }, 1.0f);
                        } else {
                            str_col = color_str(stack, colors, color_name);
                        }

                        font_begin(fd, highlight, str_col);
                        if (is_src_op) {
                            fputs("[", fd);
                        }
                        fprintf(fd, "%s", inst->output_args[i].temp->name);
                        if (is_src_op) {
                            fputs("]", fd);
                        }
                        font_end(fd, highlight);
                    }
                }

                if (inst->nb_iargs > 0) {
                    for (int i = 0; i < inst->nb_iargs; ++i) {
                        if (i > 0 || inst->nb_oargs > 0) {
                            font_begin(fd, false, color_str(stack, colors, COLOR_BASE));
                            fprintf(fd, ", ");
                            font_end(fd, false);
                        }
                        LibTcgTempKind kind = inst->input_args[i].temp->kind;
                        ColorName color_name;
                        switch (kind) {
                        case LIBTCG_TEMP_CONST:
                            color_name = COLOR_CONSTANT;
                            break;
                        case LIBTCG_TEMP_GLOBAL:
                            color_name = COLOR_REGISTER;
                            break;
                        default:
                            color_name = COLOR_BASE;
                        }

                        bool highlight = unlikely(src_info != NULL && src_info->op_index == i);
                        bool is_src_op = unlikely(is_src_inst && src_info != NULL && analyze_reg_src.operand_index == i);
                        const char *str_col = NULL;
                        if (highlight) {
                            str_col = hsl_to_str(stack, (ColorHSL){
                                .h = reg_src_hue(inst->input_args[i].temp->index),
                                .s = 0.7f,
                                .l = 0.5f,
                            }, 1.0f);
                        } else {
                            str_col = color_str(stack, colors, color_name);
                        }

                        font_begin(fd, highlight, str_col);
                        if (is_src_op) {
                            fputs("[", fd);
                        }
                        if (settings.compact_args &&
                            inst->input_args[i].temp->kind == LIBTCG_TEMP_CONST) {
                            if (is_pc_write(arch_info, inst, NULL, NULL)) {
                                fprintf(fd, "$0x%lx", inst->input_args[i].temp->val);
                            } else {
                                fprintf(fd, "$%ld", inst->input_args[i].temp->val);
                            }
                        } else {
                            fprintf(fd, "%s", inst->input_args[i].temp->name);
                        }
                        if (is_src_op) {
                            fputs("]", fd);
                        }
                        font_end(fd, highlight);
                    }
                }

                bool is_ld = inst->opcode == LIBTCG_op_qemu_ld_a32_i32 ||
                             inst->opcode == LIBTCG_op_qemu_ld_a64_i32 ||
                             inst->opcode == LIBTCG_op_qemu_ld_a32_i64 ||
                             inst->opcode == LIBTCG_op_qemu_ld_a64_i64;

                bool is_st = inst->opcode == LIBTCG_op_qemu_st_a32_i32 ||
                             inst->opcode == LIBTCG_op_qemu_st_a64_i32 ||
                             inst->opcode == LIBTCG_op_qemu_st_a32_i64 ||
                             inst->opcode == LIBTCG_op_qemu_st_a64_i64;

                if (inst->nb_cargs > 0 && !(settings.compact_args && (is_ld || is_st))) {
                    for (int i = 0; i < inst->nb_cargs; ++i) {
                        if (i > 0 || inst->nb_oargs + inst->nb_iargs > 0) {
                            font_begin(fd, false, color_str(stack, colors, COLOR_BASE));
                            fprintf(fd, ", ");
                            font_end(fd, false);
                        }

                        memset(buf, 0, ARRLEN(buf));
                        libtcg->dump_constant_arg_to_buffer(&inst->constant_args[i], buf, ARRLEN(buf));
                        font_begin(fd, false, color_str(stack, colors, COLOR_CONSTANT));
                        fprintf(fd, "%s", buf);
                        font_end(fd, false);
                    }
                }
                fputs("  ", fd);
            }

            fputs("</td>", fd);
            fputs("</tr>\n", fd);
        }
        fputs("</table>", fd);
        fputs(">];\n", fd);
    }
    for (TbNode *n = root; n != NULL; n = n->next) {
        for (size_t i = 0; i < n->num_succ; ++i) {
            if (n->succ[i].type == FALLTHROUGH) {
                const char *dashed = (settings.dashed_fallthrough_edges) ? 
                                         " [style = dashed]"
                                     :
                                         "";
                fprintf(fd, "\"%lx\":s -> \"%lx\":n%s\n",
                        n->address,
                        n->succ[i].dst_node->address,
                        dashed);
            } else {
                fprintf(fd, "\"%lx\":s -> \"%lx\":n\n",
                        n->address,
                        n->succ[i].dst_node->address);
            }
        }
    }
    fputs("}\n", fd);
}
