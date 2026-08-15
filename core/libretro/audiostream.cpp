#include "types.h"
#include "emulator.h"

#include <libretro.h>

#define SAMPLE_COUNT 512
#define AUDIO_BUFFER_SIZE 2048

extern retro_audio_sample_batch_t audio_batch_cb;

static SoundFrame Buffer[AUDIO_BUFFER_SIZE];
static u32 writePtr;

void WriteSample(s16 r, s16 l)
{
   Buffer[writePtr].r = r;
   Buffer[writePtr].l = l;
   ++writePtr;

#if !defined(TARGET_NO_THREADS)
   if (settings.rend.ThreadedRendering)
   {
      if (writePtr >= SAMPLE_COUNT)
      {
         if (dc_is_running())
         {
            // Отправляем сэмплы в RetroArch.
            audio_batch_cb((const int16_t*)Buffer, SAMPLE_COUNT);
         }
         writePtr = 0;
      }
      return;
   }
#endif

   if (writePtr >= AUDIO_BUFFER_SIZE)
   {
      if (dc_is_running())
         audio_batch_cb((const int16_t*)Buffer, writePtr);
      writePtr = 0;
   }
}

void FlushAudioFrame(void)
{
   if (writePtr > 0)
   {
      audio_batch_cb((const int16_t*)Buffer, writePtr);
      writePtr = 0;
   }
}

void ResetAudioBuffer(void)
{
   writePtr = 0;
}
