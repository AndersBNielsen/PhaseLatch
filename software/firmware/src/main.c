// FX2 firmware: Internal IFCLK + Slave FIFO + AUTOIN (EP6 IN, 512 B)
// Build: SDCC + fx2lib headers

#include <fx2regs.h>
#include <fx2macros.h>
#include <syncdelay.h>
#include "config.h"
#include "fx2_helpers.h"
#include <setupdat.h>

// Uncomment to enable internal firmware pattern generator test (bypasses external producer)
//#define PATTERN_TEST 1

#define SD() SYNCDELAY; SYNCDELAY;

// ---------------- Vendor command support -----------------
// We preserve your original CPUCS bit comments EXACTLY as provided below.

// Simple vendor protocol (control endpoint):
//  OUT (Host->Device) commands expect 2 bytes: MASK, VALUE. Bits where MASK=1 are replaced.
//  IN  (Device->Host) commands return 1 byte.
//  Codes (avoid 0xA0–0xAF reserved by EZ-USB):
//    0xB0: SET CPUCS
//    0xB1: GET CPUCS
//    0xB2: SET IFCONFIG
//    0xB3: GET IFCONFIG
//    0xB4: SET PORTA (IOA)
//    0xB5: GET PORTA (IOA)
//    0xB6: SET OEA
//    0xB7: GET OEA
//    0xB8: GET EP6CS
//    0xB9: GET PINFLAGSAB
//    0xBA: GET PINFLAGSCD
//    0xBB: GET IFCONFIG snapshot (captured right after firmware init)
//    0xBC: I2C single-byte register write (OUT, no payload, wValueL=7-bit slave addr, wIndexL=register, wIndexH=value)
//    0xBD: I2C single-byte register read  (IN payload: [status, value], wValueL=7-bit slave addr, wIndexL=register)
//    0xBE: GET last I2C status
//    0xBF: I2C block write (OUT payload: [start_reg, data0, data1, ...], wValueL=7-bit slave addr)
//    0xC0: SET sample rate using SI5351 CLK2 = 2 * Fs (OUT, no payload, wValue:wIndex = 32-bit Fs in Hz)
//    0xC1: I2C block read (IN payload: [status, data...], wValueL=7-bit slave addr, wIndexL=start register, wLength=1+count)
//    0xC2: GET FX2 startup SI5351 CLK2 profile (IN payload: [reg0,val0,reg1,val1,...])
//    0xC3: GET EP2468STAT
//    0xC4: GET EP6CFG
//    0xC5: GET EP6FIFOCFG
//    0xC6: GET PORTACFG
//    0xC7: GET FIFOPINPOLAR
//    0xC8: GET EP2FIFOFLGS
//    0xC9: GET EP4FIFOFLGS
//    0xCA: GET EP6FIFOFLGS
//    0xCB: GET EP8FIFOFLGS

// Use the shared helper macro for masked writes
// FX2_MASKED_WRITE(reg, mask, value)

#define I2C_STATUS_OK         0x00
#define I2C_STATUS_TIMEOUT    0x01
#define I2C_STATUS_BERR       0x02
#define I2C_STATUS_ADDR_NACK  0x04
#define I2C_STATUS_DATA_NACK  0x08
#define I2C_STATUS_BAD_PARAM  0x10

static BYTE last_i2c_status = I2C_STATUS_OK;

static const BYTE si5351_init_data[] = {
    3, 0xF8,
    16, 0x80, 17, 0x80, 18, 0xC0, 19, 0x80,
    20, 0x80, 21, 0x80, 22, 0x80, 23, 0x80,
    26, 0x00, 27, 0x01, 28, 0x00, 29, 0x0C,
    30, 0x00, 31, 0x00, 32, 0x00, 33, 0x00,
    34, 0x00, 35, 0x01, 36, 0x00, 37, 0x06,
    38, 0x00, 39, 0x00, 40, 0x00, 41, 0x00,
    42, 0x00, 43, 0x01, 44, 0x00, 45, 0x30,
    46, 0x00, 47, 0x00, 48, 0x00, 49, 0x00,
    50, 0x00, 51, 0x01, 52, 0x00, 53, 0x30,
    54, 0x00, 55, 0x00, 56, 0x00, 57, 0x00,
    58, 0x00, 59, 0x01, 60, 0x00, 61, 0x03,
    62, 0x00, 63, 0x00, 64, 0x00, 65, 0x00,
    165, 100,
    177, 0xA0,
    3, 0xF8,
    16, 0x0F, 17, 0x0F, 18, 0x6C,
};

