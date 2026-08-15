#include "aica.h"
#include "aica_if.h"
#include "aica_mem.h"
#include "sgc_if.h"
#include "hw/holly/holly_intc.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_sched.h"

#define SH4_IRQ_BIT (1 << (holly_SPU_IRQ & 31))

CommonData_struct* CommonData;
DSPData_struct* DSPData;
InterruptInfo* MCIEB;
InterruptInfo* MCIPD;
InterruptInfo* MCIRE;
InterruptInfo* SCIEB;
InterruptInfo* SCIPD;
InterruptInfo* SCIRE;

//Interrupts
//arm side
static inline u32 GetL(u32 which)
{
   if (which > 7)
      which = 7; //higher bits share bit 7

   u32 bit = 1 << which;
   u32 rv  = 0;

   if (CommonData->SCILV0 & bit)
      rv = 1;

   if (CommonData->SCILV1 & bit)
      rv |= 2;

   if (CommonData->SCILV2 & bit)
      rv |= 4;

   return rv;
}

static inline void update_arm_interrupts()
{
   u32 p_ints = SCIEB->full & SCIPD->full;

   if (!p_ints)
   {
      libARM_InterruptChange(0, 0);
      return;
   }

   u32 Lval = 0;
   u32 bit_value = 1;
   for (u32 i = 0; i < 11; i++)
   {
      if (p_ints & bit_value)
      {
         Lval = GetL(i);
         break;
      }
      bit_value <<= 1;
   }

   libARM_InterruptChange(p_ints, Lval);
}

//sh4 side
static inline void UpdateSh4Ints()
{
   u32 p_ints = MCIEB->full & MCIPD->full;
   if (p_ints)
   {
      if ((SB_ISTEXT & SH4_IRQ_BIT) == 0)
         asic_RaiseInterrupt(holly_SPU_IRQ);
   }
   else
   {
      if ((SB_ISTEXT & SH4_IRQ_BIT) != 0)
         asic_CancelInterrupt(holly_SPU_IRQ);
   }
}

AicaTimer timers[3];
int aica_schid = -1;
const int AICA_TICK = 145125;	// 44.1 KHz / 32

static int AicaUpdate(int tag, int c, int j)
{
   aicaarm::run(32);

#if defined(LOW_END)
   // На RPi3 всегда используем быстрый 32-сэмпловый пакет
   AICA_Sample32();
#else
   if (!settings.aica.NoBatch && !settings.aica.DSPEnabled)
      AICA_Sample32();
#endif

   return AICA_TICK;
}

//Mainloop

void libAICA_TimeStep()
{
   for (int i = 0; i < 3; i++)
      timers[i].StepTimer(1);

   SCIPD->SAMPLE_DONE = 1;
   MCIPD->SAMPLE_DONE = 1;

#if !defined(LOW_END)
   if (settings.aica.NoBatch)
      AICA_Sample();
#endif

   update_arm_interrupts();
   UpdateSh4Ints();	
}

static void AicaInternalDMA()
{
	if (!CommonData->DEXE)
		return;

	DEBUG_LOG(AICA, "AICA internal DMA: DGATE %d DDIR %d DLG %x", CommonData->DGATE, CommonData->DDIR, CommonData->DLG);
	if (CommonData->DGATE)
	{
		if (CommonData->DDIR)
		{
			u32 addr = ((CommonData->DMEA_hi << 16) | (CommonData->DMEA_lo << 2)) & ARAM_MASK;
			u32 len = std::min(CommonData->DLG, ARAM_SIZE - addr);
			memset(&aica_ram.data[addr], 0, len * 4);
		}
		else
		{
			u32 addr = CommonData->DRGA << 2;
			for (u32 i = 0; i < CommonData->DLG; i++, addr += 4)
				WriteMem_aica_reg(addr, 0, 4);
		}
	}
	else
	{
		u32 waddr = ((CommonData->DMEA_hi << 16) | (CommonData->DMEA_lo << 2)) & ARAM_MASK;
		u32 raddr = CommonData->DRGA << 2;
		u32 len = std::min(CommonData->DLG, ARAM_SIZE - waddr);
		if (CommonData->DDIR)
		{
			for (u32 i = 0; i < len; i++, waddr += 4, raddr += 4)
				*(u32*)&aica_ram[waddr] = ReadMem_aica_reg(raddr, 4);
		}
		else
		{
			for (u32 i = 0; i < len; i++, waddr += 4, raddr += 4)
				WriteMem_aica_reg(raddr, *(u32*)&aica_ram[waddr], 4);
		}
	}
	CommonData->DEXE = 0;
	MCIPD->DMA_END = 1;
	UpdateSh4Ints();
	SCIPD->DMA_END = 1;
	update_arm_interrupts();
}

