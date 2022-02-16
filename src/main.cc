#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "definitions.h"

// 虚拟机的寄存器
static uint16_t reg[RegisterType::TOTAL_COUNT] = {0};
// 虚拟机的内存使用16位来表示
// 因此虚拟机的最大存储空间为 2^16 * 16 bit = 64K * 2Byte = 128KByte

#define MEMORY_CAPACITY UINT16_MAX
static uint16_t memory[MEMORY_CAPACITY] = {0};

// operations
namespace Ops {
namespace Utils {
// 进行有符号数字bit扩展
// x为要进行扩展的数字 bit_count为此数字的bit位数
uint16_t sign_extend(uint16_t x, int bit_count) {
  if ((x >> bit_count - 1) & 1) {
    x |= (0xFFFF << bit_count);
  }
  return x;
}
// 将寄存器数字的正/负标记写入R_COND寄存器
void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[RegisterType::R_COND] = ConditionBit::FL_ZRO;
  } else if (reg[r] > 0) {
    reg[RegisterType::R_COND] = ConditionBit::FL_POS;
  } else {
    reg[RegisterType::R_COND] = ConditionBit::FL_NEG;
  }
}
uint16_t mem_read(uint16_t addr) {}
void mem_write(uint16_t addr, uint16_t val) {}
}  // namespace Utils

namespace TrapRoutine {
void getc() {
  reg[RegisterType::R_R0] = (uint16_t)getchar();
  Utils::update_flags(RegisterType::R_R0);
}

void out() { putchar((char)reg[RegisterType::R_R0]); }

void puts() {
  // 一个内存位置(16bit)中只保存一个字符
  uint16_t *c = memory + reg[RegisterType::R_R0];
  while (*c != 0x00) {
    putchar(*(c++));
  }
}

void in() {
  printf("enter a character\n");
  char c = getchar();
  putchar(c);
  reg[RegisterType::R_R0] = (uint16_t)c;
  Utils::update_flags[RegisterType::R_R0];
}

void putsp() {
  // 一个内存位置两个字符
}

void halt() {}
}  // namespace TrapRoutine

/* ADD操作处理的指令格式如下

其区别在于bit5的内容是否为1
为0则表示寄存器模式,操作的都是寄存器
为1则表示立即模式,第三个操作数是一个数字而非寄存器

模式1 ADD R2 R0 R1 ; 将R0 R1中的内容相加,结果放到R2
+----+----+----+----+----+----+---+---+----+---+---+---+---+---+-----+---+
| 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7  | 6 | 5 | 4 | 3 | 2 | 1   | 0 |
+====+====+====+====+====+====+===+===+====+===+===+===+===+===+=====+===+
| 0  | 0  | 0  | 1  |      DR     |     SR1    | 0 | 0 | 0 |     SR2     |
+----+----+----+----+----+----+---+---+----+---+---+---+---+---+-----+---+

模式2 ADD R0 R0 1 ; 将R0中的结果与操作数1相加,结果重新放回R0
+----+----+----+----+----+----+---+---+-----+----+---+---+---+------+---+---+
| 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7   | 6  | 5 | 4 | 3 | 2    | 1 | 0 |
+====+====+====+====+====+====+===+===+=====+====+===+===+===+======+===+===+
| 0  | 0  | 0  | 1  |      DR     |     SR1      | 1 |         imm5         |
+----+----+----+----+----+----+---+---+-----+----+---+---+---+------+---+---+
可以看到 第二种模式 操作数只能用5个bit来表示
*/
void ADD(uint16_t instruction) {
  // 输入的是16bit的二进制指令码
  uint16_t r0 = (instruction >> 9) & 0x07;  // 目标寄存器
  uint16_t r1 = (instruction >> 6) & 0x06;  // 原数据寄存器
  uint16_t imm_flag = (instruction >> 5) & 0x01;  // 寄存器模式(0) 立即模式(1)

  if (imm_flag) {
    // 立即模式
    uint16_t imm5 = Utils::sign_extend(instruction & 0x1F, 5);
    reg[r0] = reg[r1] + imm5;
  } else {
    // 寄存器模式
    uint16_t r2 = instruction & 0x07;
    reg[r0] = reg[r1] + reg[r2];
  }
  Utils::update_flags(r0);
}

void LD(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  reg[r0] = Utils::mem_read(reg[RegisterType::R_PC] + pc_offset9);
  Utils::update_flags(r0);
}