static const BYTE si5351_frq_30005000_pll[] = {
    0x00, 0xFA, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x80,
};

static const BYTE si5351_frq_30005000_ms[] = {
    0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
};

static const BYTE si5351_startup_clk2_profile[] = {
    3, 0xF8,
    16, 0x0F,
    17, 0x0F,
    18, 0x6C,
    34, 0x00,
    35, 0x01,
    36, 0x00,
    37, 0x06,
    38, 0x00,
    39, 0x00,
    40, 0x00,
    41, 0x00,
    58, 0x00,
    59, 0x01,
    60, 0x00,
    61, 0x03,
    62, 0x00,
    63, 0x00,
    64, 0x00,
    65, 0x00,
    165, 20,
    177, 0x20,
};

static void i2c_status_clear(void) {
    last_i2c_status = I2C_STATUS_OK;
}

static void i2c_recover_bus(void) {
    WORD timeout = 65535;

    I2CS |= bmSTOP;
    while ((I2CS & bmSTOP) && --timeout) { }

    I2CTL = 0x00;
}

static BOOL i2c_wait_done(void) {
    WORD timeout = 65535;
    while (!(I2CS & bmDONE) && --timeout) { }
    if (timeout == 0) {
        last_i2c_status |= I2C_STATUS_TIMEOUT;
        i2c_recover_bus();
        return FALSE;
    }
    if (I2CS & bmBERR) {
        last_i2c_status |= I2C_STATUS_BERR;
        i2c_recover_bus();
        return FALSE;
    }
    return TRUE;
}

static void i2c_stop_nowait(void) {
    I2CS |= bmSTOP;
}

static void si5351_delay_long(void) {
    WORD outer;
    BYTE inner;

    for (outer = 0; outer != 0x0200; ++outer) {
        for (inner = 0; inner != 0xFF; ++inner) { }
    }
}

static BOOL i2c_wait_stop(void) {
    WORD timeout = 65535;
    while ((I2CS & bmSTOP) && --timeout) { }
    if (timeout == 0) {
        last_i2c_status |= I2C_STATUS_TIMEOUT;
        return FALSE;
    }
    return TRUE;
}

static BOOL fx2_i2c_write(BYTE addr, WORD len1, BYTE *buf1, WORD len2, BYTE *buf2) {
    WORD cur_byte;
    WORD total_bytes = len1 + len2;
    BYTE retry_count = 3;

    i2c_status_clear();
    if (total_bytes == 0) {
        return TRUE;
    }

start:
    cur_byte = 0;
    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)(addr << 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    while (cur_byte < total_bytes) {
        I2DAT = (cur_byte < len1) ? buf1[cur_byte] : buf2[cur_byte - len1];
        ++cur_byte;

        if (!i2c_wait_done()) {
            if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
            i2c_stop_nowait();
            i2c_wait_stop();
            return FALSE;
        }

        if (!(I2CS & bmACK)) {
            last_i2c_status |= I2C_STATUS_DATA_NACK;
            i2c_stop_nowait();
            i2c_wait_stop();
            return FALSE;
        }
    }

    i2c_stop_nowait();
    return i2c_wait_stop();
}

static BOOL fx2_i2c_read(BYTE addr, WORD len, BYTE *buf) {
    BYTE tmp;
    WORD cur_byte;
    BYTE retry_count = 3;

    i2c_status_clear();
    if (len == 0) {
        return TRUE;
    }

start:
    cur_byte = 0;
    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)((addr << 1) | 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    if (len == 1) {
        I2CS |= bmLASTRD;
    }

    tmp = I2DAT;
    (void)tmp;

    while (len > cur_byte + 1) {
        if (!i2c_wait_done()) {
            if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
            i2c_stop_nowait();
            i2c_wait_stop();
            return FALSE;
        }

        if (len == cur_byte + 2) {
            I2CS |= bmLASTRD;
        }

        buf[cur_byte++] = I2DAT;
    }

    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    i2c_stop_nowait();
    buf[cur_byte] = I2DAT;
    return i2c_wait_stop();
}

