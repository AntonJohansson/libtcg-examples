#include "common.h"
#include "analyze-reg-src.h"
#include <assert.h>
#include <stdlib.h> // for abort()

// Returns true if inst is an indirect or direct jump, false otherwise.
// If it's a direct jump address will contain the destination address.
bool is_pc_write(LibTcgArchInfo arch, LibTcgInstruction *inst,
             bool *is_direct, uint64_t *address) {
    if (inst->nb_oargs == 0 || inst->nb_iargs > 2) {
        return false;
    }

    LibTcgArgument *dst = &inst->output_args[0];
    if (dst->kind != LIBTCG_ARG_TEMP ||
        dst->temp->kind != LIBTCG_TEMP_GLOBAL) {
        return false;
    }
    if (dst->temp->mem_offset != arch.pc) {
        return false;
    }

    LibTcgArgument *src = &inst->input_args[0];
    assert(src->kind == LIBTCG_ARG_TEMP);
    bool is_constant = src->temp->kind == LIBTCG_TEMP_CONST;
    if (is_constant && address != NULL) {
            *address = src->temp->val;
    }
    if (is_direct != NULL) {
        *is_direct = is_constant;
    }

    return true;
}

TbNode *find_tb_containing(TbNode *n, uint64_t address) {
    for (; n != NULL; n = n->next) {
        if (address >= n->address &&
            address < n->address + n->tb.size_in_bytes) {
            return n;
        }
    }
    return NULL;
}

int find_instruction_from_address(TbNode *n, uint64_t address) {
    size_t i = 0;
    for (; i < n->tb.instruction_count; ++i) {
        LibTcgInstruction *inst = &n->tb.list[i];
        if (inst->opcode != LIBTCG_op_insn_start) {
            continue;
        }
        LibTcgArgument *arg = &inst->constant_args[0];
        assert(arg->kind == LIBTCG_ARG_CONSTANT);
        if (arg->constant == address) {
            return i;
        }
    }
    return -1;
}

typedef struct FoldAddSub {
    bool valid;
    int64_t value;
} FoldAddSub;

static FoldAddSub fold_add_sub(LibTcgArchInfo arch_info,
                               bool stack_grows_down,
                               SrcInfo *info) {
    LibTcgInstruction *inst = &info->node->tb.list[info->inst_index];

    if (inst->opcode != LIBTCG_op_add_i32 &&
        inst->opcode != LIBTCG_op_add_i64 &&
        inst->opcode != LIBTCG_op_sub_i32 &&
        inst->opcode != LIBTCG_op_sub_i64 &&
        inst->opcode != LIBTCG_op_mov_i32 &&
        inst->opcode != LIBTCG_op_mov_i64) {
        return (FoldAddSub) {0};
    }

    int64_t left = 0;
    if (info->children[0].num_branches > 0) {
        for (size_t j = 0; j < info->children[0].num_branches; ++j) {
            FoldAddSub fold = fold_add_sub(arch_info, stack_grows_down, &info->children[0].branches[j]);
            if (!fold.valid) {
                return fold;
            }
            left = largest_stack_offset(stack_grows_down, left, fold.value);
        }
    } else if (inst->input_args[0].kind == LIBTCG_ARG_TEMP &&
               inst->input_args[0].temp->kind == LIBTCG_TEMP_GLOBAL &&
               (inst->input_args[0].temp->mem_offset == arch_info.bp ||
                inst->input_args[0].temp->mem_offset == arch_info.sp)) {
        left = 0;
    } else if (inst->input_args[0].kind == LIBTCG_ARG_TEMP &&
               inst->input_args[0].temp->kind == LIBTCG_TEMP_CONST) {
        left = inst->input_args[0].temp->val;
    } else {
        return (FoldAddSub) {0};
    }

    int64_t right = 0;
    if (inst->nb_iargs == 2) {
        if (info->children[1].num_branches > 0) {
            for (size_t j = 0; j < info->children[1].num_branches; ++j) {
                FoldAddSub fold = fold_add_sub(arch_info, stack_grows_down, &info->children[1].branches[j]);
                if (!fold.valid) {
                    return fold;
                }
                right = largest_stack_offset(stack_grows_down, right, fold.value);
            }
        } else if (inst->input_args[1].kind == LIBTCG_ARG_TEMP &&
                   inst->input_args[1].temp->kind == LIBTCG_TEMP_GLOBAL &&
                   (inst->input_args[1].temp->mem_offset == arch_info.bp ||
                    inst->input_args[1].temp->mem_offset == arch_info.sp)) {
            right = 0;
        } else if (inst->input_args[1].kind == LIBTCG_ARG_TEMP &&
                   inst->input_args[1].temp->kind == LIBTCG_TEMP_CONST) {
            right = inst->input_args[1].temp->val;
        } else {
            return (FoldAddSub) {0};
        }
    }

    int64_t result;
    switch (inst->opcode) {
    case LIBTCG_op_add_i32:
        result = (int32_t) left + (int32_t) right;
        break;
    case LIBTCG_op_add_i64:
        result = left + right;
        break;
    case LIBTCG_op_sub_i32:
        result = (int32_t) left - (int32_t) right;
        break;
    case LIBTCG_op_sub_i64:
        result = left - right;
        break;
    case LIBTCG_op_mov_i32:
    case LIBTCG_op_mov_i64:
        result = left;
        break;
    default:
        abort();
    }

    //printf("  %ld %ld %ld\n", result, left, right);

    return (FoldAddSub) {
        .valid = true,
        .value = result,
    };
}

