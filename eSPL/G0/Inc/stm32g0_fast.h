#ifndef STM32G0_FAST_H
#define STM32G0_FAST_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_diag.h"
#include "stm32g0_iwdg.h"

/* Sicak yol erisimcileri (Docs/FAST_PATH.md).
 *
 * Dongude ve kesmelerde her cagrida calisan basit erisimler. Hepsi static inline: cagri yerine
 * birkac komut olarak acilir. Yapilandirma (Init, Config) burada YOK; o is denetimli API'nin.
 *
 * Kurallar (METHOD istisnasi, Docs/FAST_PATH.md bolum 2):
 *  1. Yalniz erisim: tek register okuma / yazma ya da bayrak temizleme. Kip degisikligi ve
 *     cok adimli sira yok.
 *  2. Isimde "Fast": cagri yerinde denetimsiz bir erisim yapildigi gorunur.
 *  3. Kanal isimde (TIM_FastSetCompare2) ya da skaler arguman SPL_DIAG ile derleme zamaninda
 *     denetlenir (stm32g0_diag.h).
 *  4. Hic kaldirilmayan denetimler: donanim durumu (yakalama kaybi) ve bayrak temizledikten
 *     sonra geri okuma (METHOD 1.7). DAC deger / DMA durumu ve DMA kanal araligi / ES0548 2.4.1
 *     SAFE sinifi (Docs/CHECK_LEVEL.md 5.3, 5.4): SPL_CHECK_FAST'ta derlenmez. Denetimli
 *     erisimciler SPL_Status dondurur.
 *  5. Durum dondurmeyen erisimci degeri donus degeriyle verir (cikis isaretcisi yok).
 *
 * CAGIRANIN YUKUMLULUKLERI (bunlar calisma aninda DENETLENMEZ):
 *  Y1. Ornek (GPIOx, TIMx, DAC1, FDCANx) denetimli Init / Config API'si ile kurulmus, saati
 *      acik olmali.
 *  Y2. Ornek isaretcisi dogrudan bir CMSIS sabiti olmali (GPIOD, TIM1); tablodan ya da
 *      degiskenden gelmemeli. Kart esleme basligi bunu saglar.
 *  Y3. TIM_FastSetCompareN: deger timer genisligine sigmali (TIM2 32 bit, digerleri 16 bit).
 *      Fazlasi sessizce kesilir; sinir degerin uretildigi yerde saglanir.
 *  Y4. TIM_FastGetCounter: CR1.UIFREMAP = 0 olmali (yoksa bit 31 sayac degil UIF kopyasidir).
 *  Y5. Kanal ve hat argumanlari (DMA1_Fast*, EXTI_Fast*) SABIT olmali; SPL_DIAG yalniz
 *      sabitleri denetleyebilir.
 *  Y6. TIM_FastSetCompareN / TIM_FastGetCaptureN: kanal o timer'da bulunmali (TIM14, TIM16,
 *      TIM17 tek kanalli; TIM15 iki kanalli). */

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- GPIO ---- */

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastRead: pin maskesi bos");
static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastSet: pin maskesi bos");
static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastReset: pin maskesi bos");

/* IDR & pins. */
static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
{
    return (uint16_t)(port->IDR & (uint32_t)pins);
}

/* BSRR set yarisi: tek yazma, oku-degistir-yaz yok, her baglamdan guvenli. */
static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
{
    port->BSRR = (uint32_t)pins;
}

/* BRR: tek yazma. */
static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
{
    port->BRR = (uint32_t)pins;
}

/* ---- TIM: karsilastirma, sayac, yon ---- */

/* CCRx tek yazma. Y3: deger genislige sigmali. OCxPE = 1 ise bir sonraki update'te etkin. */
static inline void TIM_FastSetCompare1(TIM_TypeDef *t, uint32_t value)
{
    t->CCR1 = value;
}
static inline void TIM_FastSetCompare2(TIM_TypeDef *t, uint32_t value)
{
    t->CCR2 = value;
}
static inline void TIM_FastSetCompare3(TIM_TypeDef *t, uint32_t value)
{
    t->CCR3 = value;
}
static inline void TIM_FastSetCompare4(TIM_TypeDef *t, uint32_t value)
{
    t->CCR4 = value;
}

