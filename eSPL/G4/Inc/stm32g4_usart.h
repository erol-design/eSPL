/**
 * @file    stm32g4_usart.h
 * @brief   STM32G4 USART, UART ve LPUART yapilandirmasi ve beklemesiz G/C.
 *
 * Kullanim: eski yol USART_Init() asenkron 8N1 kurar ve UE'yi acar. Genisletilmis yol:
 * USART_InitEx(), UE kapaliyken istege bagli FIFO / DE / iletisim modu ayarlari, sonra
 * USART_Enable(). TEACK/REACK gorulmeden veri aktarilmamasi gerekiyorsa USART_Ready().
 *
 * Kapsam      : Secili CMSIS aygitinin tanimladigi USART1..3, UART4/5 ve LPUART1.
 *               Bazi API'ler daha dar bir kumeyi destekler ve SPL_ERR_UNSUPPORTED doner.
 * Sahiplik    : RCC saatleri, cekirdek saati secimi, GPIO alternatif islevleri, pinler,
 *               NVIC, DMA kanallari ve dis alici-vericiler cagiranindir.
 * Bellek      : Dinamik bellek yok, kullanici isaretcisi saklanmaz, degisken yazilim durumu
 *               yok; durum cevre birimi register'larindadir.
 * Bekleme     : Hicbir API beklemez. Try, durum ve istek servisleri tek adim yapar;
 *               SPL_WOULD_BLOCK ya da SPL_BUSY "sonra tekrar dene" demektir.
 * Eszamanlilik: Ayni ornege cagrilar siralanmaz. Gorev, ISR ve DMA erisimini (ozellikle
 *               CR1/CR3 guncellemeleri ve TDR/RDR veri yolu) cagiran siralar. Farkli
 *               ornekler bagimsizdir.
 * ISR         : Butun API'ler ISR'den cagrilabilir (kilit ve bekleme yok) | Sinir: dongu
 *               yok; USART_InitEx 64 bit bolme kullanir | WCET: olculmedi.
 * Hata cikisi : Reddedilen cagri register yazmaz ve cikti parametrelerini degistirmez.
 * Kurtarma    : ERR_STATE (UE acik) -> USART_Disable ya da USART_Abort, sonra ayarla.
 *               ERR_HW -> USART_ErrorFlags ile bayraklari al, USART_ClearErrorFlags ile
 *               yalniz alinanlari temizle. BUSY / WOULD_BLOCK -> sonra tekrar dene.
 *
 * @see ../Docs/v0.11_USART_CONTRACT.md
 * @see ../Docs/v0.16_USART_MODES_CONTRACT.md
 */

#ifndef STM32G4_USART_H
#define STM32G4_USART_H

