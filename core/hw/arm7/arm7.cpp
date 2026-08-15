#include "arm7.h"
#include "arm_mem.h"
#include <cstring>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define arm_printf(...) DEBUG_LOG(AICA_ARM, __VA_ARGS__)

#define CPUReadMemoryQuick(addr) (*(u32*)&aica_ram[addr&ARAM_MASK])
#define CPUReadByte arm_ReadMem8
#define CPUReadMemory arm_ReadMem32
#define CPUReadHalfWord arm_ReadMem16
#define CPUReadHalfWordSigned(addr) ((s16)arm_ReadMem16(addr))

#define CPUWriteMemory arm_WriteMem32
#define CPUWriteHalfWord arm_WriteMem16
#define CPUWriteByte arm_WriteMem8

#define reg arm_Reg
#define armNextPC reg[R15_ARM_NEXT].I

#define CPUUpdateTicksAccesint(a) 1
#define CPUUpdateTicksAccessSeq32(a) 1
#define CPUUpdateTicksAccesshort(a) 1
#define CPUUpdateTicksAccess32(a) 1
#define CPUUpdateTicksAccess16(a) 1

#define ARM_CYCLES_PER_SAMPLE 256

DECL_ALIGN(8) reg_pair arm_Reg[RN_ARM_REG_COUNT];

void CPUSwap(u32 *a, u32 *b)
{
	u32 c = *b;
	*b = *a;
	*a = c;
}

#define N_FLAG (reg[RN_PSR_FLAGS].FLG.N)
#define Z_FLAG (reg[RN_PSR_FLAGS].FLG.Z)
#define C_FLAG (reg[RN_PSR_FLAGS].FLG.C)
#define V_FLAG (reg[RN_PSR_FLAGS].FLG.V)

bool armIrqEnable;
bool armFiqEnable;
int armMode;

bool Arm7Enabled=false;

u8 cpuBitsSet[256];

void CPUSwitchMode(int mode, bool saveState, bool breakLoop=true);
extern "C" void CPUFiq();
void CPUUpdateCPSR();
void CPUUpdateFlags();
void CPUSoftwareInterrupt(int comment);
void CPUUndefinedException();
void libAICA_TimeStep();

#if FEAT_AREC == DYNAREC_NONE

void arm_Run_(u32 CycleCount)
{
	if (!Arm7Enabled)
		return;

	u32 clockTicks=0;
	while (clockTicks<CycleCount)
	{
		if (reg[INTR_PEND].I)
		{
			CPUFiq();
		}

		reg[15].I = armNextPC + 8;
		#include "arm-new.h"
	}
}

void aicaarm::run(u32 samples)
{
	for (u32 i = 0; i < samples; i++)
	{
		arm_Run_(ARM_CYCLES_PER_SAMPLE);
		libAICA_TimeStep();
	}
}
#endif

void armt_init();

void aicaarm::init()
{
#if FEAT_AREC != DYNAREC_NONE
	armt_init();
#endif
   aicaarm::reset();

	for (int i = 0; i < 256; i++)
	{
		int count = 0;
		for (int j = 0; j < 8; j++)
			if (i & (1 << j))
				count++;

		cpuBitsSet[i] = count;
	}
}

void CPUSwitchMode(int mode, bool saveState, bool breakLoop)
{
	CPUUpdateCPSR();

	switch(armMode)
	{
	case 0x10:
	case 0x1F:
		reg[R13_USR].I = reg[13].I;
		reg[R14_USR].I = reg[14].I;
		reg[17].I = reg[16].I;
		break;
	case 0x11:
		CPUSwap(&reg[R8_FIQ].I, &reg[8].I);
		CPUSwap(&reg[R9_FIQ].I, &reg[9].I);
		CPUSwap(&reg[R10_FIQ].I, &reg[10].I);
		CPUSwap(&reg[R11_FIQ].I, &reg[11].I);
		CPUSwap(&reg[R12_FIQ].I, &reg[12].I);
		reg[R13_FIQ].I = reg[13].I;
		reg[R14_FIQ].I = reg[14].I;
		reg[SPSR_FIQ].I = reg[17].I;
		break;
	case 0x12:
		reg[R13_IRQ].I  = reg[13].I;
		reg[R14_IRQ].I  = reg[14].I;
		reg[SPSR_IRQ].I =  reg[17].I;
		break;
	case 0x13:
		reg[R13_SVC].I  = reg[13].I;
		reg[R14_SVC].I  = reg[14].I;
		reg[SPSR_SVC].I =  reg[17].I;
		break;
	case 0x17:
		reg[R13_ABT].I  = reg[13].I;
		reg[R14_ABT].I  = reg[14].I;
		reg[SPSR_ABT].I =  reg[17].I;
		break;
	case 0x1b:
		reg[R13_UND].I  = reg[13].I;
		reg[R14_UND].I  = reg[14].I;
		reg[SPSR_UND].I =  reg[17].I;
		break;
	}

	u32 CPSR = reg[16].I;
	u32 SPSR = reg[17].I;

	switch(mode)
	{
	case 0x10:
	case 0x1F:
		reg[13].I = reg[R13_USR].I;
		reg[14].I = reg[R14_USR].I;
		reg[16].I = SPSR;
		break;
	case 0x11:
		CPUSwap(&reg[8].I, &reg[R8_FIQ].I);
		CPUSwap(&reg[9].I, &reg[R9_FIQ].I);
		CPUSwap(&reg[10].I, &reg[R10_FIQ].I);
		CPUSwap(&reg[11].I, &reg[R11_FIQ].I);
		CPUSwap(&reg[12].I, &reg[R12_FIQ].I);
		reg[13].I = reg[R13_FIQ].I;
		reg[14].I = reg[R14_FIQ].I;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_FIQ].I;
		break;
	case 0x12:
		reg[13].I = reg[R13_IRQ].I;
		reg[14].I = reg[R14_IRQ].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_IRQ].I;
		break;
	case 0x13:
		reg[13].I = reg[R13_SVC].I;
		reg[14].I = reg[R14_SVC].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_SVC].I;
		break;
	case 0x17:
		reg[13].I = reg[R13_ABT].I;
		reg[14].I = reg[R14_ABT].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_ABT].I;
		break;
	case 0x1b:
		reg[13].I = reg[R13_UND].I;
		reg[14].I = reg[R14_UND].I;
		reg[16].I = SPSR;
		if(saveState)
			reg[17].I = CPSR;
		else
			reg[17].I = reg[SPSR_UND].I;
		break;
	default:
		ERROR_LOG(AICA_ARM, "Unsupported ARM mode %02x", mode);
		die("Arm error..");
		break;
	}
	armMode = mode;
	CPUUpdateFlags();
	CPUUpdateCPSR();
}

