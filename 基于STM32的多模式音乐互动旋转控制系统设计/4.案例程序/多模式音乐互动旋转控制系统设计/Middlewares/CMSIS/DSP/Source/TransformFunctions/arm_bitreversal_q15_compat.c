#include "dsp/transform_functions.h"

ARM_DSP_ATTRIBUTE void arm_bitreversal_q15(
        q15_t * pSrc16,
        uint32_t fftLen,
        uint16_t bitRevFactor,
  const uint16_t * pBitRevTab)
{
   q31_t *pSrc = (q31_t *)pSrc16;
   q31_t in;
   uint32_t fftLenBy2;
   uint32_t fftLenBy2p1;
   uint32_t i;
   uint32_t j;

   j = 0u;
   fftLenBy2 = fftLen / 2u;
   fftLenBy2p1 = fftLenBy2 + 1u;

   for (i = 0u; i <= (fftLenBy2 - 2u); i += 2u)
   {
      if (i < j)
      {
         in = pSrc[i];
         pSrc[i] = pSrc[j];
         pSrc[j] = in;

         in = pSrc[i + fftLenBy2p1];
         pSrc[i + fftLenBy2p1] = pSrc[j + fftLenBy2p1];
         pSrc[j + fftLenBy2p1] = in;
      }

      in = pSrc[i + 1u];
      pSrc[i + 1u] = pSrc[j + fftLenBy2];
      pSrc[j + fftLenBy2] = in;

      j = *pBitRevTab;
      pBitRevTab += bitRevFactor;
   }
}
