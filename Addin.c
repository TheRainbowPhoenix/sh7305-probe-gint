// SPDX-FileCopyrightText: 2026 Artem Novak
// SPDX-License-Identifier: BSD-3-Clause

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/clock.h>
#include <gint/mpu/cpg.h>

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#define ROM_SIZE 0x180000u
#define ROM_MID  0x080000u
#define ROM_LAST (ROM_SIZE - 1u)
#define ROM_PATH "/romprobe.bin"

/* fxCP/HH3 runs from physical RAM (P1), without an active UTLB.
   The 1.5 MiB test data must not be linked into this executable. */
static uint8_t read_buffer[4096];

typedef struct {
    int opened;
    int size_ok;
    int complete;
    int markers[3];
    uint32_t read_bytes;
    uint32_t first_bad;
} rom_result_t;

static inline uint32_t reg32(uint32_t address)
{
    return *(volatile uint32_t const *)address;
}

static void next_page(void)
{
    dupdate();
    getkey();
}

static void check_rom(rom_result_t *r)
{
    int fd = open(ROM_PATH, O_RDONLY);
    if(fd < 0) return;
    r->opened = 1;

    off_t length = lseek(fd, 0, SEEK_END);
    r->size_ok = (length == (off_t)ROM_SIZE);
    if(!r->size_ok || lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return;
    }

    r->first_bad = 0xffffffffu;
    while(r->read_bytes < ROM_SIZE) {
        unsigned int count = ROM_SIZE - r->read_bytes;
        if(count > sizeof(read_buffer)) count = sizeof(read_buffer);
        ssize_t n = read(fd, read_buffer, count);
        if(n <= 0) break;

        for(ssize_t i = 0; i < n; i++) {
            uint32_t offset = r->read_bytes + (uint32_t)i;
            uint8_t expected = 0;
            if(offset == 0) expected = 0x5a;
            if(offset == ROM_MID) expected = 0xa5;
            if(offset == ROM_LAST) expected = 0x3c;
            if(read_buffer[i] != expected && r->first_bad == 0xffffffffu)
                r->first_bad = offset;
            if(offset == 0) r->markers[0] = (read_buffer[i] == 0x5a);
            if(offset == ROM_MID) r->markers[1] = (read_buffer[i] == 0xa5);
            if(offset == ROM_LAST) r->markers[2] = (read_buffer[i] == 0x3c);
        }
        r->read_bytes += (uint32_t)n;
    }

    r->complete = (r->read_bytes == ROM_SIZE &&
                   r->first_bad == 0xffffffffu);
    close(fd);
}

static void title(char const *name)
{
    dclear(C_WHITE);
    dtext(1, 1, C_BLACK, name);
}

int main(void)
{
    uint32_t pvr = reg32(0xff000030);
    uint32_t cvr = reg32(0xff000040);
    uint32_t prr = reg32(0xff000044);
    uint32_t cpidr = reg32(0xff000048);

    uint32_t frqcr = SH7305_CPG.FRQCR.lword;
    uint32_t pllcr = SH7305_CPG.PLLCR.lword;
    uint32_t fllfrq = SH7305_CPG.FLLFRQ.lword;
    clock_frequency_t const *f = clock_freq();

    uint32_t code_va = (uint32_t)&main;
    int direct = (code_va >= 0x80000000u && code_va < 0xc0000000u);
    uint32_t code_pa = direct ? (code_va & 0x1fffffffu) : 0xffffffffu;
    uint32_t code_p1 = direct ? (code_pa | 0x80000000u) : 0xffffffffu;
    uint32_t va_word = *(volatile uint32_t const *)code_va;
    uint32_t p1_word = direct
        ? *(volatile uint32_t const *)code_p1 : 0;

    rom_result_t rom = {0};
    check_rom(&rom);

    title("SH7305 CPU");
    dprint(1, 13, C_BLACK, "PVR %08X", pvr);
    dprint(1, 23, C_BLACK, "PRR %08X", prr);
    dprint(1, 33, C_BLACK, "CVR %08X", cvr);
    dprint(1, 43, C_BLACK, "CPID %08X", cpidr);
    next_page();

    title("USER RAM / MMU");
    dtext(1, 13, C_BLACK, "HH3: direct RAM");
    dtext(1, 23, C_BLACK, "No active UTLB mapping");
    dtext(1, 33, C_BLACK, "0x08100000: N/A");
    dtext(1, 43, C_BLACK, "MMU pages: N/A");
    next_page();

    title("SH7305 CLOCKS");
    dprint(1, 13, C_BLACK, "FRQ %08X", frqcr);
    dprint(1, 23, C_BLACK, "PLL %08X", pllcr);
    dprint(1, 33, C_BLACK, "FLL %08X", fllfrq);
    dprint(1, 43, C_BLACK, "I %u", f->Iphi_f);
    dprint(1, 53, C_BLACK, "B%u P%u", f->Bphi_f, f->Pphi_f);
    next_page();

    title("CODE MAP (DIRECT)");
    dprint(1, 13, C_BLACK, "VA %08X", code_va);
    dprint(1, 25, C_BLACK, "PA %08X", code_pa);
    dprint(1, 37, C_BLACK, "P1 %08X", code_p1);
    dtext(1, 49, C_BLACK, "UTLB PG N/A");
    next_page();

    title("P1 CODE TEST");
    dprint(1, 15, C_BLACK, "VA %08X", va_word);
    dprint(1, 29, C_BLACK, "P1 %08X", p1_word);
    dprint(1, 45, C_BLACK, "MATCH %s",
        direct && va_word == p1_word ? "YES" : "NO");
    next_page();

    title("ROM 1.5M START");
    dtext(1, 13, C_BLACK, "FILE /romprobe.bin");
    dtext(1, 25, C_BLACK, "OFFSET 00000000");
    dtext(1, 37, C_BLACK, "EXPECTED 5A");
    dprint(1, 49, C_BLACK, "MATCH %s", rom.markers[0] ? "YES" : "NO");
    next_page();

    title("ROM 1.5M MID");
    dtext(1, 13, C_BLACK, "FILE /romprobe.bin");
    dtext(1, 25, C_BLACK, "OFFSET 00080000");
    dtext(1, 37, C_BLACK, "EXPECTED A5");
    dprint(1, 49, C_BLACK, "MATCH %s", rom.markers[1] ? "YES" : "NO");
    next_page();

    title("ROM 1.5M END");
    dtext(1, 13, C_BLACK, "FILE /romprobe.bin");
    dtext(1, 25, C_BLACK, "OFFSET 0017FFFF");
    dtext(1, 37, C_BLACK, "EXPECTED 3C");
    dprint(1, 49, C_BLACK, "MATCH %s", rom.markers[2] ? "YES" : "NO");
    next_page();

    title("EXTERNAL ROM TEST");
    dprint(1, 13, C_BLACK, "SIZE %u", ROM_SIZE);
    dprint(1, 23, C_BLACK, "OPEN %s", rom.opened ? "YES" : "NO");
    dprint(1, 33, C_BLACK, "LEN %s", rom.size_ok ? "OK" : "BAD");
    dprint(1, 43, C_BLACK, "READ %u", rom.read_bytes);
    dprint(1, 53, C_BLACK, "DATA %s", rom.complete ? "PASS" : "FAIL");
    next_page();

    return 1;
}