#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @brief Eski asenkron 8N1 yapilandirmasi (USART_Init).
     *
     * Cagri sirasinda okunur, saklanmaz.
     */
    typedef struct
    {
        uint32_t kernel_hz; /**< On bolucu oncesi gercek cekirdek saati, Hz (RCC mux sonrasi). */
        uint32_t baudrate;  /**< Istenen hat hizi, bit/s; > 0. */
    } USART_InitTypeDef;
    /**
     * @brief Eski asenkron 8N1 modunu kurar ve acar.
     *
     * USART1/2/3, UART4/5 (CMSIS tanimliysa). CR1, CR2, CR3, PRESC ve BRR yeniden yazilir:
     * 8N1, x16 asiri ornekleme, FIFO / parite / akis kontrolu / kesme yok; alim hata
     * bayraklari temizlenir, TE, RE ve UE acilir. LPUART1 kabul edilmez (BRR formulu farkli;
     * USART_InitEx kullanin).
     *
     * @param[in] instance USART ornegi.
     * @param[in] cfg      Yapilandirma; cagri sirasinda okunur.
     * @retval SPL_OK        Yapilandirma uygulandi, UE acik.
     * @retval SPL_ERR_PARAM Gecersiz ornek (LPUART1 dahil), NULL cfg, sifir saat ya da hiz,
     *                       bolen 16..65535 disinda.
     * @retval SPL_ERR_STATE UE zaten acik.
     */
    /* Seviye: PARAM[S] STATE[S] */
    SPL_Status USART_Init(USART_TypeDef *instance, const USART_InitTypeDef *cfg);

    /**
     * @brief Verici kabul edebiliyorsa bir 8 bit deger yazar.
     *
     * Paritesiz 9 bit veri alani, 8. bit kaybolmasin diye reddedilir. OK disinda TDR
     * yazilmaz.
     *
     * @param[in] instance USART ornegi.
     * @param[in] byte     Gonderilecek deger.
     * @retval SPL_OK          Deger TDR'ye yazildi (hattan cikis bitti demek degildir).
     * @retval SPL_WOULD_BLOCK Verici hazir degil (TXE/TXFNF = 0).
     * @retval SPL_ERR_STATE   UE ya da TE kapali, ya da veri alani 8 bitten genis.
     * @retval SPL_ERR_PARAM   Gecersiz ornek.
     */
    /* Seviye: PARAM[S] STATE[S] WOULD_BLOCK[r] */
    SPL_Status USART_TryWrite(USART_TypeDef *instance, uint8_t byte);

    /**
     * @brief Alim verisi varsa bir 8 bit deger okur.
     *
     * DESTRUCTIVE: OK donuste RDR okunur, alim FIFO'sundan bir eleman cekilir. PE, FE, NE ya
     * da ORE etkinken RDR okunmaz ve bayraklar korunur; temizlemeden once USART_ErrorFlags
     * ile alin.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] byte     Okunan deger.
     * @retval SPL_OK          RDR okundu, byte yazildi.
     * @retval SPL_WOULD_BLOCK Alim verisi yok (RXNE/RXFNE = 0).
     * @retval SPL_ERR_HW      Alim hata bayragi etkin; RDR okunmadi.
     * @retval SPL_ERR_STATE   UE ya da RE kapali, ya da veri alani 8 bitten genis.
     * @retval SPL_ERR_PARAM   Gecersiz ornek ya da NULL cikti.
     * @note Data only: with parity (PCE=1) or a 7-bit frame the parity bit is masked off (CCL-2
     *       YAP-09). USART_TryWrite: bits above the frame's data width are not sent.
     */
    /* Seviye: PARAM[S] STATE[S] HW[r] WOULD_BLOCK[r] */
    SPL_Status USART_TryRead(USART_TypeDef *instance, uint8_t *byte);

    /**
     * @brief PE, FE, NE ve ORE alim hata bayraklarinin hepsini temizler.
     *
     * Hatalar eszamanli gelebiliyorsa USART_ErrorFlags + USART_ClearErrorFlags tercih edilir;
     * bu kolaylik API'si o an kurulu olan butun kumeyi siler (anlik goruntuden sonra gelen
     * hata da kaybolur).
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        ICR yazildi ve ISR geri okundu.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_ClearErrors(USART_TypeDef *instance);

    /**
     * @brief Verici ve alici etkinlestirme onayinin anlik goruntusunu verir; beklemez.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] tx_ready TE acik ve TEACK = 1 ise 1, degilse 0.
     * @param[out] rx_ready RE acik ve REACK = 1 ise 1, degilse 0.
     * @retval SPL_OK        Iki cikti yazildi.
     * @retval SPL_ERR_STATE UE kapali.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] STATE[S] */
    SPL_Status USART_Ready(USART_TypeDef *instance, uint8_t *tx_ready, uint8_t *rx_ready);

    /**
     * @brief Ham PE, FE, NE ve ORE hata bayraklarinin anlik goruntusunu verir.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] errors   ISR & USART_ERRORS_ALL.
     * @retval SPL_OK        Goruntu yazildi.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_ErrorFlags(USART_TypeDef *instance, uint32_t *errors);

    /**
     * @brief Son cercevenin hattan tamamen cikip cikmadigini bildirir.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] complete TC = 1 ise 1, degilse 0.
     * @retval SPL_OK        Durum yazildi.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_TxComplete(USART_TypeDef *instance, uint8_t *complete);

/* ---- v0.11: extended configuration, LPUART1, FIFO, RS485, IRQ/DMA ---- */
/* @see ../Docs/v0.11_USART_CONTRACT.md. Asagidaki yapilandirma API'leri "yalniz UE=0" alanlarina
 * dokunur (RM0440 40.8 / 41.7); UE=1 iken SPL_ERR_STATE doner.
 * ERRATA ES0431 2.12.2 / ES0430 2.16.2 / ES0523 2.12.2: durma bitinin ikinci yarisindaki
 * (yarim bitten kisa) bir parazit RX verisini bozabilir; RX hatti temiz tutulur ya da
 * filtrelenir (kart gorevi).
 * ES0431 2.12.4 / ES0430 2.16.4 / ES0523 2.12.4: uygulanmaz (ONEBIT bu API'de yok).
 * 2.12.1 (senkron slave) ve 2.12.3 (ABREN) icin USART_SyncConfig ve USART_AutoBaudConfig. */

/* USART_InitExTypeDef.word_length: data bits including the parity bit. */
#define USART_WORDLENGTH_7B 7U
#define USART_WORDLENGTH_8B 8U
#define USART_WORDLENGTH_9B 9U
/* USART_InitExTypeDef.parity */
#define USART_PARITY_NONE 0U
#define USART_PARITY_EVEN 1U
#define USART_PARITY_ODD 2U
/* USART_InitExTypeDef.stop_bits (value = CR2.STOP encoding). 0.5 and 1.5
 * are smartcard-oriented (RM0440 40.3/40.5.5): accepted on USART1/2/3 only;
 * UART4/5 and LPUART1 => SPL_ERR_UNSUPPORTED. */
