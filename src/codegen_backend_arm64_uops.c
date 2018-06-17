#ifdef __aarch64__

#include "ibm.h"
#include "codegen.h"
#include "codegen_backend.h"
#include "codegen_backend_arm64_defs.h"
#include "codegen_ir_defs.h"

static inline void codegen_addlong(codeblock_t *block, uint32_t val)
{
        *(uint32_t *)&block->data[block_pos] = val;
        block_pos += 4;
        if (block_pos >= BLOCK_MAX)
        {
                fatal("codegen_addlong over! %i\n", block_pos);
                CPU_BLOCK_END();
        }
}

#define Rt(x)   (x)
#define Rd(x)   (x)
#define Rn(x)  ((x) << 5)
#define Rt2(x) ((x) << 10)

#define DATA_OFFSET_UP   (1 << 23)
#define DATA_OFFSET_DOWN (0 << 23)

#define COND_SHIFT 28
#define COND_NE (0x1 << COND_SHIFT)
#define COND_AL (0xe << COND_SHIFT)

#define OPCODE_SHIFT 24
#define OPCODE_CBNZ          (0xb5 << OPCODE_SHIFT)
#define OPCODE_LDR_LITERAL_W (0x18 << OPCODE_SHIFT)
#define OPCODE_LDR_LITERAL_X (0x58 << OPCODE_SHIFT)

#define OPCODE_MOVK_W        (0x0e5 << 23)
#define OPCODE_MOVZ_W        (0x0a5 << 23)

#define OPCODE_LDP_POSTIDX_X (0x2a3 << 22)
#define OPCODE_STP_PREIDX_X  (0x2a6 << 22)
#define OPCODE_STR_IMM_W     (0x2e4 << 22)
#define OPCODE_STRB_IMM      (0x0e4 << 22)

#define OPCODE_BLR           (0xd63f0000)
#define OPCODE_NOP           (0xd503201f)
#define OPCODE_RET           (0xd65f0000)

#define MOV_WIDE_HW(hw) (hw << 21)

#define IMM7_X(imm_data)  (((imm_data >> 3) & 0x7f) << 15)
#define IMM16(imm_data) (imm_data << 5)

#define OFFSET19(offset) (((offset >> 2) << 5) & 0x00ffffe0)

#define OFFSET12_B(offset)    (offset << 10)
#define OFFSET12_W(offset) ((offset >> 2) << 10)

static int literal_offset = 0;
void codegen_reset_literal_pool(codeblock_t *block)
{
	literal_offset = 0;
}

/*Returns true if offset fits into 19 bits*/
static int offset_is_19bit(int offset)
{
	if (offset >= (1 << (18+2)))
		return 0;
	if (offset < -(1 << (18+2)))
		return 0;
	return 1;
}

#define in_range7_x(offset) (((offset) >= -0x200) && ((offset) < (0x200)) && !((offset) & 7))
#define in_range12_b(offset) (((offset) >= 0) && ((offset) < 0x1000))
#define in_range12_w(offset) (((offset) >= 0) && ((offset) < 0x4000) && !((offset) & 3))

int add_literal(codeblock_t *block, uint32_t data)
{
	if (literal_offset >= 4096)
		fatal("add_literal - literal pool full\n");

	*(uint32_t *)&block->data[ARM_LITERAL_POOL_OFFSET + literal_offset] = data;

	literal_offset += 4;

	return literal_offset - 4;
}

int add_literal_q(codeblock_t *block, uint64_t data)
{
	if (literal_offset & 4)
		literal_offset += 4;

	if (literal_offset >= 4096-4)
		fatal("add_literal - literal pool full\n");

	*(uint64_t *)&block->data[ARM_LITERAL_POOL_OFFSET + literal_offset] = data;

	literal_offset += 8;

	return literal_offset - 8;
}

static inline int imm_is_imm16(uint32_t imm_data)
{
	if (!(imm_data & 0xffff0000) || !(imm_data & 0x0000ffff))
		return 1;
	return 0;
}


void host_arm64_BLR(codeblock_t *block, int addr_reg)
{
	codegen_addlong(block, OPCODE_BLR | Rn(addr_reg));
}

void host_arm64_CBNZ(codeblock_t *block, int reg, uintptr_t dest)
{
	int offset = dest - (uintptr_t)&block->data[block_pos];
	if (!offset_is_19bit(offset))
		fatal("host_arm64_CBNZ - offset out of range %x\n", offset);
	codegen_addlong(block, OPCODE_CBNZ | OFFSET19(offset) | Rt(reg));
}

void host_arm64_LDP_POSTIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset)
{
	if (!in_range7_x(offset))
		fatal("host_arm64_LDP_POSTIDX out of range7 %i\n", offset);
	codegen_addlong(block, OPCODE_LDP_POSTIDX_X | IMM7_X(offset) | Rn(base_reg) | Rt(src_reg1) | Rt2(src_reg2));
}

void host_arm64_LDR_LITERAL_W(codeblock_t *block, int dest_reg, int literal_offset)
{
	int offset = (ARM_LITERAL_POOL_OFFSET + literal_offset) - block_pos;

	if (!offset_is_19bit(offset))
		fatal("host_arm64_CBNZ - offset out of range %x\n", offset);

	codegen_addlong(block, OPCODE_LDR_LITERAL_W | OFFSET19(offset) | Rt(dest_reg));
}
void host_arm64_LDR_LITERAL_X(codeblock_t *block, int dest_reg, int literal_offset)
{
	int offset = (ARM_LITERAL_POOL_OFFSET + literal_offset) - block_pos;

	if (!offset_is_19bit(offset))
		fatal("host_arm64_CBNZ - offset out of range %x\n", offset);

	codegen_addlong(block, OPCODE_LDR_LITERAL_X | OFFSET19(offset) | Rt(dest_reg));
}