static BOOL fx2_i2c_write_reg(BYTE addr, BYTE reg, BYTE value) {
    BYTE reg_buf[1];
    BYTE val_buf[1];
    reg_buf[0] = reg;
    val_buf[0] = value;
    return fx2_i2c_write(addr, 1, reg_buf, 1, val_buf);
}

static BOOL fx2_i2c_read_reg_once(BYTE addr, BYTE reg, BYTE *value) {
    BYTE tmp;
    BYTE retry_count = 3;

    i2c_status_clear();

start:
    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)(addr << 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    I2DAT = reg;
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_DATA_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)((addr << 1) | 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    I2CS |= bmLASTRD;
    tmp = I2DAT;
    (void)tmp;

    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    i2c_stop_nowait();
    *value = I2DAT;
    return i2c_wait_stop();
}

static BOOL fx2_i2c_read_reg(BYTE addr, BYTE reg, BYTE *value) {
    BYTE discard;

    if (!fx2_i2c_read_reg_once(addr, reg, &discard)) {
        return FALSE;
    }

    return fx2_i2c_read_reg_once(addr, reg, value);
}

static BOOL fx2_i2c_read_block_reg(BYTE addr, BYTE reg, BYTE len, BYTE *buf) {
    BYTE tmp;
    BYTE cur_byte;
    BYTE retry_count = 3;

    i2c_status_clear();
    if (len == 0) {
        return TRUE;
    }

start:
    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)(addr << 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    I2DAT = reg;
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_DATA_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    I2CS |= bmSTART;
    if (I2CS & bmBERR) {
        i2c_recover_bus();
        if (--retry_count == 0) {
            last_i2c_status |= I2C_STATUS_BERR;
            return FALSE;
        }
        goto start;
    }

    I2DAT = (BYTE)((addr << 1) | 1);
    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }
    if (!(I2CS & bmACK)) {
        last_i2c_status |= I2C_STATUS_ADDR_NACK;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    if (len == 1) {
        I2CS |= bmLASTRD;
    }

    tmp = I2DAT;
    (void)tmp;
    cur_byte = 0;

    while (len > cur_byte + 1) {
        if (!i2c_wait_done()) {
            if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
            i2c_stop_nowait();
            i2c_wait_stop();
            return FALSE;
        }

        if (len == cur_byte + 2) {
            I2CS |= bmLASTRD;
        }

        buf[cur_byte++] = I2DAT;
    }

    if (!i2c_wait_done()) {
        if ((last_i2c_status & I2C_STATUS_BERR) && --retry_count) goto start;
        i2c_stop_nowait();
        i2c_wait_stop();
        return FALSE;
    }

    i2c_stop_nowait();
    buf[cur_byte] = I2DAT;
    return i2c_wait_stop();
}