void ST(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  Utils::mem_write(reg[RegisterType::R_PC + pc_offset9], reg[r0]);
}

void JSR(uint16_t instruction) {
  reg[RegisterType::R_R7] = reg[RegisterType::R_PC];

  uint16_t cond = (instruction >> 11) & 0x01;
  if (cond) {
    uint16_t pc_offset11 = instruction & 0x7ff;
    reg[RegisterType::R_PC] += pc_offset11;
  } else {
    uint16_t baseR = (instruction >> 6) & 0x07;
    reg[RegisterType::R_PC] = reg[baseR];
  }
}

void AND(uint16_t instruction) {
  // 输入的是16bit的二进制指令码
  uint16_t r0 = (instruction >> 9) & 0x07;  // 目标寄存器
  uint16_t r1 = (instruction >> 6) & 0x06;  // 原数据寄存器
  uint16_t imm_flag = (instruction >> 5) & 0x01;  // 寄存器模式(0) 立即模式(1)

  if (imm_flag) {
    // 立即模式
    uint16_t imm5 = Utils::sign_extend(instruction & 0x1F, 5);
    reg[r0] = reg[r1] & imm5;
  } else {
    // 寄存器模式
    uint16_t r2 = instruction & 0x07;
    reg[r0] = reg[r1] & reg[r2];
  }
  Utils::update_flags(r0);
}

void LDR(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;  // 目标寄存器
  uint16_t r1 = (instruction >> 6) & 0x07;  // 基地址寄存器
  uint16_t offset6 = Utils::sign_extend(instruction & 0x3f, 6);  // 偏移值
  reg[r0] = Utils::mem_read(reg[r1] + offset6);
  Utils::update_flags(r0);
}

void STR(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;  // 目标寄存器
  uint16_t r1 = (instruction >> 6) & 0x07;  // 基地址寄存器
  uint16_t offset6 = Utils::sign_extend(instruction & 0x3f, 6);  // 偏移值
  Utils::mem_write(reg[r1] + offset6 ,reg[r0]);
}

void RTI(uint16_t instruction) { abort(); }

void NOT(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t r1 = (instruction >> 6) & 0x07;
  reg[r0] = ~reg[r1];
  Utils::update_flags(r0);
}

/*
LDI(load indirect)操作指令格式如下
+----+----+----+----+----+----+---+---+---+---+---+---+---+---+---+---+
| 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
+====+====+====+====+====+====+===+===+===+===+===+===+===+===+===+===+
| 1  | 0  | 1  | 0  |      DR     |   PCoffset9                       |
+----+----+----+----+----+----+---+---+---+---+---+---+---+---+---+---+
LDI R0 123 ; 将立即数123加载到寄存器r0中
*/
void LDI(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  reg[r0] =
      Utils::mem_read(Utils::mem_read(reg[RegisterType::R_PC] + pc_offset9));
  Utils::update_flags(r0);
}

void STI(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  Utils::mem_write(Utils::mem_read(reg[RegisterType::R_PC] + pc_offset9),
                   reg[r0]);
}

void JMP(uint16_t instruction) {
  uint16_t r0 = (instruction >> 6) & 0x07;
  reg[RegisterType::R_PC] = reg[r0];
}

void RES(uint16_t instruction) { abort(); }

void LEA(uint16_t instruction) {
  uint16_t r0 = (instruction >> 9) & 0x07;
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  reg[r0] = reg[RegisterType::R_PC] + pc_offset9;
  Utils::update_flags(r0);
}

void TRAP(uint16_t instruction) {
  reg[RegisterType::R_R7] = reg[RegisterType::R_PC];
  uint16_t trap_vec8 = instruction & 0xff;
  switch (trap_vec8) {
    case TrapCode::TRAP_GETC:
      TrapRoutine::getc();
      break;
    case TrapCode::TRAP_OUT:
      TrapRoutine::out();
      break;
    case TrapCode::TRAP_PUTS:
      TrapRoutine::puts();
      break;
    case TrapCode::TRAP_IN:
      TrapRoutine::in();
      break;
    case TrapCode::TRAP_PUTSP:
      TrapRoutine::putsp();
      break;
    case TrapCode::TRAP_HALT:
      TrapRoutine::halt();
      break;
    default:
      break;
  }
}

};  // namespace Ops