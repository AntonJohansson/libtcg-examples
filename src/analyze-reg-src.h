#pragma once

#include "common.h"

void find_sources(LibTcgInterface *libtcg,
                  TbNode *root,
                  uint64_t address,
                  uint64_t offset,
                  uint64_t arg_index);