void CPUUpdateCPSR()
{
	reg_pair CPSR;

	CPSR.I = reg[RN_CPSR].I & 0x40;
	CPSR.PSR.NZCV=reg[RN_PSR_FLAGS].FLG.NZCV;

	if (!armFiqEnable)
		CPSR.I |= 0x40;
	if(!armIrqEnable)
		CPSR.I |= 0x80;

	CPSR.PSR.M=armMode;
	
	reg[16].I = CPSR.I;
}

void CPUUpdateFlags()
{
	u32 CPSR = reg[16].I;
	reg[RN_PSR_FLAGS].FLG.NZCV=reg[16].PSR.NZCV;
	armIrqEnable = (CPSR & 0x80) ? false : true;
	armFiqEnable = (CPSR & 0x40) ? false : true;
	update_armintc();
}

void CPUSoftwareInterrupt(int comment)
{
	u32 PC = reg[R15_ARM_NEXT].I+4;
	CPUSwitchMode(0x13, true, false);
	reg[14].I = PC;
	
	armIrqEnable = false;
	armNextPC = 0x08;
}

void CPUUndefinedException()
{
	WARN_LOG(AICA_ARM, "arm7: CPUUndefinedException(). SOMETHING WENT WRONG");
	u32 PC = reg[R15_ARM_NEXT].I+4;
	CPUSwitchMode(0x1b, true, false);
	reg[14].I = PC;
	armIrqEnable = false;
	armNextPC = 0x04;
}

void FlushCache();

void aicaarm::reset()
{
   DEBUG_LOG(AICA_ARM, "AICA ARM Reset");
#if FEAT_AREC != DYNAREC_NONE
	FlushCache();
#endif
	aica_interr = false;
	aica_reg_L = 0;
	e68k_out = false;
	e68k_reg_L = 0;
	e68k_reg_M = 0;

	Arm7Enabled = false;
	memset(&arm_Reg[0], 0, sizeof(arm_Reg));

	armMode = 0x13;

	reg[13].I = 0x03007F00;
	reg[15].I = 0x0000000;
	reg[16].I = 0x00000000;
	reg[R13_IRQ].I = 0x03007FA0;
	reg[R13_SVC].I = 0x03007FE0;
	armIrqEnable = true;      
	armFiqEnable = false;
	update_armintc();

	C_FLAG = V_FLAG = N_FLAG = Z_FLAG = false;

	reg[16].I |= 0x40;
	CPUUpdateCPSR();

	armNextPC = reg[15].I;
	reg[15].I += 4;
}

extern "C"
NOINLINE
void CPUFiq()
{
	u32 PC = reg[R15_ARM_NEXT].I+4;
	CPUSwitchMode(0x11, true, false);
	reg[14].I = PC;
	armIrqEnable = false;
	armFiqEnable = false;
	update_armintc();

	armNextPC = 0x1c;
}

#include "hw/sh4/sh4_core.h"

void aicaarm::enable(bool enabled)
{
	if(!Arm7Enabled && enabled)
      aicaarm::reset();
	
	Arm7Enabled=enabled;
}

void update_armintc()
{
	reg[INTR_PEND].I=e68k_out && armFiqEnable;
}

#if FEAT_AREC != DYNAREC_NONE

extern "C" void CompileCode();

u32 DYNACALL arm_single_op(u32 opcode)
{
	u32 clockTicks=0;

#define NO_OPCODE_READ
#include "arm-new.h"

	return clockTicks;
}

struct ArmDPOP
{
	u32 key;
	u32 mask;
	u32 flags;
};

std::vector<ArmDPOP> ops;

enum OpFlags
{
	OP_SETS_PC         = 1,
	OP_READS_PC        = 32768,
	OP_IS_COND         = 65536,
	OP_MFB             = 0x80000000,

	OP_HAS_RD_12       = 2,
	OP_HAS_RD_16       = 4,
	OP_HAS_RS_0        = 8,
	OP_HAS_RS_8        = 16,
	OP_HAS_RS_16       = 32,
	OP_HAS_FLAGS_READ  = 4096,
	OP_HAS_FLAGS_WRITE = 8192,
	OP_HAS_RD_READ     = 16384,

	OP_WRITE_FLAGS     = 64,
	OP_WRITE_FLAGS_S   = 128,
	OP_READ_FLAGS      = 256,
	OP_READ_FLAGS_S    = 512,
	OP_WRITE_REG       = 1024,
	OP_READ_REG_1      = 2048,
};

#define DP_R_ROFC (OP_READ_FLAGS_S|OP_READ_REG_1)
#define DP_R_ROF (OP_READ_FLAGS|OP_READ_REG_1)
#define DP_R_OFC (OP_READ_FLAGS_S)