//Memory i/o
template<u32 sz>
void WriteAicaReg(u32 reg, u32 data)
{
	switch (reg)
	{
	case SCIPD_addr:
		verify(sz != 1);
		if (data & (1 << 5))
		{
			SCIPD->SCPU = 1;
			update_arm_interrupts();
		}
		return;

	case SCIRE_addr:
		{
			verify(sz != 1);
			SCIPD->full &= ~data;
			update_arm_interrupts();
		}
		break;

	case MCIPD_addr:
		if (data & (1 << 5))
		{
			verify(sz != 1);
			MCIPD->SCPU = 1;
			UpdateSh4Ints();
		}
		return;

	case MCIRE_addr:
		{
			verify(sz != 1);
			MCIPD->full &= ~data;
			UpdateSh4Ints();
		}
		break;

	case TIMER_A:
		WriteMemArr<sz>(aica_reg, reg, data);
		timers[0].RegisterWrite();
		break;

	case TIMER_B:
		WriteMemArr<sz>(aica_reg, reg, data);
		timers[1].RegisterWrite();
		break;

	case TIMER_C:
		WriteMemArr<sz>(aica_reg, reg, data);
		timers[2].RegisterWrite();
		break;

	case 0x288C:
		WriteMemArr<sz>(aica_reg, reg, data);
		AicaInternalDMA();
		break;

	default:
		WriteMemArr<sz>(aica_reg, reg, data);
		break;
	}
}

template void WriteAicaReg<1>(u32 reg, u32 data);
template void WriteAicaReg<2>(u32 reg, u32 data);

s32 libAICA_Init()
{
	init_mem();
	aica_Init();

	static_assert(sizeof(*CommonData) == 0x508, "Invalid CommonData size");
	static_assert(sizeof(*DSPData) == 0x15C8, "Invalid DSPData size");

	CommonData = (CommonData_struct*)&aica_reg[0x2800];
	DSPData = (DSPData_struct*)&aica_reg[0x3000];

	SCIEB = (InterruptInfo*)&aica_reg[0x289C];
	SCIPD = (InterruptInfo*)&aica_reg[0x289C + 4];
	SCIRE = (InterruptInfo*)&aica_reg[0x289C + 8];

	MCIEB = (InterruptInfo*)&aica_reg[0x28B4];
	MCIPD = (InterruptInfo*)&aica_reg[0x28B4 + 4];
	MCIRE = (InterruptInfo*)&aica_reg[0x28B4 + 8];

	sgc_Init();
	if (aica_schid == -1)
	{
		aica_schid = sh4_sched_register(0, &AicaUpdate);
		sh4_sched_request(aica_schid, AICA_TICK);
	}

	return 0;
}

void libAICA_Reset(bool hard)
{
	if (hard)
	{
		init_mem();
		sgc_Init();
	}
	for (u32 i = 0; i < 3; i++)
		timers[i].Init(aica_reg, i);
	aica_Reset(hard);
}

void libAICA_Term()
{
	sgc_Term();
	term_mem();
}