#define USART_STOPBITS_1 0U
#define USART_STOPBITS_0_5 1U
#define USART_STOPBITS_2 2U
#define USART_STOPBITS_1_5 3U
/* USART_InitExTypeDef.oversampling. LPUART1 has no OVER8 => x8 UNSUPPORTED. */
#define USART_OVERSAMPLING_16 0U
#define USART_OVERSAMPLING_8 1U
/* USART_InitExTypeDef.prescaler: PRESC.PRESCALER code (RM0440 40.8.14). */
#define USART_PRESC_DIV1 0U
#define USART_PRESC_DIV2 1U
#define USART_PRESC_DIV4 2U
#define USART_PRESC_DIV6 3U
#define USART_PRESC_DIV8 4U
#define USART_PRESC_DIV10 5U
#define USART_PRESC_DIV12 6U
#define USART_PRESC_DIV16 7U
#define USART_PRESC_DIV32 8U
#define USART_PRESC_DIV64 9U
#define USART_PRESC_DIV128 10U
#define USART_PRESC_DIV256 11U
/* USART_InitExTypeDef.direction (bit mask, at least one bit). */
#define USART_DIR_TX 0x01U
#define USART_DIR_RX 0x02U
/* USART_InitExTypeDef.flow_control */
#define USART_FLOW_NONE 0U
#define USART_FLOW_RTS 1U
#define USART_FLOW_CTS 2U
#define USART_FLOW_RTS_CTS 3U
/* USART_InitExTypeDef.options (bit mask) */
#define USART_OPT_MSBFIRST 0x01U    /* CR2.MSBFIRST */
#define USART_OPT_SWAP 0x02U        /* CR2.SWAP: TX/RX pin functions swapped */
#define USART_OPT_TXINV 0x04U       /* CR2.TXINV: TX pin level inverted */
#define USART_OPT_RXINV 0x08U       /* CR2.RXINV: RX pin level inverted */
#define USART_OPT_DATAINV 0x10U     /* CR2.DATAINV: data (and parity) inverted */
#define USART_OPT_HALF_DUPLEX 0x20U /* CR3.HDSEL: single wire on TX pin */
/* Caller duty (CCL-2 UART-04): in half-duplex (HDSEL) or RS485 use the library does not
 * filter received data; discard own echoed bytes (or keep the transceiver receiver off while
 * transmitting), and release a GPIO direction pin only after USART_TxComplete reports 1. */

    /**
     * @brief Genisletilmis asenkron cerceve yapilandirmasi (USART_InitEx).
     *
     * USART_InitEx sirasinda kopyalanir, adresi saklanmaz. Desteklenen hedeflerde 16 bayt,
     * dolgu yok.
     */
    typedef struct
    {
        uint32_t kernel_hz;   /* usart_ker_ck / lpuart_ker_ck before PRESC, Hz */
        uint32_t baudrate;    /* bit/s, > 0 */
        uint8_t word_length;  /* USART_WORDLENGTH_* */
        uint8_t parity;       /* USART_PARITY_* */
        uint8_t stop_bits;    /* USART_STOPBITS_* */
        uint8_t oversampling; /* USART_OVERSAMPLING_* */
        uint8_t prescaler;    /* USART_PRESC_* */
        uint8_t direction;    /* USART_DIR_* mask */
        uint8_t flow_control; /* USART_FLOW_* */
        uint8_t options;      /* USART_OPT_* mask */
    } USART_InitExTypeDef;

    /**
     * @brief Genisletilmis asenkron cerceveyi kurar, cevre birimini kapali birakir.
     *
     * UE kapali olmali. CR1 (M, PCE, PS, OVER8, TE, RE; FIFOEN, DEAT/DEDT ve kesme izinleri
     * dahil geri kalan 0), CR2 (STOP + secenekler; geri kalan 0), CR3 (RTSE/CTSE/HDSEL;
     * geri kalan 0), PRESC ve BRR yazilir; daha once kurulan mod ayarlari silinir. UE=0
     * kalir: gerekiyorsa USART_FifoConfig / USART_DriverEnableConfig / mod ayarlari, sonra
     * USART_Enable. LPUART1 kabul edilir (BRR = round(256*f/baud), 0x300..0xFFFFF).
     * Sinir: BRR hesabi 64 bit bolme kullanir (Cortex-M4'te kutuphane cagrisi).
     *
     * ERRATA ES0431 2.13.1 / ES0430 2.17.1 / ES0523 2.13.1: LPUART1'de BRR 0x301..0x3FF ve
     * BRR & 0xFF != 0 (cekirdek/hiz orani 3..4 arasinda ve tam sayi degil, ornegin
     * 32768 Hz / 9600 Bd) reddedilir; cekirdek saatini artirin ya da hizi dusurun.
     *
     * @param[in] instance USART, UART ya da LPUART ornegi.
     * @param[in] cfg      Yapilandirma; cagri sirasinda okunur.
     * @retval SPL_OK              Yapilandirma uygulandi; UE kapali.
     * @retval SPL_ERR_PARAM       Gecersiz arguman ya da ulasilamayan hiz (BRR araligi,
     *                             errata 2.13.1).
     * @retval SPL_ERR_UNSUPPORTED Ornekte olmayan ozellik (0.5/1.5 durma biti, LPUART x8).
     * @retval SPL_ERR_STATE       UE acik.
     */
    /* Seviye: PARAM[S,r] STATE[S] UNSUPPORTED[r] */
    SPL_Status USART_InitEx(USART_TypeDef *instance, const USART_InitExTypeDef *cfg);

    /**
     * @brief Yapilandirilmis cevre birimini acar (CR1.UE = 1).
     *
     * Zaten acik ornek yazma yapmadan OK doner. BRR ornegin alt sinirini saglamalidir:
     * once USART_InitEx; TEACK/REACK onayi gerekiyorsa sonra USART_Ready.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        UE acildi ya da zaten acikti.
     * @retval SPL_ERR_STATE BRR gecerli bir baslatilmis deger degil (USART < 0x10,
     *                       LPUART < 0x300); USART_InitEx ile duzeltin.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status USART_Enable(USART_TypeDef *instance);

    /**
     * @brief Bekleyen iletim bittikten sonra cevre birimini kapatir.
     *
     * UE=1, TE=1 ve TC=0 iken UE'ye dokunmadan SPL_BUSY doner (RM0440 40.5.5 adim 8).
     * UE'yi kapatmak DMA isteklerini de sifirlar; DMA kanalini bu cagridan once kapatin.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        UE kapatildi ya da zaten kapaliydi.
     * @retval SPL_BUSY      Son cerceve hattan tamamen cikmadi; sonra tekrar deneyin (CTS
     *                       vericiyi tutuyorsa USART_Abort).
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] BUSY[r] */
    SPL_Status USART_Disable(USART_TypeDef *instance);

    /**
     * @brief UE'yi hemen kapatir.
     *
     * DESTRUCTIVE: suren cerceve atilir; gonderilen ya da alinan veri kaybolabilir. TC
     * beklenmez. Zorunlu kurtarma icindir (CTS ile tutulan verici dahil).
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        UE kapatildi.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_Abort(USART_TypeDef *instance);

/* FIFO thresholds (CR3 TXFTCFG/RXFTCFG). EMPTY_FULL = TXFIFO empty / RXFIFO full. */
#define USART_FIFO_THRESHOLD_1_8 0U
#define USART_FIFO_THRESHOLD_1_4 1U
#define USART_FIFO_THRESHOLD_1_2 2U
#define USART_FIFO_THRESHOLD_3_4 3U
#define USART_FIFO_THRESHOLD_7_8 4U
#define USART_FIFO_THRESHOLD_EMPTY_FULL 5U
    /**
     * @brief FIFO modunu ve gonderim/alim esiklerini ayarlar.
     *
     * UE kapali olmali. Uc deger tek yapilandirma olarak uygulanir.
     *
     * @param[in] instance     USART ornegi.
     * @param[in] enable       1: FIFOEN = 1, 0: kapali.
     * @param[in] tx_threshold USART_FIFO_THRESHOLD_*.
     * @param[in] rx_threshold USART_FIFO_THRESHOLD_*.
     * @retval SPL_OK        FIFO yapilandirmasi uygulandi.
     * @retval SPL_ERR_STATE UE acik.
     * @retval SPL_ERR_PARAM Gecersiz ornek, enable ya da esik.
     */
    /* Seviye: PARAM[S,n] STATE[S] */
    SPL_Status USART_FifoConfig(USART_TypeDef *instance, uint8_t enable, uint8_t tx_threshold,
                                uint8_t rx_threshold);

    /**
     * @brief RTS/DE pininde RS485 surucu etkinlestirme sinyalini ayarlar.
     *
     * UE kapali olmali. RTS akis kontrolu ayni pini kullanirken DE acmak reddedilir.
     *
     * @param[in] instance      USART ornegi.
     * @param[in] enable        1: CR3.DEM = 1.
     * @param[in] active_low    1: CR3.DEP = 1 (DE aktif dusuk).
     * @param[in] assert_time   DEAT, 0..31 ornekleme zamani (x16'da bit/16, x8'de bit/8).
     * @param[in] deassert_time DEDT, 0..31, ayni birim.
     * @retval SPL_OK        DE yapilandirmasi uygulandi.
     * @retval SPL_ERR_STATE UE acik ya da RTS akis kontrolu DE ile cakisiyor.
     * @retval SPL_ERR_PARAM Gecersiz ornek, mantiksal deger ya da zaman.
     */
    /* Seviye: PARAM[S,n] STATE[S] */
    SPL_Status USART_DriverEnableConfig(USART_TypeDef *instance, uint8_t enable, uint8_t active_low,
                                        uint8_t assert_time, uint8_t deassert_time);

    /**
     * @brief Verici hazirsa 9 bite kadar genis TDR yolundan bir deger yazar.
     *
     * OK disinda TDR yazilmaz.
     *
     * @param[in] instance USART ornegi.
     * @param[in] data     Yapilandirilmis veri alanina sigmali (9/8/7 bit, parite varsa bir
     *                     bit eksik).
     * @retval SPL_OK          Deger yazildi.
     * @retval SPL_WOULD_BLOCK Verici hazir degil.
     * @retval SPL_ERR_STATE   UE ya da TE kapali.
     * @retval SPL_ERR_PARAM   Gecersiz ornek ya da data veri alanindan genis.
     */
    /* Seviye: PARAM[S] STATE[S] WOULD_BLOCK[r] */
    SPL_Status USART_TryWrite9(USART_TypeDef *instance, uint16_t data);

    /**
     * @brief Veri varsa 9 bite kadar genis RDR yolundan ham deger okur.
     *
     * DESTRUCTIVE: OK donuste RDR okunur, alim FIFO'sundan bir eleman cekilir. Alim
     * hatasinda RDR okunmaz.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] data     RDR[8:0]; parite aciksa parite biti dahil.
     * @retval SPL_OK          Ham deger okundu.
     * @retval SPL_WOULD_BLOCK Alim verisi yok.
     * @retval SPL_ERR_HW      PE, FE, NE ya da ORE etkin; RDR okunmadi.
     * @retval SPL_ERR_STATE   UE ya da RE kapali.
     * @retval SPL_ERR_PARAM   Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] STATE[S] HW[r] WOULD_BLOCK[r] */
    SPL_Status USART_TryRead9(USART_TypeDef *instance, uint16_t *data);