/* CNT. Y4: UIFREMAP = 0 iken 16 bitlik timer'larda bit 31..16 sifir okunur. */
static inline uint32_t TIM_FastGetCounter(const TIM_TypeDef *t)
{
    return t->CNT;
}

/* CR1.DIR: 1 = asagi sayiyor. Sayici kipi olmayan timer'da bit ayrilmis, 0 okunur. */
static inline uint8_t TIM_FastIsCountingDown(const TIM_TypeDef *t)
{
    return (uint8_t)(((t->CR1 & TIM_CR1_DIR) != 0U) ? 1U : 0U);
}

/* ---- TIM: yakalama (durum denetimi korunur) ---- */

/* Ortak govde. CCxOF (SR bit ch + 8) yakalama kaybi: deger guvenilmez -> SPL_ERR_HW, bayrak
 * TIM_FastClearFlags ile temizlenir. CCxIF (SR bit ch) yoksa yeni yakalama yok -> bayat deger
 * yerine SPL_WOULD_BLOCK. CCRx okumak CCxIF'i donanimda temizler. */
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status Tim_FastCapture(const TIM_TypeDef *t, uint32_t ch_bit,
                                         const volatile uint32_t *ccr, uint32_t *value)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(value != (uint32_t *)0, SPL_ERR_PARAM); /* SAFE (BULGU-03) */
    sr = t->SR;
    if ((sr & (ch_bit << 8U)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & ch_bit) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *value = *ccr;
    return SPL_OK;
}
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture1(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC1IF, &t->CCR1, value);
}
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture2(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC2IF, &t->CCR2, value);
}
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture3(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC3IF, &t->CCR3, value);
}
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture4(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC4IF, &t->CCR4, value);
}

/* ---- TIM: bayraklar ---- */

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline void TIM_FastClearFlags(TIM_TypeDef *t, uint32_t flags)
    SPL_DIAG(flags == 0U, "TIM_FastClearFlags: bayrak maskesi bos");

/* Timer tipine gore gecerli SR bitleri (RM0444 21.4.5, 22.4.5, 24.4.3, 25.5.5, 25.6.4); denetimli
 * SPL'in stm32g0_tim.c maskeleriyle ayni. Ornek sabitse derleyici karsilastirmalari katlar. */
static inline uint32_t Tim_FastSrMask(const TIM_TypeDef *t)
{
    const uint32_t general = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF |
                             TIM_SR_CC4IF | TIM_SR_TIF | TIM_SR_CC1OF | TIM_SR_CC2OF |
                             TIM_SR_CC3OF | TIM_SR_CC4OF;
    uint32_t mask = TIM_SR_UIF; /* TIM6 / TIM7 */

    if (t == TIM1)
    {
        mask = general | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF | TIM_SR_CC5IF |
               TIM_SR_CC6IF;
    }
    else if (t == TIM3)
    {
        mask = general;
    }
#if defined(TIM2)
    else if (t == TIM2)
    {
        mask = general;
    }
#endif
#if defined(TIM4)
    else if (t == TIM4)
    {
        mask = general;
    }
#endif
#if defined(TIM15)
    else if (t == TIM15)
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_COMIF | TIM_SR_TIF | TIM_SR_BIF |
               TIM_SR_CC1OF | TIM_SR_CC2OF;
    }
#endif
    else if ((t == TIM16) || (t == TIM17))
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_CC1OF;
    }
    else if (t == TIM14)
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC1OF;
    }
    else
    {
        /* TIM6 / TIM7: yalniz UIF */
    }
    return mask;
}

