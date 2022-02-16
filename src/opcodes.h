#pragma once
#include <stdint.h>

enum OpCode {
  OP_BR = 0,  // branch
  OP_ADD,     // add
  OP_LD,      // load
  OP_ST,      // store
  OP_JSR,     // jump register
  OP_AND,     // bitwise and
  OP_LDR,     // load register
  OP_STR,     // store register
  OP_RTI,     // unused
  OP_NOT,     // bitwise not
  OP_LDI,     // load indirect
  OP_STI,     // store indirect
  OP_JMP,     // jump
  OP_RES,     // reserved, unused right now
  OP_LEA,     // load effective address
  OP_TRAP,    // excute trap
  TOTAL_COUNT
};

enum ConditionBit {
  FL_POS = 1 << 0,  // P, positive
  FL_ZRO = 1 << 1,  // Z, zero
  FL_NEG = 1 << 2,  // N, negative
};