/* Interrupt sources for USART_ITConfig. */
#define USART_IT_RXNE 0x01U /* CR1.RXNEIE/RXFNEIE (also ORE) */
#define USART_IT_TXE 0x02U  /* CR1.TXEIE/TXFNFIE */
#define USART_IT_TC 0x04U   /* CR1.TCIE */
#define USART_IT_IDLE 0x08U /* CR1.IDLEIE */
#define USART_IT_PE 0x10U   /* CR1.PEIE */
#define USART_IT_ERR 0x20U  /* CR3.EIE (FE/ORE/NE) */
#define USART_IT_ALL 0x3FU
    /**
     * @brief Secili USART kesme kaynaklarini acar ya da kapatir.
     *
     * Yalniz secili CR1/CR3 izin bitleri degisir, digerleri korunur. UE acikken izinlidir.
     * Okuma-degistirme-yazmadir: ayni ornegin diger CR1/CR3 yazicilariyla siralayin.
     *
     * @param[in] instance USART ornegi.
     * @param[in] it_mask  USART_IT_* maskesi, bos olamaz.
     * @param[in] enable   1: ac, 0: kapat.
     * @retval SPL_OK        Secili izinler guncellendi.
     * @retval SPL_ERR_PARAM Gecersiz ornek, maske ya da enable.
     */
    /* Seviye: PARAM[S,n] */
    SPL_Status USART_ITConfig(USART_TypeDef *instance, uint32_t it_mask, uint8_t enable);