/* SR & gecerli bitler. */
static inline uint32_t TIM_FastReadFlags(const TIM_TypeDef *t)
{
    return t->SR & Tim_FastSrMask(t);
}

/* SR rc_w0: istenen bitlere 0, diger gecerli bitlere 1, ayrilmis bitlere reset degeri 0 yazilir;
 * oku-degistir-yaz yok (arada gelen bayrak silinmez). */
static inline void TIM_FastClearFlags(TIM_TypeDef *t, uint32_t flags)
{
    t->SR = Tim_FastSrMask(t) & ~flags;
    /* issue #23: temizleme cevre birimine ulasmadan donulmez (METHOD 1.7) */
    (void)t->SR;
}

/* UIF temizleme: en sik kesme yolu. */
static inline void TIM_FastClearUpdate(TIM_TypeDef *t)
{
    t->SR = Tim_FastSrMask(t) & ~TIM_SR_UIF;
    /* issue #23: temizleme cevre birimine ulasmadan donulmez (METHOD 1.7) */
    (void)t->SR;
}

#if defined(DAC1)
/* ---- DAC (deger ve DMA durumu korunur) ---- */

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline SPL_Status DAC_FastWrite12R1(DAC_TypeDef *dac, uint32_t value)
    SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R1: deger 12 bite sigmali");
static inline SPL_Status DAC_FastWrite12R2(DAC_TypeDef *dac, uint32_t value)
    SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R2: deger 12 bite sigmali");

/* DHR12Rx. Deger > 0xFFF: ust bitler atilir ve cikis yanlis olurdu -> SPL_ERR_PARAM (yazma yok).
 * DMAENx = 1: DMA ile elle yazma yarisir -> SPL_ERR_STATE (yazma yok). Ikisi de SAFE (A4 dalga 5,
 * CHECK_LEVEL.md 5.3): FULL / MIX'te denetlenir, FAST'ta UFAST bicimi (yalniz yazma). */
/* Seviye: PARAM[S] STATE[S] */
static inline SPL_Status DAC_FastWrite12R1(DAC_TypeDef *dac, uint32_t value)
{
    SPL_REQUIRE_SAFE(value <= 0xFFFU, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((dac->CR & DAC_CR_DMAEN1) == 0U, SPL_ERR_STATE);
    dac->DHR12R1 = value;
    return SPL_OK;
}
/* Seviye: PARAM[S] STATE[S] */
static inline SPL_Status DAC_FastWrite12R2(DAC_TypeDef *dac, uint32_t value)
{
    SPL_REQUIRE_SAFE(value <= 0xFFFU, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((dac->CR & DAC_CR_DMAEN2) == 0U, SPL_ERR_STATE);
    dac->DHR12R2 = value;
    return SPL_OK;
}
#endif /* DAC1 */

/* ---- IWDG ---- */

static inline void IWDG_FastReload(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

#if defined(FDCAN1)
/* ---- FDCAN ---- */

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
    SPL_DIAG(mask == 0U, "FDCAN_FastClearIrq: bayrak maskesi bos");

/* PSR okumak LEC/DLEC'i 7'ye sifirlar (yikici okuma): degeri bir kez okuyup saklayin. */
static inline uint32_t FDCAN_FastReadPsr(const FDCAN_GlobalTypeDef *can)
{
    return can->PSR;
}

/* ECR okumak CEL'i sifirlar (yikici okuma). */
static inline uint32_t FDCAN_FastReadEcr(const FDCAN_GlobalTypeDef *can)
{
    return can->ECR;
}

static inline uint32_t FDCAN_FastReadIrq(const FDCAN_GlobalTypeDef *can)
{
    return can->IR;
}

/* IR w1c: yalniz istenen bitler yazilir. */
static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
{
    can->IR = mask;
    /* issue #23: temizleme cevre birimine ulasmadan donulmez (METHOD 1.7) */
    (void)can->IR;
}
#endif /* FDCAN1 */

/* ---- DMA1 (kanal 1..DMA1_FAST_CHANNELS) ---- */

/* G05x / G06x / G07x / G08x / G0Bx / G0C1: 7 kanal; G030 / G031 / G041: 5 kanal (RM0444 Table 49,
 * RM0454 Table 33; CMSIS DMA1_Channel6 / 7). 2026-11-14: yorum duzeltildi, kod dogruydu. */
#if defined(DMA1_Channel6)
#define DMA1_FAST_CHANNELS 7U
#else
#define DMA1_FAST_CHANNELS 5U
#endif

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline uint32_t DMA1_FastFlags(uint32_t channel)
    SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS), "DMA1_FastFlags: kanal SKU disinda");
static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
    SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS),
             "DMA1_FastClearFlags: kanal SKU disinda")
        SPL_DIAG((flags == 0U) || (flags > 0xFU), "DMA1_FastClearFlags: bayrak 1..15");