#define DP_W_RFC (OP_WRITE_FLAGS_S|OP_WRITE_REG)
#define DP_W_F (OP_WRITE_FLAGS)

void AddDPOP(u32 subcd, u32 rflags, u32 wflags)
{
	ArmDPOP op;

	u32 key=subcd<<21;
	u32 mask=(15<<21) | (7<<25);

	op.flags=rflags|wflags;
	
	if (wflags==DP_W_F)
	{
		mask|=1<<20;
		key|=1<<20;
	}

	op.key=key;
	op.mask=mask | (1<<4);
	ops.push_back(op);

	op.key =  key  | (1<<4);
	op.mask = mask | (1<<4) | (1<<7);
	ops.push_back(op);

	op.key =  key  | (1<<25);
	op.mask = mask;
	ops.push_back(op);
}

void InitHash()
{
	AddDPOP(0,DP_R_ROFC, DP_W_RFC);
	AddDPOP(1,DP_R_ROFC, DP_W_RFC);
	AddDPOP(2,DP_R_ROFC, DP_W_RFC);
	AddDPOP(3,DP_R_ROFC, DP_W_RFC);
	AddDPOP(4,DP_R_ROFC, DP_W_RFC);
	AddDPOP(12,DP_R_ROFC, DP_W_RFC);
	AddDPOP(14,DP_R_ROFC, DP_W_RFC);
	
	AddDPOP(5,DP_R_ROF, DP_W_RFC);
	AddDPOP(6,DP_R_ROF, DP_W_RFC);
	AddDPOP(7,DP_R_ROF, DP_W_RFC);

	AddDPOP(8,DP_R_ROF, DP_W_F);
	AddDPOP(9,DP_R_ROF, DP_W_F);

	AddDPOP(10,DP_R_ROF, DP_W_F);
	AddDPOP(11,DP_R_ROF, DP_W_F);
	
	AddDPOP(13,DP_R_OFC, DP_W_RFC);
	AddDPOP(15,DP_R_OFC, DP_W_RFC);
}

void  armEmit32(u32 emit32);
void *armGetEmitPtr();

#define _DEVEL          (1)
#define EMIT_I          armEmit32((I))
#define EMIT_GET_PTR()  armGetEmitPtr()
u8* icPtr;
u8* ICache;

extern const u32 ICacheSize=1024*1024;
#ifdef _WIN32
u8 ARM7_TCB[ICacheSize+4096];
#elif defined(__linux__) || defined(HAVE_LIBNX)
u8 ARM7_TCB[ICacheSize+4096] __attribute__((section(".text")));
#elif defined(__APPLE__)
u8 ARM7_TCB[ICacheSize+4096] __attribute__((section("__TEXT, .text")));
#else
#error ARM7_TCB ALLOC
#endif

#include "rec-ARM/arm_emitter.h"
#undef I

using namespace ARM;

void* EntryPoints[ARAM_SIZE_MAX / 4];

enum OpType
{
	VOT_Fallback,
	VOT_DataOp,
	VOT_B,
	VOT_BL,
	VOT_BR,
	VOT_Read,
	VOT_MRS,
	VOT_MSR,
};

void armv_call(void* target);
void armv_setup();
void armv_intpr(u32 opcd);
void armv_end(void* codestart, u32 cycles);
void armv_check_pc(u32 pc);
void armv_check_cache(u32 opcd, u32 pc);
void armv_imm_to_reg(u32 regn, u32 imm);
void armv_MOV32(eReg regn, u32 imm);
void armv_prof(OpType opt,u32 op,u32 flg);

extern "C" void arm_dispatch();
extern "C" void arm_exit();
extern "C" void DYNACALL arm_mainloop(u32 cycl, void* regs, void* entrypoints);
extern "C" void DYNACALL arm_compilecode();

template <bool Load, bool Byte>
u32 DYNACALL DoMemOp(u32 addr,u32 data)
{
	u32 rv=0;

#if HOST_CPU==CPU_X86
	addr=virt_arm_reg(0);
	data=virt_arm_reg(1);
#endif

	if (Load)
	{
		if (Byte)
			rv=arm_ReadMem8(addr);
		else
			rv=arm_ReadMem32(addr);
	}
	else
	{
		if (Byte)
			arm_WriteMem8(addr,data);
		else
			arm_WriteMem32(addr,data);
	}

	#if HOST_CPU==CPU_X86
		virt_arm_reg(0)=rv;
	#endif

	return rv;
}

#if HOST_CPU==CPU_X86 && !defined(__GNUC__)
#include <intrin.h>
u32 findfirstset(u32 v)
{
	unsigned long rv;
	_BitScanForward(&rv,v);
	return rv+1;
}
#else
#define findfirstset __builtin_ffs
#endif

void* GetMemOp(bool Load, bool Byte)
{
	if (Load)
	{
		if (Byte)
			return (void*)(u32(DYNACALL*)(u32,u32))&DoMemOp<true,true>;
		else
			return (void*)(u32(DYNACALL*)(u32,u32))&DoMemOp<true,false>;
	}
	else
	{
		if (Byte)
			return (void*)(u32(DYNACALL*)(u32,u32))&DoMemOp<false,true>;
		else
			return (void*)(u32(DYNACALL*)(u32,u32))&DoMemOp<false,false>;
	}
}