/* DMA request flags for USART_DmaConfig. */
#define USART_DMA_TX 0x01U   /* CR3.DMAT */
#define USART_DMA_RX 0x02U   /* CR3.DMAR */
#define USART_DMA_DDRE 0x04U /* CR3.DDRE: DMA disabled on reception error */
    /**
     * @brief Gonderim, alim ve alim hatasi DMA istek politikasini ayarlar.
     *
     * DMAT ve DMAR UE acikken degisebilir; UE acikken DDRE degisikligi reddedilir.
     *
     * @param[in] instance  USART ornegi.
     * @param[in] dma_flags Istenen tam USART_DMA_* kumesi (eksik bit kapatilir).
     * @retval SPL_OK        DMA istek bitleri guncellendi.
     * @retval SPL_ERR_STATE UE acikken DDRE degisecekti.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da maske.
     */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status USART_DmaConfig(USART_TypeDef *instance, uint32_t dma_flags);

    /**
     * @brief IDLE hat algilama bayraginin anlik goruntusunu verir.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] idle     ISR.IDLE = 1 ise 1, degilse 0.
     * @retval SPL_OK        Durum yazildi.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_IdleDetected(USART_TypeDef *instance, uint8_t *idle);

    /**
     * @brief IDLE bayragini dogrudan W1C yazimiyla temizler.
     *
     * Yalniz ICR.IDLECF yazilir, donmeden once ISR geri okunur.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        Temizleme yazildi ve geri okundu.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_ClearIdle(USART_TypeDef *instance);

    /**
     * @brief Iletim tamam (TC) bayragini dogrudan W1C yazimiyla temizler.
     *
     * Yalniz ICR.TCCF yazilir, donmeden once ISR geri okunur.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        Temizleme yazildi ve geri okundu.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_ClearTxComplete(USART_TypeDef *instance);
/* Error flags (ISR bit positions) reported by USART_ErrorFlags. */
#define USART_ERRORS_ALL 0x0FU /* PE | FE | NE | ORE */
    /**
     * @brief Alinmis bir alim hata bayragi alt kumesini temizler.
     *
     * Dogrudan ICR yazimi yalniz istenen bayraklari siler; anlik goruntuden sonra gelen hata
     * kaybolmaz.
     *
     * @param[in] instance USART ornegi.
     * @param[in] errors   USART_ERRORS_ALL'in bos olmayan alt kumesi; normalde
     *                     USART_ErrorFlags'in verdigi deger.
     * @retval SPL_OK        Secili bayraklar temizlendi, ISR geri okundu.
     * @retval SPL_ERR_PARAM Gecersiz ornek, bos maske ya da desteklenmeyen bit.
     */
    /* Seviye: PARAM[S,n] */
    SPL_Status USART_ClearErrorFlags(USART_TypeDef *instance, uint32_t errors);

    /* ---- v0.16 (G4): communication modes; @see ../Docs/v0.16_USART_MODES_CONTRACT.md ----
     * Call after USART_InitEx (which rewrites CR1..CR3) and before USART_Enable: every mode setting
     * needs UE = 0 (else SPL_ERR_STATE). Instance support follows RM0440 Table 370: synchronous and
     * smartcard on USART1..3; IrDA, LIN, auto baud and receiver timeout on USART1..3/UART4/5;
     * wake-up and multiprocessor (mute) on all instances. Unsupported instance/feature combinations
     * return SPL_ERR_UNSUPPORTED. Mutually exclusive modes (RM0440 40.5.13 .. 40.5.18 "must be kept
     * cleared" lists) are checked against the current CR1..CR3: a conflict -> SPL_ERR_STATE. */

    typedef struct
    {
        uint8_t clock_out;      /* CLKEN: synchronous master, CK output */
        uint8_t cpol;           /* CPOL */
        uint8_t cpha;           /* CPHA */
        uint8_t last_bit_clock; /* LBCL: clock pulse for the last data bit */
        uint8_t slave;          /* SLVEN: synchronous slave (CK input); not with clock_out */
        uint8_t ignore_nss;     /* DIS_NSS: slave select pin ignored */
    } USART_SyncTypeDef;
    /**
     * @brief UE kapaliyken senkron saat calismasini ayarlar.
     *
     * Butun mod alanlari 0 ise asenkron calisma secilir. Yalniz USART1..3. Senkron calisma
     * LIN, smartcard, yari cift yonlu ve IrDA ile cakisir. Alanlar 0/1'dir.
     *
     * ERRATA ES0431 2.12.1 / ES0430 2.16.1 / ES0523 2.12.1: senkron slave modda dusuk hizda
     * TC son bit kaydirilmadan kurulabilir; TC'den sonra USART'i kapatmadan once CK hattinin
     * yarim saat periyodundan uzun bosta kalmasini bekleyin (cagiranin gorevi).
     *
     * @param[in] instance USART ornegi.
     * @param[in] cfg      Senkron ayarlar; cagri sirasinda okunur.
     * @retval SPL_OK              Senkron alanlar guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, NULL cfg, alan 1'den buyuk ya da
     *                             clock_out ile slave birlikte.
     * @retval SPL_ERR_STATE       UE acik ya da baska mod cakisiyor.
     * @retval SPL_ERR_UNSUPPORTED Ornekte senkron mod yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_SyncConfig(USART_TypeDef *instance, const USART_SyncTypeDef *cfg);

    typedef struct
    {
        uint8_t enable;     /* SCEN */
        uint8_t guard_time; /* GT: guard time in baud clocks */
        uint8_t prescaler;  /* PSC 1..31: smartcard clock = kernel clock / (2 x PSC) */
        uint8_t nack;       /* NACK on parity error */
        uint8_t retries;    /* SCARCNT 0..7 */
        uint8_t clock_out;  /* CLKEN: clock to the card */
    } USART_SmartcardTypeDef;
    /**
     * @brief UE kapaliyken ISO 7816 smartcard modunu ayarlar.
     *
     * Yalniz USART1/2/3. Acmak icin LIN, yari cift yonlu ve IrDA kapali; 8 veri biti +
     * parite, x16 asiri ornekleme ve 0.5 ya da 1.5 durma biti gerekir. Kapatmak SCEN, NACK
     * ve tekrar sayisini siler; GTPR ve CLKEN korunur.
     *
     * @param[in] instance USART ornegi.
     * @param[in] cfg      Smartcard ayarlari; cagri sirasinda okunur.
     * @retval SPL_OK              Smartcard ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, NULL cfg ya da alan degeri (acikken
     *                             prescaler 0 dahil).
     * @retval SPL_ERR_STATE       UE acik ya da cerceve/mod on kosullari saglanmiyor.
     * @retval SPL_ERR_UNSUPPORTED Ornekte smartcard modu yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_SmartcardConfig(USART_TypeDef *instance, const USART_SmartcardTypeDef *cfg);

    /**
     * @brief UE kapaliyken IrDA normal ya da dusuk guc modunu ayarlar.
     *
     * Yalniz USART/UART ornekleri. GTPR.GT korunur. Acmak icin tek durma biti, x16 asiri
     * ornekleme; LIN, senkron, smartcard ve yari cift yonlu kapali olmali.
     *
     * @param[in] instance  USART ornegi.
     * @param[in] enable    1: IREN = 1.
     * @param[in] low_power 1: IRLP = 1.
     * @param[in] prescaler GTPR.PSC, acikken 1..255; kapatirken kullanilmaz.
     * @retval SPL_OK              IrDA ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz arguman ya da acikken prescaler 0.
     * @retval SPL_ERR_STATE       UE acik ya da baska mod / cerceve ayari cakisiyor.
     * @retval SPL_ERR_UNSUPPORTED Ornekte IrDA modu yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_IrdaConfig(USART_TypeDef *instance, uint8_t enable, uint8_t low_power,
                                uint8_t prescaler);

    /**
     * @brief UE kapaliyken LIN modunu ve break algilamayi ayarlar.
     *
     * Yalniz USART/UART ornekleri. Acmak icin tek durma biti, 8 bit x16 calisma; senkron,
     * smartcard, yari cift yonlu ve IrDA kapali olmali.
     *
     * @param[in] instance    USART ornegi.
     * @param[in] enable      1: LINEN = 1.
     * @param[in] break_11bit 1: 11 bit, 0: 10 bit break algilama (LBDL).
     * @param[in] break_irq   1: LBDIE = 1.
     * @retval SPL_OK              LIN ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz arguman.
     * @retval SPL_ERR_STATE       UE acik ya da baska mod / cerceve ayari cakisiyor.
     * @retval SPL_ERR_UNSUPPORTED Ornekte LIN modu yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_LinConfig(USART_TypeDef *instance, uint8_t enable, uint8_t break_11bit,
                               uint8_t break_irq);

    /**
     * @brief Break karakteri gonderimi ister.
     *
     * LIN modunda break 13 bittir. UE ve TE acik olmali. Tek istek yazar, bitmesini
     * beklemez.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        RQR.SBKRQ yazildi.
     * @retval SPL_BUSY      Onceki break istegi hala bekliyor (SBKF = 1).
     * @retval SPL_ERR_STATE UE ya da TE kapali.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] STATE[S] BUSY[r] */
    SPL_Status USART_SendBreak(USART_TypeDef *instance);

    /**
     * @brief UE kapaliyken otomatik hiz algilamayi ayarlar.
     *
     * Yalniz USART/UART ornekleri.
     *
     * ERRATA ES0431 2.12.3 / ES0430 2.16.3 / ES0523 2.12.3: alim sirasinda ABREN'i temizlemek
     * veriyi bozar; burada ABREN yalniz UE = 0 iken degisir (aksi halde STATE), bu yuzden alim
     * sirasinda hic temizlenmez.
     *
     * @param[in] instance USART ornegi.
     * @param[in] enable   1: ABREN = 1.
     * @param[in] mode     ABRMODE: 0 baslangic biti, 1 dusen kenar, 2 0x7F cerceve,
     *                     3 0x55 cerceve.
     * @retval SPL_OK              Otomatik hiz ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, enable ya da mode.
     * @retval SPL_ERR_STATE       UE acik.
     * @retval SPL_ERR_UNSUPPORTED Ornekte otomatik hiz algilama yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_AutoBaudConfig(USART_TypeDef *instance, uint8_t enable, uint8_t mode);

    /**
     * @brief Acik otomatik hiz algilama dongusunu yeniden baslatir.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK              RQR.ABRRQ yazildi.
     * @retval SPL_ERR_STATE       Otomatik hiz algilama kapali (USART_AutoBaudConfig).
     * @retval SPL_ERR_UNSUPPORTED Ornekte otomatik hiz algilama yok.
     * @retval SPL_ERR_PARAM       Gecersiz ornek.
     */
    /* Seviye: PARAM[S] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_AutoBaudRequest(USART_TypeDef *instance);

    /**
     * @brief Otomatik hiz algilamanin tamamlanma ve hata durumunun anlik goruntusunu verir.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] done     ABRF (0/1).
     * @param[out] error    ABRE (0/1).
     * @retval SPL_OK              Iki cikti yazildi.
     * @retval SPL_ERR_UNSUPPORTED Ornekte otomatik hiz algilama yok.
     * @retval SPL_ERR_PARAM       Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] UNSUPPORTED[S] */
    SPL_Status USART_AutoBaudStatus(USART_TypeDef *instance, uint8_t *done, uint8_t *error);

    /**
     * @brief UE kapaliyken Stop modundan uyandirmayi ayarlar.
     *
     * G4'teki butun USART/UART/LPUART orneklerinde vardir.
     *
     * @param[in] instance USART ornegi.
     * @param[in] enable   1: UESM = 1.
     * @param[in] source   WUS: 0 adres eslesmesi, 2 baslangic biti, 3 RXNE; 1 ayrilmis.
     * @param[in] irq      1: WUFIE = 1.
     * @retval SPL_OK              Uyandirma ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, mantiksal deger ya da kaynak.
     * @retval SPL_ERR_STATE       UE acik.
     * @retval SPL_ERR_UNSUPPORTED Ornekte Stop modundan uyandirma yok.
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_WakeupConfig(USART_TypeDef *instance, uint8_t enable, uint8_t source,
                                  uint8_t irq);

    /**
     * @brief UE kapaliyken alici zaman asimini ve smartcard blok uzunlugunu ayarlar.
     *
     * Yalniz USART/UART ornekleri. RTOF/EOBF USART_ModeFlags ile izlenir.
     *
     * @param[in] instance     USART ornegi.
     * @param[in] enable       1: RTOEN = 1.
     * @param[in] timeout      Bit zamani cinsinden, 24 bit ile sinirli.
     * @param[in] block_length RTOR.BLEN.
     * @retval SPL_OK              Zaman asimi ayarlari guncellendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, enable ya da timeout.
     * @retval SPL_ERR_STATE       UE acik.
     * @retval SPL_ERR_UNSUPPORTED Ornekte alici zaman asimi yok (LPUART1).
     */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status USART_RxTimeoutConfig(USART_TypeDef *instance, uint8_t enable, uint32_t timeout,
                                     uint8_t block_length);

    /**
     * @brief UE kapaliyken cok islemcili sessiz modu ve adres algilamayi ayarlar.
     *
     * @param[in] instance     USART ornegi.
     * @param[in] enable       1: MME = 1.
     * @param[in] address_mark 0: bos hat, 1: adres isareti ile uyanma (WAKE).
     * @param[in] address      CR2.ADD; 4 bit modda <= 15.
     * @param[in] addr7        1: 7 bit adres karsilastirma (ADDM7), 0: 4 bit.
     * @retval SPL_OK        Sessiz mod ve adres ayarlari guncellendi.
     * @retval SPL_ERR_PARAM Gecersiz ornek, mantiksal deger ya da adres.
     * @retval SPL_ERR_STATE UE acik.
     */
    /* Seviye: PARAM[S,n] STATE[S] */
    SPL_Status USART_MuteConfig(USART_TypeDef *instance, uint8_t enable, uint8_t address_mark,
                                uint8_t address, uint8_t addr7);

    /**
     * @brief Sessiz moda giris ister.
     *
     * Sessiz mod once USART_MuteConfig ile acilmis olmali. RWU USART_ModeFlags ile izlenir.
     *
     * @param[in] instance USART ornegi.
     * @retval SPL_OK        RQR.MMRQ yazildi.
     * @retval SPL_ERR_STATE Cok islemcili sessiz mod kapali.
     * @retval SPL_ERR_PARAM Gecersiz ornek.
     */
    /* Seviye: PARAM[S] STATE[S] */
    SPL_Status USART_EnterMute(USART_TypeDef *instance);

