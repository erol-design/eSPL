/* SPDX-License-Identifier: MIT */
/* FLASH: erase the last 2 KB page of the flash (the last page of bank 2 on dual-bank devices such
 * as the STM32G0B1RE), program two 64-bit double words, read them back and print the result.
 * The page must not hold code or data still needed: this image is far smaller than the flash and
 * its linker script does not use the last page. Each run erases the page once (endurance:
 * 10 000 cycles per page, datasheet).
 * Shows: geometry query instead of hard-coded addresses, pending error flags read before they are
 * cleared, unlock -> start -> poll -> end -> lock (no driver waits: the poll and its timeout are
 * the application's), read-back verify (device-layer duty). */
#include "ex_board.h"

#define ERASE_TIMEOUT_MS 100U /* page erase 22..40 ms (datasheet tERASE) */
#define PROGRAM_TIMEOUT_MS 5U

static void WaitOperation(uint32_t timeout_ms)
{
    uint32_t t0 = Ex_Millis();
    SPL_Status status = FLASH_PollOperation((uint8_t *)0);

    while (status == SPL_BUSY)
    {
        if (Ex_Expired(t0, timeout_ms) != 0U)
        {
            Ex_Fatal(SPL_ERR_TIMEOUT, (uint32_t)__LINE__);
        }
        status = FLASH_PollOperation((uint8_t *)0);
    }
    if (status == SPL_ERR_HW)
    {
        uint32_t flags = 0U;

        EX_CHECK(FLASH_GetFlags(&flags));
        Ex_Print("flash error flags ");
        Ex_PrintHex(flags);
        Ex_Print("\r\n");
    }
    EX_CHECK(status);
    EX_CHECK(FLASH_EndOperation());
}

int main(void)
{
    static const uint64_t pattern[2] = {0x1122334455667788ULL, 0x0123456789ABCDEFULL};
    FLASH_GeometryTypeDef geo;
    uint32_t bank;
    uint32_t page;
    uint32_t address;
    uint32_t flags = 0U;
    uint32_t i;
    uint32_t errors = 0U;

    Ex_Init();
    Ex_Print("\r\nex_flash: erase / program / verify the last page\r\n");

    EX_CHECK(FLASH_GetGeometry(&geo));
    page = geo.pages_per_bank - 1U;
    if (geo.banks == 2U)
    {
        bank = FLASH_BANK_2;
        /* Physical bank 2 sits in the upper half unless the banks are swapped (nSWAP_BANK). */
        address = ((geo.swapped != 0U) ? FLASH_BASE : geo.bank2_base) + (page * FLASH_PAGE_BYTES);
    }
    else
    {
        bank = FLASH_BANK_1;
        address = FLASH_BASE + (page * FLASH_PAGE_BYTES);
    }
    Ex_Print("flash ");
    Ex_PrintU32(geo.size_kb);
    Ex_Print(" KB, banks ");
    Ex_PrintU32(geo.banks);
    Ex_Print(", page ");
    Ex_PrintU32(page);
    Ex_Print(" of bank ");
    Ex_PrintU32(bank);
    Ex_Print(" at ");
    Ex_PrintHex(address);
    Ex_Print("\r\n");

    /* Flags left by an earlier operation block a new one: show them, then clear. */
    EX_CHECK(FLASH_GetFlags(&flags));
    if ((flags & FLASH_SR_ERRORS) != 0U)
    {
        Ex_Print("old error flags ");
        Ex_PrintHex(flags);
        Ex_Print(" cleared\r\n");
        EX_CHECK(FLASH_ClearFlags(flags & FLASH_SR_CLEARABLE));
    }

    EX_WAIT(FLASH_Unlock(), 5U);
    EX_CHECK(FLASH_ErasePageStart(bank, page));
    WaitOperation(ERASE_TIMEOUT_MS);
    for (i = 0U; i < 2U; i++)
    {
        EX_CHECK(FLASH_ProgramDoubleWordStart(address + (i * 8U), pattern[i]));
        WaitOperation(PROGRAM_TIMEOUT_MS);
    }
    EX_CHECK(FLASH_Lock());

    for (i = 0U; i < 2U; i++)
    {
        uint64_t value = *(const volatile uint64_t *)(address + (i * 8U));

        if (value != pattern[i])
        {
            errors++;
        }
    }
    Ex_Print((errors == 0U) ? "verify OK\r\n" : "verify FAILED\r\n");

    for (;;)
    {
        Ex_LedToggle(); /* slow blink = OK, fast blink = failed */
        Ex_DelayMs((errors == 0U) ? 1000U : 100U);
    }
}