OpType DecodeOpcode(u32& opcd,u32& flags)
{
	flags=OP_READS_PC;

	u32 CC=(opcd >> 28);

	if (CC!=CC_AL)
		flags|=OP_IS_COND;

	#define CHK_BTS(M,S,V) ( (M & (opcd>>S)) == (V) )
	#define IS_LOAD (opcd & (1<<20))
	#define READ_PC_CHECK(S) if (CHK_BTS(15,S,15)) flags|=OP_READS_PC;

	bool _set_pc=
		(CHK_BTS(3,26,0) && CHK_BTS(15,12,15))             ||
		(CHK_BTS(3,26,1) && CHK_BTS(15,12,15) && IS_LOAD ) ||
		(CHK_BTS(7,25,4) && (opcd & 32768) &&  IS_LOAD)    ||
		CHK_BTS(7,25,5)                                    ||
		CHK_BTS(15,24,15);
	
	if (CC==15)
		return VOT_Fallback;

	if (_set_pc)
		flags|=OP_SETS_PC;

	if (CHK_BTS(7,25,5))
	{
		verify(_set_pc);
		if (!(flags&OP_IS_COND))
			flags&=~OP_READS_PC;

		flags|=OP_SETS_PC;
		return (opcd&(1<<24))?VOT_BL:VOT_B;
	}

	if (CHK_BTS(0xFFFFFF,4,0x1A0F00))
	{
		verify(_set_pc);
		if (CC==CC_AL)
			flags&=~OP_READS_PC;

		return VOT_BR;
	}

	if (CC!=CC_AL && _set_pc)
	{
		return VOT_Fallback;
	}

	u32 RList=opcd&0xFFFF;
	u32 Rn=(opcd>>16)&15;

#define LDM_REGCNT() (cpuBitsSet[RList & 255] + cpuBitsSet[(RList >> 8) & 255])

	for( u32 i=0;i<ops.size();i++)
	{
		if (!_set_pc && ops[i].key==(opcd&ops[i].mask))
		{
			flags &= ~OP_READS_PC;

			if ((opcd >> 28)!=0xE)
			{
				flags |= OP_HAS_FLAGS_READ;
				flags |= OP_HAS_RD_READ;
			}

			if ((ops[i].flags & OP_READ_FLAGS) ||
			   ((ops[i].flags & OP_READ_FLAGS_S) && (opcd & (1<<20))))
			{
				flags |= OP_HAS_FLAGS_READ;
			}

			if ((ops[i].flags & OP_WRITE_FLAGS) ||
			   ((ops[i].flags & OP_WRITE_FLAGS_S) && (opcd & (1<<20))))
			{
				flags |= OP_HAS_FLAGS_WRITE;
			}

			if(ops[i].flags & OP_WRITE_REG)
			{
				flags |= OP_HAS_RD_12;
				verify(! (CHK_BTS(15,12,15) && CC!=CC_AL));
			}

			if(ops[i].flags & OP_READ_REG_1)
			{
				flags |= OP_HAS_RS_16;
				READ_PC_CHECK(16);
			}

			if ( !(opcd & (1<<25)) )
			{
				flags |= OP_HAS_RS_0;
				READ_PC_CHECK(0);

				if (opcd & (1<<4))
				{
					verify(! (opcd & (1<<7)) );
					flags |= OP_HAS_RS_8;
					verify(!CHK_BTS(15,8,15));
				}
				else
				{
					if ( ((opcd>>4)&7)==6)
					{
						flags |= OP_HAS_FLAGS_READ;
					}
				}
			}

			return VOT_DataOp;
		}
	}

	if ((opcd>>25)==(0xE4/2) )
	{
		arm_printf("ARM: MEM %08X L/S:%d, AWB:%d!\n",opcd,(opcd>>20)&1,(opcd>>21)&1);
		return VOT_Read;
	}
	else if ((opcd>>25)==(0xE6/2) && CHK_BTS(0x7,4,0) )
	{
		arm_printf("ARM: MEM REG to Reg %08X\n",opcd);
		return VOT_Read;
	}
	else if ((opcd>>25)==(0xE8/2) && CHK_BTS(1,22,0) && CHK_BTS(1,20,1) && LDM_REGCNT()==1)
	{
		u32 old_opcd=opcd;
		opcd=0xE4000000;
		opcd |= 0<<25;
		opcd |= old_opcd & (1<<24);
		opcd |= old_opcd & (1<<23);
		opcd |= 0<<22;
		opcd |= old_opcd & (1<<21);
		opcd |= old_opcd & (1<<20);
		opcd |= Rn<<16;
		u32 Rd=findfirstset(RList)-1;
		opcd |= Rd<<12;
		opcd |= 4;
		arm_printf("ARM: MEM TFX R %08X\n",opcd);
		return VOT_Read;
	}
	else if ((opcd>>25)==(0xE8/2) && CHK_BTS(1,22,0) && CHK_BTS(1,20,0) && LDM_REGCNT()==1)
	{
		u32 old_opcd=opcd;
		opcd=0xE4000000;
		opcd |= 0<<25;
		opcd |= old_opcd & (1<<24);
		opcd |= old_opcd & (1<<23);
		opcd |= 0<<22;
		opcd |= old_opcd & (1<<21);
		opcd |= old_opcd & (1<<20);
		opcd |= Rn<<16;
		u32 Rd=findfirstset(RList)-1;
		opcd |= Rd<<12;
		opcd |= 4;
		arm_printf("ARM: MEM TFX W %08X\n",opcd);
		return VOT_Read;
	}
	else if (CHK_BTS(0xE10F0FFF,0,0xE10F0000))
	{
		return VOT_MRS;
	}
	else if (CHK_BTS(0xEFBFFFF0,0,0xE129F000))
	{
		return VOT_MSR;
	}
	else if ((opcd>>25)==(0xE8/2) && CHK_BTS(32768,0,0))
	{
		arm_printf("ARM: MEM FB %08X\n",opcd);
		flags|=OP_MFB;
	}
	else
	{
		arm_printf("ARM: FB %08X\n",opcd);
	}

	return VOT_Fallback;
}