/* ISR flags of the modes above, and the subset with a same-position ICR clear bit. */
#define USART_MODE_FLAGS_CLEARABLE                                                                 \
    (USART_ISR_LBDF | USART_ISR_CMF | USART_ISR_WUF | USART_ISR_RTOF | USART_ISR_EOBF |            \
     USART_ISR_UDR)
#define USART_MODE_FLAGS                                                                           \
    (USART_MODE_FLAGS_CLEARABLE | USART_ISR_ABRE | USART_ISR_ABRF | USART_ISR_SBKF |               \
     USART_ISR_RWU | USART_ISR_TCBGT)
    /**
     * @brief Genisletilmis iletisim modlarina ait durum bayraklarinin anlik goruntusunu verir.
     *
     * @param[in]  instance USART ornegi.
     * @param[out] flags    ISR & USART_MODE_FLAGS.
     * @retval SPL_OK        Goruntu yazildi.
     * @retval SPL_ERR_PARAM Gecersiz ornek ya da NULL cikti.
     */
    /* Seviye: PARAM[S] */
    SPL_Status USART_ModeFlags(USART_TypeDef *instance, uint32_t *flags);

    /**
     * @brief Secili genisletilmis mod bayraklarini dogrudan W1C yazimiyla temizler.
     *
     * ICR bir kez yazilir, donmeden once ISR geri okunur. LBDF/RTOF/EOBF USART/UART'ta, UDR
     * yalniz USART1..3'te, CMF/WUF butun orneklerde vardir (RM0440 Tablo 370).
     *
     * @param[in] instance USART ornegi.
     * @param[in] flags    USART_MODE_FLAGS_CLEARABLE'in bos olmayan, ornekte bulunan alt
     *                     kumesi.
     * @retval SPL_OK              Secili bayraklar temizlendi.
     * @retval SPL_ERR_PARAM       Gecersiz ornek, bos maske ya da temizlenemeyen bit.
     * @retval SPL_ERR_UNSUPPORTED Istenen bayrak ornekte yok.
     */
    /* Seviye: PARAM[S,n] UNSUPPORTED[S] */
    SPL_Status USART_ClearModeFlags(USART_TypeDef *instance, uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
