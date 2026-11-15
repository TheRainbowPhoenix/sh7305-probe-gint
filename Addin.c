// SPDX-FileCopyrightText: 2026 Artem Novak
// SPDX-License-Identifier: BSD-3-Clause

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/clock.h>
#include <gint/mpu/cpg.h>
#include <gint/mmu.h>
#include <gint/gint.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROM_SIZE 0x180000u
#define ROM_MID  0x080000u
#define ROM_LAST (ROM_SIZE - 1u)
#define ROM_PATH "/romprobe.bin"
#define RESULTS_PATH "/results.txt" /* OS: \\fls0\\results.txt */

/* The payload is an external file, never linked into the HH3 image.
   NTOS_SH4 shows that SH7305 can enable and program the UTLB; inspect
   live MMUCR. Do not install mappings or mutate OS/gint MMU state. */
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

/* MMUCR.AT is bit 0 (see NTOS_SH4/src/mmu.hpp).
   P1/P2 are unmapped aliases regardless of AT. For P0 use gint's
   UTLB lookup only while translation is enabled. */
static int translate_any(uint32_t va, int at_enabled,
    uint32_t *pa, uint32_t *page_size)
{
    if(va >= 0x80000000u && va < 0xc0000000u) {
        *pa = va & 0x1fffffffu;
        *page_size = 0;
        return 1;
    }
    *pa = 0xffffffffu;
    *page_size = 0;
    if(!at_enabled || va >= 0x80000000u) return 0;

    static const uint32_t sizes[] = {1024, 4096, 65536, 1048576};
    for(unsigned int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        uint32_t base = va & ~(sizes[i] - 1u);
        uint32_t pg = 0;
        uint32_t pbase = mmu_translate(base, &pg);
        if(pbase == 0xffffffffu || pg == 0 || (va - base) >= pg)
            continue;
        *pa = pbase + va - base;
        *page_size = pg;
        return 1;
    }
    return 0;
}

static void next_page(void)
{
    dupdate();
    getkey();
}