#if HOST_CPU == CPU_ARM64
extern void LoadReg(eReg rd,u32 regn,ConditionCode cc=CC_AL);
extern void StoreReg(eReg rd,u32 regn,ConditionCode cc=CC_AL);
extern void armv_mov(ARM::eReg regd, ARM::eReg regn);
extern void armv_add(ARM::eReg regd, ARM::eReg regn, ARM::eReg regm);
extern void armv_sub(ARM::eReg regd, ARM::eReg regn, ARM::eReg regm);
extern void armv_add(ARM::eReg regd, ARM::eReg regn, s32 imm);
extern void armv_lsl(ARM::eReg regd, ARM::eReg regn, u32 imm);
extern void armv_bic(ARM::eReg regd, ARM::eReg regn, u32 imm);
extern void *armv_start_conditional(ARM::ConditionCode cc);
extern void armv_end_conditional(void *ref);
#define r9 ((ARM::eReg)25)
#else
void LoadReg(eReg rd,u32 regn,ConditionCode cc=CC_AL)
{
	LDR(rd,r8,(u8*)&reg[regn].I-(u8*)&reg[0].I,Offset,cc);
}
void StoreReg(eReg rd,u32 regn,ConditionCode cc=CC_AL)
{
	STR(rd,r8,(u8*)&reg[regn].I-(u8*)&reg[0].I,Offset,cc);
}
void armv_mov(ARM::eReg regd, ARM::eReg regn)
{
	MOV(regd, regn);
}

void armv_add(ARM::eReg regd, ARM::eReg regn, ARM::eReg regm)
{
	ADD(regd, regn, regm);
}

void armv_sub(ARM::eReg regd, ARM::eReg regn, ARM::eReg regm)
{
	SUB(regd, regn, regm);
}

void armv_add(ARM::eReg regd, ARM::eReg regn, s32 imm)
{
	if (imm >= 0)
		ADD(regd, regn, imm);
	else
		SUB(regd, regn, -imm);
}

void armv_lsl(ARM::eReg regd, ARM::eReg regn, u32 imm)
{
	LSL(regd, regn, imm);
}

void armv_bic(ARM::eReg regd, ARM::eReg regn, u32 imm)
{
	BIC(regd, regn, imm);
}

void *armv_start_conditional(ARM::ConditionCode cc)
{
	return NULL;
}
void armv_end_conditional(void *ref)
{
}
#endif

u32 renamed_regs[16];
u32 rename_reg_base;

void RenameRegReset()
{
	rename_reg_base=r1;
   memset(renamed_regs, 0, sizeof(renamed_regs));
}

u32 RenameReg(u32 reg, bool& didrn)
{
   if (renamed_regs[reg] == 0)
	{
		renamed_regs[reg]=rename_reg_base;
		rename_reg_base++;
		didrn=true;
	}
	else
	{
		didrn=false;
	}

	return renamed_regs[reg];
}

void LoadAndRename(u32& opcd, u32 bitpos, bool load,u32 pc)
{
	bool didrn;
	u32 reg=(opcd>>bitpos)&15;

	u32 nreg=RenameReg(reg,didrn);

	opcd = (opcd& ~(15<<bitpos)) | (nreg<<bitpos);

	if (load && didrn)
	{
		if (reg==15)
			armv_MOV32((eReg)nreg,pc);
		else
			LoadReg((eReg)nreg,reg);
	}
}

void StoreAndRename(u32 opcd, u32 bitpos)
{
	bool didrn;
	u32 reg=(opcd>>bitpos)&15;

	u32 nreg=RenameReg(reg,didrn);

	verify(!didrn);

	if (reg==15)
		reg=R15_ARM_NEXT;

	StoreReg((eReg)nreg,reg);
}

#if HOST_CPU == CPU_ARM64
extern void LoadFlags();
extern void StoreFlags();
#else
void LoadFlags()
{
	LoadReg(r0,RN_PSR_FLAGS);
	MSR(0,8,r0);
}

void StoreFlags()
{
	MRS(r1,0);
	StoreReg(r1,RN_PSR_FLAGS);
}
#endif

void VirtualizeOpcode(u32 opcd,u32 flag,u32 pc)
{
	u32 orig=opcd;

	if (flag & OP_HAS_FLAGS_READ)
	{
		LoadFlags();
	}

	bool shiftByReg = !(opcd & (1 << 25)) && (opcd & (1 << 4));
	if (flag & OP_HAS_RS_0)
		LoadAndRename(opcd, 0, true, pc + (shiftByReg ? 12 : 8));
	if (flag & OP_HAS_RS_8)
		LoadAndRename(opcd, 8, true, pc + 8);
	if (flag & OP_HAS_RS_16)
		LoadAndRename(opcd, 16, true, pc + (shiftByReg ? 12 : 8));

	if (flag & OP_HAS_RD_12)
		LoadAndRename(opcd,12,flag&OP_HAS_RD_READ,pc+4);

	if (flag & OP_HAS_RD_16)
	{
		verify(! (flag & OP_HAS_RS_16));
		LoadAndRename(opcd,16,flag&OP_HAS_RD_READ,pc+4);
	}

	arm_printf("Arm Virtual: %08X -> %08X\n",orig,opcd);
	armEmit32(opcd);

	if (flag & OP_HAS_RD_12)
		StoreAndRename(orig,12);

	if (flag & OP_HAS_RD_16)
		StoreAndRename(orig,16);

   if (renamed_regs[15] != 0)
	{
		verify(flag&OP_READS_PC || (flag&OP_SETS_PC && !(flag&OP_IS_COND)));
	}

	if (flag & OP_HAS_FLAGS_WRITE)
		StoreFlags();
}

