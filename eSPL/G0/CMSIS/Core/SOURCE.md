# CMSIS Core(M) (third party)

Source: ST `cmsis_core` (copy of Arm CMSIS_5; latest pdsc version 5.9.0, `cmsis_version.h`
Core(M) 5.6), taken from a local copy of that package on 2026-11-11. Files are unmodified.
License: Apache 2.0 (in each file header; `LICENSE.md`).

Why: the Keil CMSIS pack installed on the development PC was 4.2.0 / 4.3.0, too old for
cmsis_device_g0 v1.4.4. The Arm target build (clang `--target=arm-none-eabi -mcpu=cortex-m0plus`)
and the Keil projects use this folder and do not depend on the RTE CMSIS component.

| File | SHA-256 |
|---|---|
| `Include/cachel1_armv7.h` | `2e336a64f60a9c0eaf5a08e2c4845d94da86c3fd84d219d0d0c9f530e63bc9c3` |
| `Include/cmsis_armcc.h` | `5bc0d7ed8e36e9bcb868c4042fff350c702eca305df4162fb00384789e35ee1c` |
| `Include/cmsis_armclang.h` | `d5da2b48204f0d0cf1ff38ffccb527e5706cd0a091ff0a331eb41d6596c33fe5` |
| `Include/cmsis_armclang_ltm.h` | `3e8ffb284f4f155de3ea93841cca853b41d5e812b53e52c64c3d4a67b7f1ec7c` |
| `Include/cmsis_compiler.h` | `b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8` |
| `Include/cmsis_gcc.h` | `43bfd1fe69fbbc2ca70aaf7d43cc3e20f16e0c7f66e1bdef9b51af6dc14b2617` |
| `Include/cmsis_iccarm.h` | `2d255c8429e507e52cb603f2970bed4cc4a6cdcc3eb7fc7997385abad7364837` |
| `Include/cmsis_version.h` | `184c19fd3ee73632edf35a0b4d49cd48be75fbf49e6ccb19d9db05fa83bea4b3` |
| `Include/core_armv81mml.h` | `a40954bccbf202bd65ee721ea688122561e005c9f9f9d18e082280c5b897d0e4` |
| `Include/core_armv8mbl.h` | `458b1e6788079ded52adc7d06383701c865ca97b67c5e7a0a44884194f91a842` |
| `Include/core_armv8mml.h` | `5a68204fa31cda7d04d41c9ceaf3f4b1217908f59366f757ee778181c7cc8cb9` |
| `Include/core_cm0.h` | `ac13cb645f108666a0e57c93467872660d6bc860a839f840dc870b7a5863b4bf` |
| `Include/core_cm0plus.h` | `a09fa9c6b8b21d1920bce15b98aa56182b027e9278cca0b154812bac2aea3cfa` |
| `Include/core_cm1.h` | `944bf0c4f940eef30a632cd99d5dcfe8270e8a8431eb98c0f081bcf4f61f728e` |
| `Include/core_cm23.h` | `77822e9738885394ec52fca6f617452b823a0b55e2664ee225e65576cf0e8f2d` |
| `Include/core_cm3.h` | `c9937781259a8897adf514445441c68570087d6a72a558d8a4afe75cdb62dbd2` |
| `Include/core_cm33.h` | `225c47c36230edaa314f1e7a45b38279f6ab4d58faeb132226db35f7007d58aa` |
| `Include/core_cm35p.h` | `206b427589981b78169c1d9b73e898e4e8de4efbd76fd0c2df7df81d645b9fa2` |
| `Include/core_cm4.h` | `f5b63d52dd1557b15ca414cb59c264f595a7b5669db2a5878dffe22f4caedc8c` |
| `Include/core_cm55.h` | `5707f716cf03f7e80151108ae94601fcfdf362dd2d830876040213318f6dee3a` |
| `Include/core_cm7.h` | `e974d0a91752487d0154457cd422c37667328e9ddb7ba4fa2fed479a63577f63` |
| `Include/core_cm85.h` | `75bc123889f0f5ca33c4d16b6cbdd580a3572663c899fadb2c8e28f6a5e6fead` |
| `Include/core_sc000.h` | `c81377a69a4ea8ecaead42194f2ce8ab59bd4ee7bd638e423f5b1f5ec283d154` |
| `Include/core_sc300.h` | `73dd461c12c7d3a693840df3084fd8f85122095c6f21fa3d779b0f3eceaebad5` |
| `Include/core_starmc1.h` | `92d8661f2947d00da91653d04794cde0593fdaafb3438842a0477775716aafc5` |
| `Include/mpu_armv7.h` | `29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372` |
| `Include/mpu_armv8.h` | `2c7d22a344cedcc1e35380d1f76f0e6c55da5e3101e6a41fd85497d1841fe8c9` |
| `Include/pac_armv81.h` | `d76f5cc0c59a59e035115f700b8e8d95b7b98ecb99a2924e885f82a94903d984` |
| `Include/pmu_armv8.h` | `cea8c105c5f41ba831fe4d9e8e39758068cd4c414383348dc2835d6be447cca0` |
| `Include/tz_context.h` | `45565c8cb04c653aabaf3cceddfc848b6a0de28c20d4f7e3d475a992c7eee788` |
