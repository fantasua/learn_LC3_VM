#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "definitions.h"

// LC3是大端机器 LC3的程序文件都是大端序
// 因此 由于常见的平台都是小端机
// 因此我们要处理这个问题

// 虚拟机的寄存器
static uint16_t reg[RegisterType::TOTAL_COUNT] = {0};
// 虚拟机的内存使用16位来表示
// 因此虚拟机的最大存储空间为 2^16 * 16 bit = 64K * 2Byte = 128KByte

#define MEMORY_CAPACITY UINT16_MAX
static uint16_t memory[MEMORY_CAPACITY] = {0};
static bool running = false;

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
  } else if (reg[r] >> 15) {
    reg[RegisterType::R_COND] = ConditionBit::FL_NEG;
  } else {
    reg[RegisterType::R_COND] = ConditionBit::FL_POS;
  }
}

bool check_keyboard() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t mem_read(uint16_t addr) {
  enum { MR_KBSR = 0xfe00, MR_KBDR = 0xfe02 };
  if (addr == MR_KBSR) {
    if (check_keyboard()) {
      memory[MR_KBSR] = (1 << 15);
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[addr];
}
void mem_write(uint16_t addr, uint16_t val) { memory[addr] = val; }
}  // namespace Utils


namespace TrapRoutine {
void getc() {
  reg[RegisterType::R_R0] = (uint16_t)getchar();
  Utils::update_flags(RegisterType::R_R0);
}

void out() { putc((char)reg[RegisterType::R_R0], stdout); }

void puts() {
  // 一个内存位置(16bit)中只保存一个字符
  uint16_t *c = memory + reg[RegisterType::R_R0];
  while (*c) {
    putc((char)*c, stdout);
    ++c;
  }
  fflush(stdout);
}

void in() {
  printf("enter a character\n");
  char c = getchar();
  putc(c, stdout);
  fflush(stdout);
  reg[RegisterType::R_R0] = (uint16_t)c;
  Utils::update_flags(RegisterType::R_R0);
}

void putsp() {
  // 一个内存位置两个字符
  /*
  15 14 13 12 ... 1 0
  */
  uint16_t *c = memory + reg[RegisterType::R_R0];
  while (*c) {
    char c1 = (*c) & 0xff;
    putc(c1, stdout);
    char c2 = (*c) >> 8;
    if (c2) {
      putchar(c2);
    }
    ++c;
  }
  fflush(stdout);
}

void halt() {
  printf("HALT\n");
  running = false;
}
}  // namespace TrapRoutine

void BR(uint16_t instruction) {
  uint16_t pc_offset9 = Utils::sign_extend(instruction & 0x1ff, 9);
  uint16_t cond = (instruction >> 9) & 0x07;
  if (cond & reg[RegisterType::R_COND]) {
    reg[RegisterType::R_PC] += pc_offset9;
  }
}

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
  Utils::mem_write(reg[RegisterType::R_PC] + pc_offset9, reg[r0]);
}

void JSR(uint16_t instruction) {
  reg[RegisterType::R_R7] = reg[RegisterType::R_PC];

  uint16_t cond = (instruction >> 11) & 0x01;
  if (cond) {
    uint16_t pc_offset11 = Utils::sign_extend(instruction & 0x7ff, 11);
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
  // reg[RegisterType::R_R7] = reg[RegisterType::R_PC];
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

uint16_t swap16(uint16_t x) { return (x << 8) | (x >> 8); }

bool load_file(const char *file) {
  FILE *fp = fopen(file, "rb");
  if (!fp) {
    return false;
  }
  uint16_t origin = 0;
  fread(&origin, sizeof(origin), 1, fp);
  uint16_t max_read = MEMORY_CAPACITY - origin;
  uint16_t *p = memory + origin;
  size_t read_cnt = fread(p, sizeof(uint16_t), max_read, fp);
  while (read_cnt != 0) {
    *p = swap16(*p);
    ++p;
    --read_cnt;
  }
  return true;
}

struct termios original_tio;

void disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal) {
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

int main(int argc, const char *argv[]) {
  // 初始化参数
  if (argc < 2) {
    fprintf(stderr, "usage: %s <image-file1> <image-file2> ...\n", argv[0]);
    return 0;
  }
  for (int j = 1; j < argc; ++j) {
    if (!load_file(argv[j])) {
      fprintf(stderr, "fail to load file %s\n", argv[j]);
      return 0;
    }
  }

  // 程序配置
  signal(SIGINT, handle_interrupt);
  disable_input_buffering();

  reg[RegisterType::R_COND] = ConditionBit::FL_ZRO;
  const uint16_t PC_START = 0x3000;
  reg[RegisterType::R_PC] = PC_START;

  running = true;
  while (running) {
    // 取指令
    uint16_t instruction = Ops::Utils::mem_read(reg[RegisterType::R_PC]++);
    uint16_t op = instruction >> 12;
    // 执行指令
    switch (op) {
      case OpCode::OP_ADD:
        Ops::ADD(instruction);
        break;
      case OpCode::OP_AND:
        Ops::AND(instruction);
        break;
      case OpCode::OP_BR:
        Ops::BR(instruction);
        break;
      case OpCode::OP_JMP:
        Ops::JMP(instruction);
        break;
      case OpCode::OP_JSR:
        Ops::JSR(instruction);
        break;
      case OpCode::OP_LD:
        Ops::LD(instruction);
        break;
      case OpCode::OP_LDI:
        Ops::LDI(instruction);
        break;
      case OpCode ::OP_LDR:
        Ops::LDR(instruction);
        break;
      case OpCode::OP_LEA:
        Ops::LEA(instruction);
        break;
      case OpCode ::OP_NOT:
        Ops::NOT(instruction);
        break;
      case OpCode::OP_RES:
        Ops::RES(instruction);
        break;
      case OpCode::OP_RTI:
        Ops::RTI(instruction);
        break;
      case OpCode ::OP_ST:
        Ops::ST(instruction);
        break;
      case OpCode::OP_STI:
        Ops::STI(instruction);
        break;
      case OpCode::OP_STR:
        Ops::STR(instruction);
        break;
      case OpCode::OP_TRAP:
        Ops::TRAP(instruction);
        break;
      default:
        //错误指令
        abort();
        break;
    }
  }
  return 0;
}