u32 nfb,ffb,bfb,mfb;

void *armGetEmitPtr()
{
	if (icPtr < (ICache+ICacheSize-1024))
		return static_cast<void *>(icPtr);

	return NULL;
}

#if HOST_CPU == CPU_X86

#include "../../rec-x86/x86_emitter.h"
#include "virt_arm.h"

static x86_block* x86e;

void DumpRegs(const char* output)
{
	static FILE* f=fopen(output, "w");
	static int id=0;
	verify(id!=137250);
	fprintf(f,"%d\n",id);
	{
		int i=R15_ARM_NEXT;
		fprintf(f,"r%d=%08X\n",i,reg[i].I);
	}
	id++;
}

void DYNACALL PrintOp(u32 opcd)
{
	DEBUG_LOG(AICA_ARM, "%08X", opcd);
}

void armv_imm_to_reg(u32 regn, u32 imm)
{
	x86e->Emit(op_mov32,&reg[regn].I,imm);
}

void armv_MOV32(eReg regn, u32 imm)
{
	x86e->Emit(op_mov32,&virt_arm_reg(regn),imm);
}

void armv_call(void* loc)
{
	x86e->Emit(op_call,x86_ptr_imm(loc));
}

x86_Label* end_lbl;

void armv_setup()
{
	x86e = new x86_block();
	x86e->Init(0,0);
	x86e->x86_buff=(u8*)EMIT_GET_PTR();
	x86e->x86_size=1024*64;
	x86e->do_realloc=false;
	
	x86e->Emit(op_mov32,&virt_arm_reg(8),(u32)&arm_Reg[0]);
	end_lbl=x86e->CreateLabel(false,0);
}

void armv_intpr(u32 opcd)
{
	x86e->Emit(op_mov32,ECX,opcd);
	x86e->Emit(op_call,x86_ptr_imm(&arm_single_op));
}

void armv_end(void* codestart, u32 cycles)
{
	x86e->Emit(op_sub32,ESI,cycles);
	x86e->Emit(op_jns,x86_ptr_imm(arm_dispatch));
	x86e->Emit(op_jmp,x86_ptr_imm(arm_exit));

	x86e->MarkLabel(end_lbl);
	x86e->Emit(op_int3);
	x86e->Emit(op_call,x86_ptr_imm(FlushCache));
	x86e->Emit(op_sub32,ESI,cycles);
	x86e->Emit(op_jmp,x86_ptr_imm(arm_dispatch));

	x86e->Generate();
	icPtr+=x86e->x86_indx;
	delete x86e;
}

void armv_check_pc(u32 pc)
{
	x86e->Emit(op_cmp32,&armNextPC,pc);
	x86_Label* nof=x86e->CreateLabel(false,0);
	x86e->Emit(op_je,nof);
	x86e->Emit(op_int3);
	x86e->MarkLabel(nof);
}

void armv_check_cache(u32 opcd, u32 pc)
{
	x86e->Emit(op_cmp32,&CPUReadMemoryQuick(pc),opcd);
	x86_Label* nof=x86e->CreateLabel(false,0);
	x86e->Emit(op_je,nof);
	x86e->Emit(op_int3);
	x86e->MarkLabel(nof);
}

void armv_prof(OpType opt,u32 op,u32 flags)
{
	if (VOT_Fallback!=opt)
		x86e->Emit(op_add32,&nfb,1);
	else
	{
		if (flags & OP_SETS_PC)
			x86e->Emit(op_add32,&bfb,1);
		else if (flags & OP_MFB)
			x86e->Emit(op_add32,&mfb,1);
		else
			x86e->Emit(op_add32,&ffb,1);
	}
}

#ifndef _WIN32
naked void DYNACALL arm_compilecode()
{
	__asm
	{
		call CompileCode;
		mov eax,0;
		jmp arm_dispatch;
	}
}

naked void DYNACALL arm_mainloop(u32 cycl, void* regs, void* entrypoints)
{
	__asm
	{
		push esi

		mov esi,ecx
		add esi,reg[CYCL_CNT*4].I

		mov eax,0;
		jmp arm_dispatch
	}
}

naked void arm_dispatch()
{
	__asm
	{
arm_disp:
		mov eax,reg[R15_ARM_NEXT*4].I
		and eax,0x7FFFFC
		cmp reg[INTR_PEND*4].I,0
		jne arm_dofiq
		jmp [EntryPoints+eax]

arm_dofiq:
		call CPUFiq
		jmp arm_disp
	}
}

naked void arm_exit()
{
	__asm
	{
	arm_exit:
		mov reg[CYCL_CNT*4].I,esi
		pop esi
		ret
	}
}
#endif

#elif	(HOST_CPU == CPU_ARM)

#include <sys/mman.h>

void  armEmit32(u32 emit32)
{
	if (icPtr >= (ICache+ICacheSize-1024))
		die("ICache is full, invalidate old entries ...");

	*(u32*)icPtr = emit32;  
	icPtr+=4;
}

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
extern "C" void armFlushICache(void *code, void *pEnd) {
    sys_dcache_flush(code, (u8*)pEnd - (u8*)code + 1);
    sys_icache_invalidate(code, (u8*)pEnd - (u8*)code + 1);
}
#else
extern "C" void armFlushICache(void *bgn, void *end) {
	__builtin___clear_cache((char *)bgn, (char *)end);
}
#endif

void armv_imm_to_reg(u32 regn, u32 imm)
{
	MOV32(r0,imm);
	StoreReg(r0,regn);
}

void armv_call(void* loc)
{
	CALL((u32)loc);
}

