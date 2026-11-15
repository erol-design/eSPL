/* Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a); reviewed against RM0444 Rev 6
 * section 10: same channel registers and ISR/IFCR layout, G0B1 channel map in stm32g0_dma_priv.h.
 */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_dma.h"
#include "stm32g0_dma_priv.h"

/*
 * DMA register operations are intentionally non-blocking.
 * Channel address decoding is shared with DMAMUX; no clock is enabled here.
 *
 * Denetim seviyesi (A3, Docs/CHECK_LEVEL.md 5.4 DMA tablosu): SPL_REQUIRE = normal sinif (yalniz
 * FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Kanal konumu (denetleyici, indeks) her
 * seviyede dogrulamadan bagimsiz, saf adres hesabiyla bulunur. Siralama: butun makrolar ilk MMIO
 * yazimindan once (METHOD 1.3).
 */

/* Kanal gecerli mi (SAFE): gecersiz kanal isaretcisi baska bir cevre biriminin register'ina yazar
 * ve IFCR kaydirmasiyla komsu kanalin bayraklarini siler (sessiz bozulma). */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* Tek kopya: 8 API'nin her birine gomulmesin diye (FULL boyutu, CHECK_LEVEL.md 7.2). Yalniz
 * dogrulamada kullanilir; FAST'ta derlenmez (kullanilmayan fonksiyon olmasin). Saf. */
SPL_NOINLINE static uint32_t Dma_IsChannel(const DMA_Channel_TypeDef *ch)
{
    return STM32G0_DmaIsChannel(ch);
}
#define DMA_CHANNEL_OK(ch) (Dma_IsChannel(ch) != 0U)
#else
#define DMA_CHANNEL_OK(ch) (STM32G0_DmaIsChannel(ch) != 0U) /* kapali seviyede degerlendirilmez */
#endif

/* Konum (denetleyici + indeks) tek paylasilan kopyada: her API'ye gomulseydi taban adres sabitleri
 * her fonksiyonda yeniden yuklenirdi (FULL +172 B, CHECK_LEVEL.md 7.2). Dogrulama yapmaz; cikti
 * yazar, bu yuzden SPL_REQUIRE* kosulunda kullanilmaz (PURE CHECK). */
SPL_NOINLINE static DMA_TypeDef *Dma_Locate(const DMA_Channel_TypeDef *ch, uint32_t *index)
{
    *index = STM32G0_DmaLocalIndex(ch);
    return STM32G0_DmaController(ch);
}

/* Kanalin ISR'deki 4 biti (GIF, TCIF, HTIF, TEIF). Saf: ISR okumasi yan etkisiz, cikti yok
 * (Tools/pure_checks.txt izinli); DMA_Enable'in eski bayrak denetimi ve DMA_Flags ortak kullanir.
 */
SPL_NOINLINE static uint32_t Dma_ChannelIsr(const DMA_Channel_TypeDef *ch)
{
    uint32_t index;
    const DMA_TypeDef *controller = Dma_Locate(ch, &index);

    return (controller->ISR >> (index * 4U)) & 0xFU;
}

SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg)
{
    DMA_TypeDef *controller;
    uint32_t index;
    uint32_t ccr;

    /* Validate everything before the first peripheral register write. */
    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(cfg != (const DMA_InitTypeDef *)0, SPL_ERR_PARAM);
    /* count = 0: kanal hic aktarim yapmaz, hata hemen gorunur (normal). */
    SPL_REQUIRE(cfg->count != 0U, SPL_ERR_PARAM);
    /* Genislik > 2: PSIZE / MSIZE'a ayrilmis kodlama (11) ya da komsu alana tasma (SAFE). */
    SPL_REQUIRE_SAFE((cfg->peripheral_width <= 2U) && (cfg->memory_width <= 2U), SPL_ERR_PARAM);
    /* direction > 2: yon sessizce cevreden bellege olur (SAFE). */
    SPL_REQUIRE_SAFE(cfg->direction <= 2U, SPL_ERR_PARAM);
    /* priority > 3: PL'den tasan bit MEM2MEM'i kurar, kanal bellekten bellege kopyalar (SAFE). */
    SPL_REQUIRE_SAFE(cfg->priority <= 3U, SPL_ERR_PARAM);
    /* 0/1 bayraklari: 1'den buyuk deger 1 gibi davranir, baska bit yazilmaz (normal). */
    SPL_REQUIRE((cfg->peripheral_increment <= 1U) && (cfg->memory_increment <= 1U) &&
                    (cfg->circular <= 1U),
                SPL_ERR_PARAM);
    /* Adres 0: DMA vektor tablosu bolgesinden sessizce okur ya da yazar (SAFE). */
    SPL_REQUIRE_SAFE((cfg->peripheral_address != 0U) && (cfg->memory_address != 0U), SPL_ERR_PARAM);
    /* Transfer widths are encoded as 0=8, 1=16, 2=32 bits. Hizasiz adres: DMA alt bitleri yok
     * sayar, aktarim baska adrese gider (SAFE). Genislik denetimi bundan once gelir (kaydirma). */
    SPL_REQUIRE_SAFE(((cfg->peripheral_address & ((1U << cfg->peripheral_width) - 1U)) == 0U) &&
                         ((cfg->memory_address & ((1U << cfg->memory_width) - 1U)) == 0U),
                     SPL_ERR_PARAM);
    /* RM0444 10.3: MEM2MEM ile dairesel kip birlikte kullanilamaz (SAFE). */
    SPL_REQUIRE_SAFE((cfg->direction != 2U) || (cfg->circular == 0U), SPL_ERR_PARAM);
    /* Reconfiguring an active channel would corrupt an in-flight transfer (SAFE). */
    SPL_REQUIRE_SAFE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    controller = Dma_Locate(ch, &index);
    ccr = ((uint32_t)cfg->peripheral_width << DMA_CCR_PSIZE_Pos) |
          ((uint32_t)cfg->memory_width << DMA_CCR_MSIZE_Pos) |
          ((uint32_t)cfg->priority << DMA_CCR_PL_Pos);

    if (cfg->direction == 1U)
    {
        ccr |= DMA_CCR_DIR;
    }
    if (cfg->direction == 2U)
    {
        ccr |= DMA_CCR_MEM2MEM;
    }
    if (cfg->peripheral_increment != 0U)
    {
        ccr |= DMA_CCR_PINC;
    }
    if (cfg->memory_increment != 0U)
    {
        ccr |= DMA_CCR_MINC;
    }
    if (cfg->circular != 0U)
    {
        ccr |= DMA_CCR_CIRC;
    }

    ch->CCR = ccr;
    ch->CNDTR = cfg->count;
    ch->CPAR = cfg->peripheral_address;
    ch->CMAR = cfg->memory_address;

    /* IFCR is W1C. Retire completion/error flags from the old transfer.
     * Configuring a disabled channel starts a new transfer epoch. */
    controller->IFCR = (uint32_t)0xFU << (index * 4U);

    return SPL_OK;
}

SPL_Status DMA_Enable(DMA_Channel_TypeDef *ch, uint8_t enable)
{
    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* enable > 1 acmak gibi davranir (normal). */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    /* CNDTR = 0 ile acilan kanal hic aktarim yapmaz; TC gelmez, hata hemen gorunur (normal). */
    SPL_REQUIRE((enable == 0U) || (ch->CNDTR != 0U), SPL_ERR_STATE);
    /* v0.4 (G4 v0.13.4 #9, DMA-08a): a stale TC/HT/TE flag of the previous transfer would raise
     * an enabled interrupt at once: the caller clears it with DMA_ClearFlags first (SAFE: sahte
     * kesme). ISR okumasi yan etkisiz. */
    SPL_REQUIRE_SAFE((enable == 0U) || ((Dma_ChannelIsr(ch) & DMA_STALE_FLAGS) == 0U),
                     SPL_ERR_STATE);

    if (enable != 0U)
    {
        /* Normal bellek yazimlari (tampon) kanal acilmadan tamamlanir: LTO ile tamponu dolduran
         * kod bu fonksiyona gomulurse derleyici yazimlari EN'den sonraya tasiyamaz. */
        /* Barrier: buffer writes complete before the channel starts (METHOD 1.7, issue #24). */
        __DMB();
        ch->CCR |= DMA_CCR_EN;
    }
    else
    {
        ch->CCR &= (uint32_t)~DMA_CCR_EN;
    }
    return SPL_OK;
}

SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    controller = Dma_Locate(ch, &index);
    /* RM0444 10.6.3: CCR fields other than EN are writable only with EN = 0. */
    ch->CCR &= (uint32_t)~DMA_CCR_EN;
    ch->CCR = 0U;
    ch->CNDTR = 0U;
    ch->CPAR = 0U;
    ch->CMAR = 0U;
    /* IFCR is W1C; GIF is safe now that EN = 0 (ERRATA ES0548 2.4.1). */
    controller->IFCR = (uint32_t)0xFU << (index * 4U);
    return SPL_OK;
}

SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count)
{
    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE(count != 0U, SPL_ERR_PARAM);
    /* EN = 1 iken CNDTR salt okunur: yazim sessizce yok sayilir (SAFE). */
    SPL_REQUIRE_SAFE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    ch->CNDTR = count;
    return SPL_OK;
}

SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining)
{
    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(remaining != (uint16_t *)0, SPL_ERR_PARAM);

    *remaining = (uint16_t)ch->CNDTR;
    return SPL_OK;
}

SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags)
{
    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(flags != (uint8_t *)0, SPL_ERR_PARAM);

    /* Each DMA channel occupies four consecutive status bits in ISR. */
    *flags = (uint8_t)Dma_ChannelIsr(ch);
    return SPL_OK;
}

SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* flags > 15: fazla bitler komsu kanalin bayraklarini siler (SAFE). */
    SPL_REQUIRE_SAFE(flags <= 15U, SPL_ERR_PARAM);
    /* ERRATA ES0548 2.4.1: CGIFx in the cycle of a transfer error
     * defeats the automatic channel disable and loses TEIFx: refuse GIF while the channel is
     * enabled (SAFE). */
    SPL_REQUIRE_SAFE(((flags & DMA_FLAG_GIF) == 0U) || ((ch->CCR & DMA_CCR_EN) == 0U),
                     SPL_ERR_STATE);

    controller = Dma_Locate(ch, &index);
    /* IFCR is write-one-to-clear: do not use read-modify-write. */
    controller->IFCR = ((uint32_t)flags) << (index * 4U);
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)controller->ISR;
    return SPL_OK;
}

SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch, uint8_t mask)
{
    uint32_t ccr;
    uint32_t irq_bits;
    const uint32_t irq_mask = DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;

    SPL_REQUIRE_SAFE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* mask > 7: fazla bitler irq_mask ile atilir, baska bit yazilmaz (normal). */
    SPL_REQUIRE(mask <= 7U, SPL_ERR_PARAM);
    /* CCR bir kez okunur (FULL'da onceki davranis): EN = 1 iken CCR alanlari yazilamaz, yazim
     * sessizce yok sayilir (SAFE). Okuma yazma degildir; METHOD 1.3 sirasini bozmaz. */
    ccr = ch->CCR;
    SPL_REQUIRE_SAFE((ccr & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    /* API: TC=bit0, HT=bit1, TE=bit2. CCR starts at TCIE=bit1. */
    irq_bits = ((uint32_t)mask << DMA_CCR_TCIE_Pos) & irq_mask;
    ch->CCR = (ccr & ~irq_mask) | irq_bits;

    return SPL_OK;
}
