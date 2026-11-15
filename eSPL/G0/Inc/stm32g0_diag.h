#ifndef STM32G0_DIAG_H
#define STM32G0_DIAG_H

/* Derleme zamani arguman denetimi (Docs/FAST_PATH.md bolum 3).
 *
 * SPL_DIAG(kosul, mesaj) bir fonksiyon bildirimine eklenir. Cagri yerinde arguman SABIT ise
 * derleyici kosulu derleme zamaninda degerlendirir; kosul dogruysa derleme HATA ile durur.
 * Arguman degiskense hicbir sey yapmaz ve hic kod uretmez: o durumda calisma ani denetimi
 * (varsa) gecerlidir.
 *
 * Neden: sabit argumanla yapilan hatalar (kanal 5, bos pin maskesi) kod yazildigi an bellidir;
 * her cagrida calisma aninda yeniden sormak yerine derleyiciye bir kez kanitlatilir.
 *
 * Sinir: yalniz skaler argumanlar. CMSIS cevre birimi isaretcileri ((TIM_TypeDef *)TIM1_BASE)
 * C'de sabit ifade sayilmaz; ornek dogrulugu bu yolla denetlenemez (kart esleme basligi ve
 * Init ile saglanir).
 *
 * Etkin oldugu yer: clang tabanli derleyicilerde (Keil AC6, clang) ve yalniz ARM hedefinde.
 * Host register-model testleri kotu sabit argumanla calisma ani yolunu sinadigi icin orada
 * kapali kalir; SPL_DIAG_OFF tanimlanirsa ARM hedefinde de kapanir. Baska derleyicide bos
 * makrodur: calisma ani davranisi degismez, yalniz derleme zamani guvencesi yoktur.
 *
 * -pedantic, diagnose_if'i "clang uzantisi" diye uyarir (-Wgcc-compat); uyari yalniz bu
 * ozniteligin cevresinde kapatilir, derleme biriminin geri kalani etkilenmez. */

#if defined(__clang__) && defined(__arm__) && !defined(SPL_DIAG_OFF)
#define SPL_DIAG(cond, msg)                                                                        \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wgcc-compat\"")          \
        __attribute__((diagnose_if((cond), msg, "error"))) _Pragma("clang diagnostic pop")
#define SPL_DIAG_ENABLED 1
#else
#define SPL_DIAG(cond, msg)
#define SPL_DIAG_ENABLED 0
#endif

#endif /* STM32G0_DIAG_H */