void armv_setup()
{
}

void armv_intpr(u32 opcd)
{
	MOV32(r0,opcd);
	CALL((u32)arm_single_op);
	SUB(r5, r5, r0, false);
}

void armv_end(void* codestart, u32 cycl)
{
	if (is_i8r4(cycl))
		SUB(r5,r5,cycl,true);
	else
	{
		u32 togo = cycl;
		while(ARMImmid8r4_enc(togo) == -1)
		{
			SUB(r5,r5,256);
			togo -= 256;
		}
		SUB(r5,r5,togo,true);
	}
	JUMP((u32)&arm_exit,CC_MI);
	JUMP((u32)&arm_dispatch);

	armFlushICache(codestart,(void*)EMIT_GET_PTR());
}

void armv_MOV32(eReg regn, u32 imm)
{
	MOV32(regn,imm);
}

#elif HOST_CPU == CPU_ARM64
#include <sys/mman.h>
#endif

void aicaarm::run(u32 samples)
{
	for (int i = 0; i < samples; i++)
	{
		if (Arm7Enabled)
			arm_mainloop(ARM_CYCLES_PER_SAMPLE, arm_Reg, EntryPoints);
		libAICA_TimeStep();
	}
}
		
#undef r

void MemOperand2(eReg dst,bool I, bool U,u32 offs, u32 opcd)
{
	if (I==true)
	{
		u32 Rm=(opcd>>0)&15;
		verify(CHK_BTS(7,4,0));
		LoadReg(r1,Rm);
		u32 SA=31&(opcd>>7);
		if (SA)
			armv_lsl(r1, r1, SA);
	}
	else
	{
		armv_MOV32(r1,offs);
	}

	if (U)
		armv_add(dst, r0, r1);
	else
		armv_sub(dst, r0, r1);
}

template<u32 Pd>
void DYNACALL MSR_do(u32 v)
{
#if HOST_CPU==CPU_X86
	v=virt_arm_reg(r0);
#endif
	if (Pd)
	{
		if(armMode > 0x10 && armMode < 0x1f)
		{
			reg[17].I = (reg[17].I & 0x00FFFF00) | (v & 0xFF0000FF);
		}
	}
	else
	{
		CPUUpdateCPSR();
	
		u32 newValue = reg[16].I;
		if(armMode > 0x10)
		{
			newValue = (newValue & 0xFFFFFF00) | (v & 0x000000FF);
		}

		newValue = (newValue & 0x00FFFFFF) | (v & 0xFF000000);
		newValue |= 0x10;
		if(armMode > 0x10)
		{
			CPUSwitchMode(newValue & 0x1f, false);
		}
		reg[16].I = newValue;
		CPUUpdateFlags();
	}
}