static BOOL si5351_write_pairs(const BYTE *pairs, BYTE pair_count) {
    BYTE idx;

    for (idx = 0; idx != pair_count; ++idx) {
        if (!fx2_i2c_write_reg(0x60, pairs[(WORD)idx * 2], pairs[(WORD)idx * 2 + 1])) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL si5351_profile_matches(const BYTE *pairs, BYTE pair_count) {
    BYTE idx;
    BYTE value;

    for (idx = 0; idx != pair_count; ++idx) {
        if (!fx2_i2c_read_reg(0x60, pairs[(WORD)idx * 2], &value)) {
            return FALSE;
        }
        if (value != pairs[(WORD)idx * 2 + 1]) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL si5351_program_default_profile(void) {
    BYTE idx;

    if (si5351_profile_matches(si5351_startup_clk2_profile, (BYTE)(sizeof(si5351_startup_clk2_profile) / 2))) {
        return TRUE;
    }

    if (!si5351_write_pairs(si5351_init_data, (BYTE)(sizeof(si5351_init_data) / 2))) return FALSE;
    if (!fx2_i2c_write_reg(0x60, 3, 0xFF)) return FALSE;

    for (idx = 0; idx != 8; ++idx) {
        if (!fx2_i2c_write_reg(0x60, (BYTE)(26 + idx), si5351_frq_30005000_pll[idx])) return FALSE;
    }

    for (idx = 0; idx != 16; ++idx) {
        if (!fx2_i2c_write_reg(0x60, (BYTE)(42 + idx), si5351_frq_30005000_ms[idx & 7])) return FALSE;
    }

    if (!fx2_i2c_write_reg(0x60, 165, 20)) return FALSE;
    si5351_delay_long();
    if (!fx2_i2c_write_reg(0x60, 177, 0x20)) return FALSE;
    si5351_delay_long();
    if (!fx2_i2c_write_reg(0x60, 3, 0xF8)) return FALSE;

    return TRUE;
}

static BOOL si5351_write_clk2_integer_sample_rate(DWORD sample_hz) {
    static BYTE ms2_regs[8];
    static BYTE pllb_regs[8];
    static BYTE reg34 = 34;
    static BYTE reg58 = 58;
    BYTE enable_reg;
    DWORD clk2_hz;
    DWORD pll_mult;
    DWORD pll_hz;
    DWORD clk2_full;
    DWORD divider;
    DWORD p1;

    i2c_status_clear();

    if (sample_hz == 0UL) {
        last_i2c_status |= I2C_STATUS_BAD_PARAM;
        return FALSE;
    }

    clk2_full = sample_hz * 2UL;
    if (clk2_full < 1UL) {
        last_i2c_status |= I2C_STATUS_BAD_PARAM;
        return FALSE;
    }
    clk2_hz = clk2_full;

    pll_mult = 24UL;
    divider = 0UL;
    while (pll_mult <= 36UL) {
        pll_hz = 25000000UL * pll_mult;
        if ((pll_hz % clk2_hz) == 0UL) {
            divider = pll_hz / clk2_hz;
            if (divider >= 8UL && divider <= 900UL) {
                break;
            }
        }
        ++pll_mult;
    }

    if (pll_mult > 36UL) {
        last_i2c_status |= I2C_STATUS_BAD_PARAM;
        return FALSE;
    }

    p1 = 128UL * pll_mult - 512UL;
    pllb_regs[0] = 0x00;
    pllb_regs[1] = 0x01;
    pllb_regs[2] = 0x00;
    pllb_regs[3] = (BYTE)(p1 >> 8);
    pllb_regs[4] = (BYTE)p1;
    pllb_regs[5] = 0x00;
    pllb_regs[6] = 0x00;
    pllb_regs[7] = 0x00;

    p1 = 128UL * divider - 512UL;
    ms2_regs[0] = 0x00;
    ms2_regs[1] = 0x01;
    ms2_regs[2] = 0x00;
    ms2_regs[3] = (BYTE)(p1 >> 8);
    ms2_regs[4] = (BYTE)p1;
    ms2_regs[5] = 0x00;
    ms2_regs[6] = 0x00;
    ms2_regs[7] = 0x00;

    if (!fx2_i2c_write(0x60, 1, &reg34, 8, pllb_regs)) return FALSE;
    if (!fx2_i2c_write(0x60, 1, &reg58, 8, ms2_regs)) return FALSE;
    if (!fx2_i2c_write_reg(0x60, 18, 0x6C)) return FALSE;
    si5351_delay_long();
    if (!fx2_i2c_write_reg(0x60, 177, 0x80)) return FALSE;
    si5351_delay_long();
    if (!fx2_i2c_read_reg(0x60, 3, &enable_reg)) return FALSE;
    if (!fx2_i2c_write_reg(0x60, 3, (BYTE)(enable_reg & (BYTE)~0x04))) return FALSE;

    return TRUE;
}

// Handle vendor commands (called by fx2lib's handle_setupdata flow)
// fx2lib will call this for vendor commands (weak symbol override)
BOOL handle_vendorcommand(BYTE cmd) {
    BYTE slave_addr;
    BYTE reg_addr;
    BYTE value;
    BYTE count;
    DWORD sample_hz;

    switch(cmd) {
        case 0xB0: // SET CPUCS
        case 0xB2: // SET IFCONFIG
        case 0xB4: // SET PORTA
        case 0xB6: // SET OEA
        {
            // Expect 2 OUT bytes
            if(EP0BCL != 2) return FALSE; // not enough data yet
            BYTE mask = EP0BUF[0];
            BYTE val  = EP0BUF[1];
                if(cmd == 0xB0) FX2_MASKED_WRITE(CPUCS, mask, val);
                else if(cmd == 0xB2) FX2_MASKED_WRITE(IFCONFIG, mask, val);
                else if(cmd == 0xB4) FX2_MASKED_WRITE(IOA, mask, val);
                else FX2_MASKED_WRITE(OEA, mask, val);
            return TRUE;
        }
            case 0xB1: // GET CPUCS
            EP0BUF[0] = CPUCS; EP0BCL = 1; return TRUE;
            case 0xB3: // GET IFCONFIG
            EP0BUF[0] = IFCONFIG; EP0BCL = 1; return TRUE;
            case 0xB5: // GET PORTA
            EP0BUF[0] = IOA; EP0BCL = 1; return TRUE;
            case 0xB7: // GET OEA
            EP0BUF[0] = OEA; EP0BCL = 1; return TRUE;
            case 0xB8: // GET EP6CS (FIFO status)
                EP0BUF[0] = EP6CS; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xB9: // GET PINFLAGSAB
                EP0BUF[0] = PINFLAGSAB; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xBA: // GET PINFLAGSCD
                EP0BUF[0] = PINFLAGSCD; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xBB: // GET IFCONFIG snapshot
                extern BYTE boot_ifconfig_snapshot;
                EP0BUF[0] = boot_ifconfig_snapshot; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xBC: // I2C single-byte register write
                slave_addr = SETUPDAT[2];
                reg_addr = SETUPDAT[4];
                value = SETUPDAT[5];
                (void)fx2_i2c_write_reg(slave_addr, reg_addr, value);
                EP0BCH = 0;
                EP0BCL = 0;
                return TRUE;
            case 0xBD: // I2C single-byte register read
                slave_addr = SETUPDAT[2];
                reg_addr = SETUPDAT[4];
                value = 0x00;
                (void)fx2_i2c_read_reg(slave_addr, reg_addr, &value);
                EP0BUF[0] = last_i2c_status;
                EP0BUF[1] = value;
                SD(); EP0BCL = 2; SD(); return TRUE;
            case 0xBE: // GET last I2C status
                EP0BUF[0] = last_i2c_status; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xBF: // I2C block write
                slave_addr = SETUPDAT[2];

                EP0BCL = 0;
                while (EP0CS & bmEPBUSY) { }
                count = EP0BCL;
                if (count < 2) return FALSE;

                reg_addr = EP0BUF[0];
                (void)fx2_i2c_write(slave_addr, 1, &reg_addr, (WORD)(count - 1), &EP0BUF[1]);
                EP0BCH = 0;
                EP0BCL = 0;
                return TRUE;
            case 0xC0: // Set sample rate via SI5351 CLK2 = 2*Fs
                sample_hz = (DWORD)SETUPDAT[2]
                          | ((DWORD)SETUPDAT[3] << 8)
                          | ((DWORD)SETUPDAT[4] << 16)
                          | ((DWORD)SETUPDAT[5] << 24);
                (void)si5351_write_clk2_integer_sample_rate(sample_hz);
                EP0BCH = 0;
                EP0BCL = 0;
                return TRUE;
            case 0xC1: // I2C block read
                slave_addr = SETUPDAT[2];
                reg_addr = SETUPDAT[4];
                count = SETUPDAT[6];
                if (count < 2 || count > 64) return FALSE;
                --count;
                (void)fx2_i2c_read_block_reg(slave_addr, reg_addr, count, &EP0BUF[1]);
                EP0BUF[0] = last_i2c_status;
                SD(); EP0BCL = (BYTE)(count + 1); SD(); return TRUE;
            case 0xC2: // Get FX2 startup SI5351 CLK2 profile
                count = (BYTE)sizeof(si5351_startup_clk2_profile);
                for (reg_addr = 0; reg_addr != count; ++reg_addr) {
                    EP0BUF[reg_addr] = si5351_startup_clk2_profile[reg_addr];
                }
                SD(); EP0BCL = count; SD(); return TRUE;
            case 0xC3: // GET EP2468STAT
                EP0BUF[0] = EP2468STAT; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC4: // GET EP6CFG
                EP0BUF[0] = EP6CFG; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC5: // GET EP6FIFOCFG
                EP0BUF[0] = EP6FIFOCFG; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC6: // GET PORTACFG
                EP0BUF[0] = PORTACFG; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC7: // GET FIFOPINPOLAR
                EP0BUF[0] = FIFOPINPOLAR; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC8: // GET EP2FIFOFLGS
                EP0BUF[0] = EP2FIFOFLGS; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xC9: // GET EP4FIFOFLGS
                EP0BUF[0] = EP4FIFOFLGS; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xCA: // GET EP6FIFOFLGS
                EP0BUF[0] = EP6FIFOFLGS; SD(); EP0BCL = 1; SD(); return TRUE;
            case 0xCB: // GET EP8FIFOFLGS
                EP0BUF[0] = EP8FIFOFLGS; SD(); EP0BCL = 1; SD(); return TRUE;
    }
    return FALSE; // not handled
}

// Optional simple pattern generator for AUTOIN endpoint 6 (16-bit wide)
#ifdef PATTERN_TEST
static void push_pattern_ep6(void) {
    // Fill exactly AUTOIN_LEN bytes. We assume AUTOIN length = 512 (set below)
    // Write 16-bit words incrementing.
    static WORD w = 0;
    BYTE __xdata *p = (BYTE __xdata*) &EP6FIFOBUF; // fx2lib symbol
    WORD count = 0;
    while(count < 512) {
        p[count++] = (BYTE)(w & 0xFF);
        p[count++] = (BYTE)((w >> 8) & 0xFF);
        ++w;
    }
    // Arm packet if manual commit needed (AUTOIN handles length threshold auto-commit)
}
#endif

// Snapshot of IFCONFIG captured during init
BYTE boot_ifconfig_snapshot = 0x00;

void main(void) {
    //Bits
    //7-6: Reserved
    //5: PortCSTB
    //4-3: 00 = 12MHz, 01 = 24MHz, 10 = 48MHz, 11 = Reserved
    //2: CLKINV 
    //1: CLKOE
    //0 - reserved
    CPUCS = 0x0A;  SD(); // 24 MHz, CLKOUT enabled

    IFCONFIG = IFCONFIG_VALUE;  SYNCDELAY;
    // Capture IFCONFIG immediately after we set it
    {
        extern BYTE boot_ifconfig_snapshot;
        boot_ifconfig_snapshot = IFCONFIG;
    }
    REVCTL = 0x03;    SYNCDELAY;  // Enhanced Packet Handling (TRM recommendation for slave FIFO)

    // Configure EP6 (IN) for bulk input, quad-buffered (4*512 bytes).
    EP6CFG = 0xE0;  SYNCDELAY; // 1110 0000: VALID + IN + BULK + Quad

    // FIFO Reset sequence (TRM)
    FIFORESET = 0x80;  SYNCDELAY;  // NAK all requests
    FIFORESET = 0x02;  SYNCDELAY;  // Reset EP2
    FIFORESET = 0x04;  SYNCDELAY;  // Reset EP4
    FIFORESET = 0x06;  SYNCDELAY;  // Reset EP6
    FIFORESET = 0x08;  SYNCDELAY;  // Reset EP8
    FIFORESET = 0x00;  SYNCDELAY;  // Release NAKs

    // 8-bit FIFO samples with AUTOIN; leave WORDWIDE disabled.
    EP6FIFOCFG = 0x0C;  SYNCDELAY;

    // PORTACFG: FLAGD SLCS(*) 0 0 0 0 INT1 INT0
    PORTACFG = 0x00;  SYNCDELAY;

    // All default polarities: SLWR active low, etc.
    FIFOPINPOLAR=0x00;  SYNCDELAY;

    // AUTOIN packet commit length (512 bytes)
    EP6AUTOINLENH = 0x02; SYNCDELAY;  // MSB
    EP6AUTOINLENL = 0x00; SYNCDELAY;  // LSB

    PINFLAGSAB = 0x00; SYNCDELAY; // No special flags on Port A/B pins  
    PINFLAGSCD = 0x00; SYNCDELAY; // No special flags on Port C/D pins

    // Enable FX2 hardware I2C at 100 kHz on the dedicated SDA/SCL pins.
    I2CTL = 0x00;
    (void)si5351_program_default_profile();

    // LED on PA0 for heartbeat
    OEA |= 0x01; // PA0 output

    // Main superloop: blink + optional pattern fill (control transfers handled by fx2lib core)
    unsigned long blink = 0UL;
    for(;;) {
        // Poll for setup data so vendor commands are handled
        handle_setupdata();
        if(++blink == 40000UL) { PA0 = 1; }
        else if(blink == 80000UL) { PA0 = 0; blink = 0UL; }
#ifdef PATTERN_TEST
        if(!(EP6CS & 0x02)) { // 0x02 == FULL bit
            push_pattern_ep6();
        }
#endif
    }
}