/* Kanal x'in 4 durum biti (GIF, TCIF, HTIF, TEIF). Y5: kanal sabit olmali. */
static inline uint32_t DMA1_FastFlags(uint32_t channel)
{
    return (DMA1->ISR >> ((channel - 1U) * 4U)) & 0xFU;
}

/* IFCR w1c. ERRATA ES0548 2.4.1: kanal acikken GIF temizlemek, ayni cevrimdeki aktarim hatasinda
 * kanalin otomatik kapanmasini bozar ve TEIF kaybolur -> SPL_ERR_STATE (yazma yok). Kanal araligi
 * calisma aninda da denetlenir: disindaki deger kaydirmayi tanimsiz yapardi. */
/* Seviye: PARAM[S,n] STATE[S] */
static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
{
    const DMA_Channel_TypeDef *ch;

    /* Denetim seviyesi (A3, Docs/CHECK_LEVEL.md 5.4): kanal araligi disi kaydirmayi tanimsiz yapar
     * ve komsu kanalin bayraklarini siler (SAFE); flags = 0 etkisiz yazim (normal); flags > 15
     * komsu kanala tasar (SAFE). */
    SPL_REQUIRE_SAFE((channel >= 1U) && (channel <= DMA1_FAST_CHANNELS), SPL_ERR_PARAM);
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(flags <= 0xFU, SPL_ERR_PARAM);
    ch = (const DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + ((channel - 1U) * 0x14U));
    /* ERRATA ES0548 2.4.1 (SAFE). */
    SPL_REQUIRE_SAFE(((flags & DMA_IFCR_CGIF1) == 0U) || ((ch->CCR & DMA_CCR_EN) == 0U),
                     SPL_ERR_STATE);
    DMA1->IFCR = flags << ((channel - 1U) * 4U);
    /* issue #23: temizleme cevre birimine ulasmadan donulmez (METHOD 1.7) */
    (void)DMA1->ISR;
    return SPL_OK;
}

/* ---- EXTI (GPIO hatlari 0..15) ---- */

/* Bildirim: SPL_DIAG ozniteligi bildirimde durur. */
static inline uint32_t EXTI_FastPending(uint32_t lines)
    SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastPending: GPIO hatlari 0..15");
static inline void EXTI_FastClearPending(uint32_t lines)
    SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastClearPending: GPIO hatlari 0..15");

/* Yukselen ya da dusen kenar bekleyen hatlar. Y5: hat maskesi sabit olmali. */
static inline uint32_t EXTI_FastPending(uint32_t lines)
{
    return (EXTI->RPR1 | EXTI->FPR1) & lines;
}

/* RPR1 / FPR1 w1c: yalniz istenen hatlar. */
static inline void EXTI_FastClearPending(uint32_t lines)
{
    EXTI->RPR1 = lines;
    EXTI->FPR1 = lines;
    /* issue #23: temizleme cevre birimine ulasmadan donulmez (METHOD 1.7) */
    (void)EXTI->RPR1;
}

#ifdef __cplusplus
}
#endif

#endif /* STM32G0_FAST_H */