extern "C" void CompileCode()
{
	void* rv=EMIT_GET_PTR();
	EntryPoints[(armNextPC & (ARAM_SIZE_MAX - 1)) / 4] = rv;

	u32 pc=armNextPC;
	armv_setup();
	u32 ops=0;
	u32 Cycles=0;

	for(;;)
	{
		ops++;
		Cycles += 6;
		u32 opcd=CPUReadMemoryQuick(pc);

#if HOST_CPU==CPU_X86
		armv_check_cache(opcd,pc);
#endif

		u32 op_flags;
		OpType opt=DecodeOpcode(opcd,op_flags);

		switch(opt)
		{
		case VOT_DataOp:
			{
				RenameRegReset();
#if HOST_CPU==CPU_X86
				armv_imm_to_reg(15,rand());
#endif
				VirtualizeOpcode(opcd,op_flags,pc);
#if HOST_CPU==CPU_X86
				armv_imm_to_reg(15,rand());
#endif
			}
			break;
		
		case VOT_BR:
			{
				ConditionCode cc=(ConditionCode)(opcd>>28);
				verify(op_flags&OP_SETS_PC);

				if (cc!=CC_AL)
				{
					LoadFlags();
					armv_imm_to_reg(R15_ARM_NEXT,pc+4);
				}

				LoadReg(r0,opcd&0xF);
#if HOST_CPU==CPU_X86
				x86e->Emit(op_and32, &virt_arm_reg(0), 0xfffffffc);
#else
				armv_bic(r0, r0, 3);
#endif
				void *ref = armv_start_conditional(cc);
				StoreReg(r0,R15_ARM_NEXT,cc);
				armv_end_conditional(ref);
				Cycles += 3;
			}
			break;

		case VOT_B:
		case VOT_BL:
			{
				s32 offs=((s32)opcd<<8)>>6;

				if (op_flags & OP_IS_COND)
				{
					armv_imm_to_reg(R15_ARM_NEXT,pc+4);
					LoadFlags();
					ConditionCode cc=(ConditionCode)(opcd>>28);
					void *ref = armv_start_conditional(cc);
					if (opt==VOT_BL)
					{
						armv_MOV32(r0,pc+4);
						StoreReg(r0,14,cc);
					}

					armv_MOV32(r0,pc+8+offs);
					StoreReg(r0,R15_ARM_NEXT,cc);
					armv_end_conditional(ref);
				}
				else
				{
					if (opt==VOT_BL)
						armv_imm_to_reg(14,pc+4);

					armv_imm_to_reg(R15_ARM_NEXT,pc+8+offs);
				}
				Cycles += 3;
			}
			break;

		case VOT_Read:
			{
				u32 offs=opcd&4095;
				bool U=opcd&(1<<23);
				bool Pre=opcd&(1<<24);
				bool W=opcd&(1<<21);
				bool I=opcd&(1<<25);
				bool L = opcd & (1 << 20);
				u32 Rn=(opcd>>16)&15;
				u32 Rd=(opcd>>12)&15;

				bool DoWB = (W || !Pre) && Rn != Rd;
				bool DoAdd=DoWB || Pre;

				if (!I && offs == 0)
				{
					DoWB=false;
					DoAdd=false;
				}

				verify(!((Rn==15) && DoWB));

				if (Rn!=15)
				{
					LoadReg(r0,Rn);

					if (DoAdd)
					{
						eReg dst=Pre?r0:r9;

						if (!I && is_i8r4(offs))
						{
							if (U)
								armv_add(dst, r0, offs);
							else
								armv_add(dst, r0, -offs);
						}
						else
						{
							MemOperand2(dst,I,U,offs,opcd);
						}

						if (DoWB && dst==r0)
							armv_mov(r9, r0);
					}
				}
				else
				{
					u32 addr=pc+8;

					if (Pre && offs && !I)
					{
						addr+=U?offs:-offs;
					}
					
					armv_MOV32(r0,addr);
					
					if (Pre && I)
					{
						MemOperand2(r1,I,U,offs,opcd);
						armv_add(r0, r0, r1);
					}
				}

				if (!L)
				{
					if (Rd==15)
					{
						armv_MOV32(r1,pc+12);
					}
					else
					{
						LoadReg(r1,Rd);
					}
				}
				armv_call(GetMemOp(L, CHK_BTS(1,22,1)));

				if (L)
				{
					if (Rd==15)
					{
						verify(op_flags & OP_SETS_PC);
						StoreReg(r0,R15_ARM_NEXT);
					}
					else
					{
						StoreReg(r0,Rd);
					}
				}
				
				if (DoWB)
				{
					StoreReg(r9,Rn);
				}
				if (L)
					Cycles += 4;
				else
					Cycles += 3;
			}
			break;

		case VOT_MRS:
			{
				u32 Rd=(opcd>>12)&15;
				armv_call((void*)&CPUUpdateCPSR);

				if (opcd & (1<<22))
				{
					LoadReg(r0,17);
				}
				else
				{
					LoadReg(r0,16);
				}

				StoreReg(r0,Rd);
			}
			break;

		case VOT_MSR:
			{
				u32 Rm=(opcd>>0)&15;
				LoadReg(r0,Rm);
				if (opcd & (1<<22))
					armv_call((void*)(void (DYNACALL*)(u32))&MSR_do<1>);
				else
					armv_call((void*)(void (DYNACALL*)(u32))&MSR_do<0>);

				if (op_flags & OP_SETS_PC)
					armv_imm_to_reg(R15_ARM_NEXT,pc+4);
				Cycles++;
			}
			break;
			
		case VOT_Fallback:
			{
				Cycles -= 6;
				armv_imm_to_reg(15,pc+8);

				if (op_flags & OP_SETS_PC)
					armv_imm_to_reg(R15_ARM_NEXT,pc+4);

#if HOST_CPU==CPU_X86
				if ( !(op_flags & OP_SETS_PC) )
					armv_imm_to_reg(R15_ARM_NEXT,pc+4);
#endif

				armv_intpr(opcd);

#if HOST_CPU==CPU_X86
				if ( !(op_flags & OP_SETS_PC) )
				{
					armv_check_pc(pc+4);
				}
#endif
			}
			break;

		default:
			die("can't happen\n");
		}

#if HOST_CPU==CPU_X86
		armv_imm_to_reg(15,0xF87641FF);
		armv_prof(opt,opcd,op_flags);
#endif

		if (op_flags & OP_SETS_PC)
		{
			arm_printf("ARM: %06X: Block End %d\n",pc,ops);
			break;
		}

		if (ops>32)
		{
			arm_printf("ARM: %06X: Block split %d\n",pc,ops);
			armv_imm_to_reg(R15_ARM_NEXT,pc+4);
			break;
		}
		
		pc+=4;
	}

	armv_end((void*)rv,Cycles);
}

void FlushCache()
{
	icPtr = ICache;

	void* compile_ptr = (void*)&arm_compilecode;
	void** dst = EntryPoints;
	u32 count = ARRAY_SIZE(EntryPoints);

	u32 i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
	uint32x4_t val = vdupq_n_u32((uintptr_t)compile_ptr);
	for (; i + 7 < count; i += 8)
	{
		vst1q_u32((uint32_t*)(dst + i), val);
		vst1q_u32((uint32_t*)(dst + i + 4), val);
	}
#endif
	for (; i < count; i++)
		dst[i] = compile_ptr;
}

#if HOST_CPU==CPU_X86 && defined(_WIN32)

#include <windows.h>

u8* ARM::emit_opt=0;
eReg ARM::reg_addr;
eReg ARM::reg_dst;
s32 ARM::imma;

void armEmit32(u32 emit32)
{
	if (icPtr >= (ICache + ICacheSize - 64*1024)) {
		die("ICache is full, invalidate old entries ...");
	}

	x86e->Emit(op_mov32,ECX,emit32);
	x86e->Emit(op_call,x86_ptr_imm(virt_arm_op));
}

void *armGetEmitPtr()
{
	return icPtr;
}

#endif

void armt_init()
{
	InitHash();

	ICache = (u8*)(((unat)ARM7_TCB+4095)& ~4095);

	#ifdef __MACH__
		munmap(ICache, ICacheSize);
		ICache = (u8*)mmap(ICache, ICacheSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANON, 0, 0);
	#endif

	mem_region_set_exec(ICache, ICacheSize);

#if TARGET_IPHONE
	memset((u8*)mmap(ICache, ICacheSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANON, 0, 0),0xFF,ICacheSize);
#else
	memset(ICache,0xFF,ICacheSize);
#endif

	FlushCache();
}

#endif