static int check_rom(rom_result_t *r)
{
    int fd = open(ROM_PATH, O_RDONLY);
    if(fd < 0) return -1;
    r->opened = 1;

    off_t length = lseek(fd, 0, SEEK_END);
    r->size_ok = (length == (off_t)ROM_SIZE);
    if(!r->size_ok || lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return -1;
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
    return 0;
}

/* Called through gint_world_switch: OS filesystem access needs OS context. */
static int write_report(char const *text)
{
    int fd = open(RESULTS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) return -1;
    size_t remaining = strlen(text);
    while(remaining) {
        ssize_t n = write(fd, text, remaining);
        if(n <= 0) {
            close(fd);
            return -2;
        }
        text += n;
        remaining -= (size_t)n;
    }
    return close(fd) == 0 ? 0 : -3;
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

    uint32_t mmucr = reg32(0xff000010);
    int at_enabled = (mmucr & 1u) != 0;
    uint32_t ram_pa = 0xffffffffu, ram_pg = 0;
    int ram_ok = translate_any(0x08100000u, at_enabled, &ram_pa, &ram_pg);
    uint32_t ram_size = mmu_uram_size();
    uint32_t ram_p1 = (uint32_t)mmu_uram();

    uint32_t code_va = (uint32_t)&main;
    uint32_t code_pa = 0xffffffffu, code_pg = 0;
    int code_ok = translate_any(code_va, at_enabled, &code_pa, &code_pg);
    uint32_t code_p1 = code_ok ? (code_pa | 0x80000000u) : 0xffffffffu;
    uint32_t va_word = *(volatile uint32_t const *)code_va;
    uint32_t p1_word = code_ok
        ? *(volatile uint32_t const *)code_p1 : 0;

    rom_result_t rom = {0};
    gint_world_switch(GINT_CALL(check_rom, &rom));

    /* Always emit all measured values, including failed/missing payloads.
       Overwrite the previous report to keep one easy-to-find log. */
    char report[1536];
    int length = snprintf(report, sizeof(report),
        "SH7305 PROBE RESULTS\\n"
        "====================\\n"
        "CPU\\n"
        "PVR: %08X\\nPRR: %08X\\nCVR: %08X\\nCPID: %08X\\n"
        "\\nUSER RAM / MMU\\n"
        "MMUCR: %08X\\nAT: %s\\n"
        "RAM VA: 08100000\\nRAM PA: %08X\\nRAM P1: %08X\\n"
        "RAM SIZE: %u\\nRAM PAGE: %u\\nRAM MAP: %s\\n"
        "\\nCLOCKS\\n"
        "FRQCR: %08X\\nPLLCR: %08X\\nFLLFRQ: %08X\\n"
        "Iphi: %u\\nBphi: %u\\nPphi: %u\\n"
        "\\nCODE MAP\\n"
        "VA: %08X\\nPA: %08X\\nP1: %08X\\nPAGE: %u\\n"
        "MAPPED: %s\\nVA WORD: %08X\\nP1 WORD: %08X\\nMATCH: %s\\n"
        "\\nEXTERNAL ROM PROBE\\n"
        "PATH: /romprobe.bin\\nSIZE EXPECTED: %u\\n"
        "OPEN: %s\\nSIZE OK: %s\\nBYTES READ: %u\\n"
        "START 0x000000 (5A): %s\\n"
        "MID   0x080000 (A5): %s\\n"
        "END   0x17FFFF (3C): %s\\n"
        "FIRST BAD OFFSET: %08X\\nDATA: %s\\n"
        "NOTE: file integrity does not establish physical ROM contiguity.\\n",
        pvr, prr, cvr, cpidr,
        mmucr, at_enabled ? "ON" : "OFF",
        ram_pa, ram_p1, ram_size, ram_pg, ram_ok ? "YES" : "NO",
        frqcr, pllcr, fllfrq, f->Iphi_f, f->Bphi_f, f->Pphi_f,
        code_va, code_pa, code_p1, code_pg,
        code_ok ? "YES" : "NO", va_word, p1_word,
        code_ok && va_word == p1_word ? "YES" : "NO",
        ROM_SIZE, rom.opened ? "YES" : "NO",
        rom.size_ok ? "YES" : "NO", rom.read_bytes,
        rom.markers[0] ? "YES" : "NO",
        rom.markers[1] ? "YES" : "NO",
        rom.markers[2] ? "YES" : "NO",
        rom.first_bad, rom.complete ? "PASS" : "FAIL");
    int save_status = -4;
    if(length >= 0 && (size_t)length < sizeof(report))
        save_status = gint_world_switch(GINT_CALL(write_report, report));

    title("SH7305 CPU");
    dprint(1, 13, C_BLACK, "PVR %08X", pvr);
    dprint(1, 23, C_BLACK, "PRR %08X", prr);
    dprint(1, 33, C_BLACK, "CVR %08X", cvr);
    dprint(1, 43, C_BLACK, "CPID %08X", cpidr);
    next_page();

    title("USER RAM / MMU");
    dprint(1, 13, C_BLACK, "MMUCR %08X", mmucr);
    dprint(1, 23, C_BLACK, "AT %s PA %08X",
        at_enabled ? "ON" : "OFF", ram_pa);
    dprint(1, 33, C_BLACK, "P1 %08X", ram_p1);
    dprint(1, 43, C_BLACK, "SIZE %u", ram_size);
    dprint(1, 53, C_BLACK, "PG %u %s", ram_pg,
        ram_ok ? "OK" : "NONE");
    next_page();

    title("SH7305 CLOCKS");
    dprint(1, 13, C_BLACK, "FRQ %08X", frqcr);
    dprint(1, 23, C_BLACK, "PLL %08X", pllcr);
    dprint(1, 33, C_BLACK, "FLL %08X", fllfrq);
    dprint(1, 43, C_BLACK, "I %u", f->Iphi_f);
    dprint(1, 53, C_BLACK, "B%u P%u", f->Bphi_f, f->Pphi_f);
    next_page();

    title("CODE MAP");
    dprint(1, 13, C_BLACK, "VA %08X", code_va);
    dprint(1, 25, C_BLACK, "PA %08X", code_pa);
    dprint(1, 37, C_BLACK, "P1 %08X", code_p1);
    dprint(1, 49, C_BLACK, "PG %u", code_pg);
    next_page();

    title("P1 CODE TEST");
    dprint(1, 15, C_BLACK, "VA %08X", va_word);
    dprint(1, 29, C_BLACK, "P1 %08X", p1_word);
    dprint(1, 45, C_BLACK, "MATCH %s",
        code_ok && va_word == p1_word ? "YES" : "NO");
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

    title("RESULTS FILE");
    dtext(1, 13, C_BLACK, "/results.txt");
    dtext(1, 25, C_BLACK, "OS: \\fls0\\results.txt");
    dprint(1, 43, C_BLACK, "SAVE %s (%d)",
        save_status == 0 ? "OK" : "FAILED", save_status);
    next_page();

    return 1;
}