void host_arm64_MOVZ_IMM(codeblock_t *block, int reg, uint32_t imm_data)
{
	int hw;

	if (!imm_is_imm16(imm_data))
		fatal("MOVZ_IMM - imm not representable %08x\n", imm_data);

	hw = (imm_data & 0xffff0000) ? 1 : 0;
	if (hw)
		imm_data >>= 16;

	codegen_addlong(block, OPCODE_MOVZ_W | MOV_WIDE_HW(hw) | IMM16(imm_data) | Rd(reg));
}

void host_arm64_MOVK_IMM(codeblock_t *block, int reg, uint32_t imm_data)
{
	int hw;

	if (!imm_is_imm16(imm_data))
		fatal("MOVK_IMM - imm not representable %08x\n", imm_data);

	hw = (imm_data & 0xffff0000) ? 1 : 0;
	if (hw)
		imm_data >>= 16;

	codegen_addlong(block, OPCODE_MOVK_W | MOV_WIDE_HW(hw) | IMM16(imm_data) | Rd(reg));
}

void host_arm64_NOP(codeblock_t *block)
{
	codegen_addlong(block, OPCODE_NOP);
}

void host_arm64_RET(codeblock_t *block, int reg)
{
	codegen_addlong(block, OPCODE_RET | Rn(reg));
}

void host_arm64_STP_PREIDX_X(codeblock_t *block, int src_reg1, int src_reg2, int base_reg, int offset)
{
	if (!in_range7_x(offset))
		fatal("host_arm64_STP_PREIDX out of range7 %i\n", offset);
	codegen_addlong(block, OPCODE_STP_PREIDX_X | IMM7_X(offset) | Rn(base_reg) | Rt(src_reg1) | Rt2(src_reg2));
}

void host_arm64_STR_IMM_W(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_w(offset))
		fatal("host_arm64_STR_IMM_W out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STR_IMM_W | OFFSET12_W(offset) | Rn(base_reg) | Rt(dest_reg));
}
void host_arm64_STRB_IMM(codeblock_t *block, int dest_reg, int base_reg, int offset)
{
	if (!in_range12_b(offset))
		fatal("host_arm64_STRB_IMM out of range12 %i\n", offset);
	codegen_addlong(block, OPCODE_STRB_IMM | OFFSET12_B(offset) | Rn(base_reg) | Rt(dest_reg));
}


void host_arm64_call(codeblock_t *block, void *dst_addr)
{
	int offset = add_literal_q(block, (uintptr_t)dst_addr);
	host_arm64_LDR_LITERAL_X(block, REG_X16, offset);
	host_arm64_BLR(block, REG_X16);
}

void host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data)
{
	if (imm_is_imm16(imm_data))
		host_arm64_MOVZ_IMM(block, reg, imm_data);
	else
	{
		host_arm64_MOVZ_IMM(block, reg, imm_data & 0xffff);
		host_arm64_MOVK_IMM(block, reg, imm_data & 0xffff0000);
	}
}


static int codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
	host_arm64_call(block, uop->p);
	host_arm64_CBNZ(block, REG_X0, (uintptr_t)&block->data[BLOCK_EXIT_OFFSET]);

        return 0;
}

static int codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_ARG0, uop->imm_data);

        return 0;
}
static int codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_ARG1, uop->imm_data);

        return 0;
}
static int codegen_LOAD_FUNC_ARG2_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_ARG2, uop->imm_data);

        return 0;
}
static int codegen_LOAD_FUNC_ARG3_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_ARG3, uop->imm_data);

        return 0;
}

static int codegen_STORE_PTR_IMM(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_W16, uop->imm_data);

	if (in_range12_w((uintptr_t)uop->p - (uintptr_t)&cpu_state))
		host_arm64_STR_IMM_W(block, REG_W16, REG_CPUSTATE, (uintptr_t)uop->p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_STORE_PTR_IMM - not in range\n");

        return 0;
}
static int codegen_STORE_PTR_IMM_8(codeblock_t *block, uop_t *uop)
{
	host_arm64_mov_imm(block, REG_W16, uop->imm_data);

	if (in_range12_b((uintptr_t)uop->p - (uintptr_t)&cpu_state))
		host_arm64_STRB_IMM(block, REG_W16, REG_CPUSTATE, (uintptr_t)uop->p - (uintptr_t)&cpu_state);
	else
		fatal("codegen_STORE_PTR_IMM - not in range\n");

        return 0;
}

const uOpFn uop_handlers[UOP_MAX] =
{
        [UOP_CALL_INSTRUCTION_FUNC & UOP_MASK] = codegen_CALL_INSTRUCTION_FUNC,

        [UOP_LOAD_FUNC_ARG_0_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG0_IMM,
        [UOP_LOAD_FUNC_ARG_1_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG1_IMM,
        [UOP_LOAD_FUNC_ARG_2_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG2_IMM,
        [UOP_LOAD_FUNC_ARG_3_IMM & UOP_MASK] = codegen_LOAD_FUNC_ARG3_IMM,

        [UOP_STORE_P_IMM & UOP_MASK] = codegen_STORE_PTR_IMM,
        [UOP_STORE_P_IMM_8 & UOP_MASK] = codegen_STORE_PTR_IMM_8
};

#endif
