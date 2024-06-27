#include "common.h"
#include <assert.h>

// Returns true if inst is an indirect or direct jump, false otherwise.
// If it's a direct jump address will contain the destination address.
bool is_jump(LibTcgInterface *libtcg, LibTcgInstruction *inst,
             bool *is_direct, uint64_t *address) {
    if (inst->opcode != LIBTCG_op_mov_i32 &&
        inst->opcode != LIBTCG_op_mov_i64) {
        return false;
    }

    LibTcgArgument *dst = &inst->output_args[0];
    if (dst->kind != LIBTCG_ARG_TEMP ||
        dst->temp->kind != LIBTCG_TEMP_GLOBAL) {
        return false;
    }
    if (dst->temp->mem_offset != libtcg->pc) {
        return false;
    }

    LibTcgArgument *src = &inst->input_args[0];
    assert(src->kind == LIBTCG_ARG_TEMP);
    if (src->temp->kind == LIBTCG_TEMP_CONST) {
        *is_direct = true;
        *address = src->temp->val;
    } else {
        *is_direct = false;
    }
    return true;
}

TbNode *find_tb_containing(TbNode *n, uint64_t address) {
    for (; n != NULL; n = n->next) {
        if (address >= n->address &&
            address < n->address + n->list.size_in_bytes) {
            return n;
        }
    }
    return NULL;
}

int find_instruction_from_address(TbNode *n, uint64_t address) {
    int i = 0;
    for (; i < n->list.instruction_count; ++i) {
        LibTcgInstruction *inst = &n->list.list[i];
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

bool is_stack_ld(LibTcgInterface *libtcg,
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

    LibTcgArgument *ptr_op = &inst->input_args[0];
    assert(ptr_op->kind == LIBTCG_ARG_TEMP);
    LibTcgInstruction *ptr_inst = NULL;
    for (int j = inst_index - 1; j > 0; --j) {
        LibTcgInstruction *inst = &n->list.list[j];
        if (inst->nb_oargs > 0 &&
            inst->output_args[0].temp->index == ptr_op->temp->index) {
            ptr_inst = inst;
            break;
        }
    }
    if (ptr_inst == NULL) {
        return false;
    }
    if (ptr_inst->opcode == LIBTCG_op_add_i32 ||
        ptr_inst->opcode == LIBTCG_op_add_i64) {
        LibTcgArgument *op0 = &ptr_inst->input_args[0];
        LibTcgArgument *op1 = &ptr_inst->input_args[1];
        if (op0->kind != LIBTCG_ARG_TEMP ||
            op0->temp->kind != LIBTCG_TEMP_GLOBAL ||
            op1->kind != LIBTCG_ARG_TEMP ||
            op1->temp->kind != LIBTCG_TEMP_CONST) {
            return false;
        }
        if (op0->temp->mem_offset != libtcg->bp) {
            return false;
        }
        *offset = ABS(op1->temp->val);
        return true;
    } else {
        return false;
    }
}

bool is_stack_st(LibTcgInterface *libtcg,
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

    LibTcgArgument *ptr_op = &inst->input_args[1];
    assert(ptr_op->kind == LIBTCG_ARG_TEMP);
    LibTcgInstruction *ptr_inst = NULL;
    for (int j = inst_index - 1; j > 0; --j) {
        LibTcgInstruction *inst = &n->list.list[j];
        if (inst->nb_oargs > 0 &&
            inst->output_args[0].temp->index == ptr_op->temp->index) {
            ptr_inst = inst;
            break;
        }
    }
    if (ptr_inst == NULL) {
        return false;
    }
    if (ptr_inst->opcode == LIBTCG_op_add_i32 ||
        ptr_inst->opcode == LIBTCG_op_add_i64) {

        LibTcgArgument *op0 = &ptr_inst->input_args[0];
        LibTcgArgument *op1 = &ptr_inst->input_args[1];
        if (op0->kind != LIBTCG_ARG_TEMP ||
            op0->temp->kind != LIBTCG_TEMP_GLOBAL ||
            op1->kind != LIBTCG_ARG_TEMP ||
            op1->temp->kind != LIBTCG_TEMP_CONST) {
            return false;
        }
        if (op0->temp->mem_offset != libtcg->bp) {
            return false;
        }
        *offset = ABS(op1->temp->val);
        return true;
    } else {
        return false;
    }
}