static bool fold_ld_st_stack_pointer(LibTcgArchInfo arch_info,
                                     SrcInfoBranch *child,
                                     int64_t *offset) {
    bool stack_grows_down = true;

    int64_t value = 0;
    for (size_t j = 0; j < child->num_branches; ++j) {
        FoldAddSub fold = fold_add_sub(arch_info, stack_grows_down, &child->branches[j]);
        if (!fold.valid) {
            return false;
        }
        value = largest_stack_offset(stack_grows_down, value, fold.value);
    }
    *offset = ABS(value);
    return true;
}

bool is_stack_ld_fancy(LibTcgArchInfo arch_info,
                       Memory *memory,
                       TbNode *n,
                       LibTcgInstruction *inst,
                       size_t inst_index,
                       int64_t *offset) {
    if (inst->opcode != LIBTCG_op_qemu_ld_a32_i32 &&
        inst->opcode != LIBTCG_op_qemu_ld_a64_i32 &&
        inst->opcode != LIBTCG_op_qemu_ld_a32_i64 &&
        inst->opcode != LIBTCG_op_qemu_ld_a64_i64) {
        return false;
    }

    StackMarker marker = stack_marker(&memory->persistent);
    SrcInfo *info = find_sources(arch_info, memory, n, inst_index, 1);
    SrcInfoBranch *child = &info->children[0];
    bool folded = fold_ld_st_stack_pointer(arch_info, child, offset);
    stack_reset_to_marker(&memory->persistent, marker);
    return folded;
}

bool is_stack_st_fancy(LibTcgArchInfo arch_info,
                       Memory *memory,
                       TbNode *n,
                       LibTcgInstruction *inst,
                       size_t inst_index,
                       int64_t *offset) {
    if (inst->opcode != LIBTCG_op_qemu_st_a32_i32 &&
        inst->opcode != LIBTCG_op_qemu_st_a64_i32 &&
        inst->opcode != LIBTCG_op_qemu_st_a32_i64 &&
        inst->opcode != LIBTCG_op_qemu_st_a64_i64) {
        return false;
    }

    StackMarker marker = stack_marker(&memory->persistent);
    SrcInfo *info = find_sources(arch_info, memory, n, inst_index, 1);
    SrcInfoBranch *child = &info->children[1];
    bool folded = fold_ld_st_stack_pointer(arch_info, child, offset);
    stack_reset_to_marker(&memory->persistent, marker);
    return folded;
}
