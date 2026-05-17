#!/usr/bin/env python3
import argparse
import math
from fractions import Fraction
import sys

import usb.core
import usb.util


VID = 0x04B4
PID = 0x8613
SI5351_ADDR = 0x60
SI5351_XTAL_HZ = 25_000_000
SI5351_PLLA_MIN_HZ = 600_000_000
SI5351_PLLA_MAX_HZ = 900_000_000
SI5351_PLLB_HZ = 400_000_000
SI5351_MAX_DENOM = 1_048_575
SI5351_MS_INT_MIN = 5
SI5351_MS_INT_MAX = 900
SI5351_PHASE_MAX = 127
SI5351_SAMPLE_PLL_INT_MIN = 24
SI5351_SAMPLE_PLL_INT_MAX = 36
SI5351_SAMPLE_MS_MIN = 8
SI5351_SAMPLE_MS_MAX = 900

REQ_I2C_WRITE1 = 0xBC
REQ_I2C_READ1 = 0xBD
REQ_I2C_STATUS = 0xBE
REQ_I2C_WRITE_BLOCK = 0xBF
REQ_SET_SAMPLE_RATE = 0xC0
REQ_I2C_READ_BLOCK = 0xC1
REQ_GET_STARTUP_PROFILE = 0xC2
REQ_GET_CPUCS = 0xB1
REQ_GET_IFCONFIG = 0xB3
REQ_GET_EP6CS = 0xB8
REQ_GET_PINFLAGSAB = 0xB9
REQ_GET_PINFLAGSCD = 0xBA
REQ_GET_IFCONFIG_SNAPSHOT = 0xBB
REQ_GET_EP2468STAT = 0xC3
REQ_GET_EP6CFG = 0xC4
REQ_GET_EP6FIFOCFG = 0xC5
REQ_GET_PORTACFG = 0xC6
REQ_GET_FIFOPINPOLAR = 0xC7
REQ_GET_EP2FIFOFLGS = 0xC8
REQ_GET_EP4FIFOFLGS = 0xC9
REQ_GET_EP6FIFOFLGS = 0xCA
REQ_GET_EP8FIFOFLGS = 0xCB

I2C_STATUS_TIMEOUT = 0x01
I2C_STATUS_BERR = 0x02
I2C_STATUS_ADDR_NACK = 0x04
I2C_STATUS_DATA_NACK = 0x08
I2C_STATUS_BAD_PARAM = 0x10

SI5351_INIT_DATA = [
    (3, 0xF8),
    (16, 0x80), (17, 0x80), (18, 0xC0), (19, 0x80),
    (20, 0x80), (21, 0x80), (22, 0x80), (23, 0x80),
    (26, 0x00), (27, 0x01), (28, 0x00), (29, 0x0C),
    (30, 0x00), (31, 0x00), (32, 0x00), (33, 0x00),
    (34, 0x00), (35, 0x01), (36, 0x00), (37, 0x06),
    (38, 0x00), (39, 0x00), (40, 0x00), (41, 0x00),
    (42, 0x00), (43, 0x01), (44, 0x00), (45, 0x30),
    (46, 0x00), (47, 0x00), (48, 0x00), (49, 0x00),
    (50, 0x00), (51, 0x01), (52, 0x00), (53, 0x30),
    (54, 0x00), (55, 0x00), (56, 0x00), (57, 0x00),
    (58, 0x00), (59, 0x01), (60, 0x00), (61, 0x03),
    (62, 0x00), (63, 0x00), (64, 0x00), (65, 0x00),
    (165, 100),
    (177, 0xA0),
    (3, 0xF8),
    (16, 0x0F), (17, 0x0F), (18, 0x6C),
]

FRQ_30005000_DATA = [0, 250, 0, 10, 0, 0, 0, 128, 0, 1, 0, 8, 0, 0, 0, 0, 20]

FREQ_TABLE = [
    (7025000,  [1, 244, 0, 10, 21, 0, 0, 124, 0, 1, 0, 41, 0, 0, 0, 0, 86]),
    (7075000,  [0, 200, 0, 10, 7, 0, 0, 8, 0, 1, 0, 40, 128, 0, 0, 0, 85]),
    (7125000,  [0, 40, 0, 10, 28, 0, 0, 32, 0, 1, 0, 40, 128, 0, 0, 0, 85]),
    (7175000,  [0, 250, 0, 10, 13, 0, 0, 206, 0, 1, 0, 40, 0, 0, 0, 0, 84]),
    (7225000,  [0, 250, 0, 10, 35, 0, 0, 82, 0, 1, 0, 40, 0, 0, 0, 0, 84]),
    (7275000,  [3, 232, 0, 10, 19, 0, 2, 72, 0, 1, 0, 39, 128, 0, 0, 0, 83]),
    (30005000, FRQ_30005000_DATA),
    (89750000, [136, 181, 192, 10, 144, 8, 5, 176, 0, 1, 0, 1, 128, 0, 0, 0, 7]),
    (93350000, [1, 244, 0, 11, 17, 0, 1, 76, 0, 1, 0, 1, 128, 0, 0, 0, 7]),
    (97850000, [1, 244, 0, 11, 178, 0, 1, 216, 0, 1, 0, 1, 128, 0, 0, 0, 7]),
    (98150000, [43, 58, 224, 11, 189, 9, 220, 174, 0, 1, 0, 1, 128, 0, 0, 0, 7]),
    (100250000,[0, 50, 0, 10, 7, 0, 0, 34, 0, 1, 0, 1, 0, 0, 0, 0, 6]),
    (102750000,[0, 50, 0, 10, 84, 0, 0, 24, 0, 1, 0, 1, 0, 0, 0, 0, 6]),
    (104550000,[0, 250, 0, 10, 139, 0, 0, 194, 0, 1, 0, 1, 0, 0, 0, 0, 6]),
    (106050000,[0, 250, 0, 10, 185, 0, 0, 214, 0, 1, 0, 1, 0, 0, 0, 0, 6]),
    (144850000,[0, 100, 0, 12, 124, 0, 0, 16, 0, 1, 0, 0, 128, 0, 0, 0, 5]),
]

EXPECTED_STARTUP_CLK2_PROFILE = [
    (3, 0xF8),
    (16, 0x0F),
    (17, 0x0F),
    (18, 0x6C),
    (34, 0x00),
    (35, 0x01),
    (36, 0x00),
    (37, 0x06),
    (38, 0x00),
    (39, 0x00),
    (40, 0x00),
    (41, 0x00),
    (58, 0x00),
    (59, 0x01),
    (60, 0x00),
    (61, 0x03),
    (62, 0x00),
    (63, 0x00),
    (64, 0x00),
    (65, 0x00),
    (165, 20),
    (177, 0x20),
]


def parse_int(text: str) -> int:
    return int(text, 0)


def status_to_text(status: int) -> str:
    if status == 0:
        return "OK"
    parts = []
    if status & I2C_STATUS_TIMEOUT:
        parts.append("timeout")
    if status & I2C_STATUS_BERR:
        parts.append("bus-error")
    if status & I2C_STATUS_ADDR_NACK:
        parts.append("addr-nack")
    if status & I2C_STATUS_DATA_NACK:
        parts.append("data-nack")
    return ",".join(parts) if parts else f"0x{status:02x}"


def open_device(vid: int, pid: int):
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        raise SystemExit(f"FX2 device {vid:04x}:{pid:04x} not found")

    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass

    try:
        cfg = dev.get_active_configuration()
        intf = cfg[(0, 0)]
        try:
            if dev.is_kernel_driver_active(intf.bInterfaceNumber):
                dev.detach_kernel_driver(intf.bInterfaceNumber)
        except (NotImplementedError, usb.core.USBError):
            pass
        try:
            usb.util.claim_interface(dev, intf.bInterfaceNumber)
        except usb.core.USBError:
            pass
    except usb.core.USBError:
        pass

    return dev


def try_ctrl_transfer(dev, request_types, request, value, index, data_or_length, timeout=2000):
    last_exc = None
    for request_type in request_types:
        try:
            return dev.ctrl_transfer(request_type, request, value, index, data_or_length, timeout=timeout)
        except usb.core.USBError as exc:
            last_exc = exc
    if last_exc is not None:
        raise last_exc
    raise RuntimeError("no control transfer request types provided")


def get_i2c_status(dev) -> int:
    last_exc = None
    for _ in range(5):
        try:
            data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), REQ_I2C_STATUS, 0, 0, 1, timeout=2000)
            if len(data) < 1:
                raise RuntimeError("I2C status request returned no data")
            data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), REQ_I2C_STATUS, 0, 0, 1, timeout=2000)
            if len(data) < 1:
                raise RuntimeError("I2C status request returned no data")
            return int(data[0])
        except (RuntimeError, usb.core.USBError) as exc:
            last_exc = exc
    if last_exc is not None:
        raise last_exc
    raise RuntimeError("I2C status request failed")


def get_vendor_byte(dev, request: int) -> int:
    last_exc = None
    for _ in range(5):
        try:
            data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), request, 0, 0, 1, timeout=2000)
            if len(data) < 1:
                raise RuntimeError(f"vendor request 0x{request:02X} returned no data")
            data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), request, 0, 0, 1, timeout=2000)
            if len(data) < 1:
                raise RuntimeError(f"vendor request 0x{request:02X} returned no data")
            return int(data[0])
        except (RuntimeError, usb.core.USBError) as exc:
            last_exc = exc
    if last_exc is not None:
        raise last_exc
    raise RuntimeError(f"vendor request 0x{request:02X} failed")


def i2c_write_reg(dev, slave: int, reg: int, value: int) -> None:
    index = (reg & 0xFF) | ((value & 0xFF) << 8)
    try_ctrl_transfer(dev, (0x40, 0x41, 0x42), REQ_I2C_WRITE1, slave & 0xFF, index, b"", timeout=2000)


def i2c_read_reg_once(dev, slave: int, reg: int) -> int:
    data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), REQ_I2C_READ1, slave & 0xFF, reg & 0xFF, 2, timeout=2000)
    if len(data) < 2:
        raise RuntimeError(f"I2C read at reg 0x{reg:02X} returned {len(data)} bytes")
    status = int(data[0])
    value = int(data[1])
    if status != 0:
        raise RuntimeError(f"I2C read failed at reg 0x{reg:02X}: {status_to_text(status)}")
    return value


def i2c_read_reg(dev, slave: int, reg: int) -> int:
    last_exc = None
    for _ in range(5):
        try:
            i2c_read_reg_once(dev, slave, reg)
            return i2c_read_reg_once(dev, slave, reg)
        except (RuntimeError, usb.core.USBError) as exc:
            last_exc = exc
    if last_exc is not None:
        raise last_exc
    raise RuntimeError(f"I2C read failed at reg 0x{reg:02X}")


def i2c_read_block(dev, slave: int, start_reg: int, count: int) -> list[int]:
    data = try_ctrl_transfer(
        dev,
        (0xC0, 0xC1, 0xC2),
        REQ_I2C_READ_BLOCK,
        slave & 0xFF,
        start_reg & 0xFF,
        count + 1,
        timeout=2000,
    )
    if len(data) < count + 1:
        raise RuntimeError(f"I2C block read returned {len(data)} bytes, expected {count + 1}")
    status = int(data[0])
    if status != 0:
        raise RuntimeError(f"I2C block read failed at reg 0x{start_reg:02X}: {status_to_text(status)}")
    return [int(byte) for byte in data[1:1 + count]]


def i2c_write_block(dev, slave: int, start_reg: int, values: list[int]) -> None:
    payload = [start_reg & 0xFF] + [value & 0xFF for value in values]
    try_ctrl_transfer(dev, (0x40, 0x41, 0x42), REQ_I2C_WRITE_BLOCK, slave & 0xFF, 0, bytes(payload), timeout=2000)


def get_startup_profile(dev) -> list[tuple[int, int]]:
    expected_len = len(EXPECTED_STARTUP_CLK2_PROFILE) * 2
    data = try_ctrl_transfer(dev, (0xC0, 0xC1, 0xC2), REQ_GET_STARTUP_PROFILE, 0, 0, expected_len, timeout=2000)
    if len(data) != expected_len:
        raise RuntimeError(f"startup profile returned {len(data)} bytes, expected {expected_len}")
    pairs = []
    for offset in range(0, len(data), 2):
        pairs.append((int(data[offset]), int(data[offset + 1])))
    return pairs


def encode_si5351_abc(a: int, b: int, c: int) -> tuple[int, int, int, int, int, int, int, int]:
    if c <= 0:
        raise ValueError("denominator must be > 0")
    if b < 0 or b >= c:
        raise ValueError("fraction must satisfy 0 <= b < c")

    frac = (128 * b) // c
    p1 = 128 * a + frac - 512
    p2 = 128 * b - c * frac
    p3 = c

    return (
        (p3 >> 8) & 0xFF,
        p3 & 0xFF,
        ((p1 >> 16) & 0x03),
        (p1 >> 8) & 0xFF,
        p1 & 0xFF,
        (((p3 >> 12) & 0xF0) | ((p2 >> 16) & 0x0F)),
        (p2 >> 8) & 0xFF,
        p2 & 0xFF,
    )


def ratio_to_abc(numerator: int, denominator: int) -> tuple[int, int, int]:
    if denominator <= 0:
        raise ValueError("denominator must be > 0")

    frac = Fraction(numerator, denominator)
    if frac.denominator > SI5351_MAX_DENOM:
        frac = frac.limit_denominator(SI5351_MAX_DENOM)

    a = frac.numerator // frac.denominator
    b = frac.numerator % frac.denominator
    c = frac.denominator
    return a, b, c


def build_pllb_400mhz_regs(xtal_hz: int) -> list[tuple[int, int]]:
    a, b, c = ratio_to_abc(SI5351_PLLB_HZ, xtal_hz)
    regs = encode_si5351_abc(a, b, c)
    return [(34 + idx, value) for idx, value in enumerate(regs)]


def build_ms2_regs_for_clk_hz(clk_hz: int) -> list[tuple[int, int]]:
    if clk_hz <= 0:
        raise ValueError("CLK2 frequency must be > 0")

    a, b, c = ratio_to_abc(SI5351_PLLB_HZ, clk_hz)
    if a < 8 or a > 900:
        raise ValueError(f"derived multisynth divider {a} out of expected range for CLK2")

    regs = list(encode_si5351_abc(a, b, c))
    return [(58 + idx, value) for idx, value in enumerate(regs)]


def build_integer_ms0_ms1_regs(divider: int) -> list[tuple[int, int]]:
    if divider < SI5351_MS_INT_MIN or divider > SI5351_MS_INT_MAX:
        raise ValueError(f"integer multisynth divider {divider} out of supported range")

    regs = list(encode_si5351_abc(divider, 0, 1))
    pairs = []
    for idx, value in enumerate(regs):
        pairs.append((42 + idx, value))
        pairs.append((50 + idx, value))
    return pairs


def find_integer_only_sample_rate_matches(sample_hz: int) -> list[tuple[int, int, int]]:
    matches = []
    target_clk2_hz = sample_hz * 2
    for pll_mult in range(SI5351_SAMPLE_PLL_INT_MIN, SI5351_SAMPLE_PLL_INT_MAX + 1):
        pll_hz = SI5351_XTAL_HZ * pll_mult
        if pll_hz % target_clk2_hz != 0:
            continue
        divider = pll_hz // target_clk2_hz
        if SI5351_SAMPLE_MS_MIN <= divider <= SI5351_SAMPLE_MS_MAX:
            matches.append((sample_hz, pll_mult, divider))
    return matches


def nearest_integer_only_sample_rates(sample_hz: int, count: int = 5) -> list[int]:
    rates = set()
    for pll_mult in range(SI5351_SAMPLE_PLL_INT_MIN, SI5351_SAMPLE_PLL_INT_MAX + 1):
        pll_hz = SI5351_XTAL_HZ * pll_mult
        for divider in range(SI5351_SAMPLE_MS_MIN, SI5351_SAMPLE_MS_MAX + 1):
            if pll_hz % (2 * divider) != 0:
                continue
            rates.add(pll_hz // (2 * divider))
    return sorted(rates, key=lambda rate: (abs(rate - sample_hz), rate))[:count]


def synthesize_clk01_frequency(freq_hz: int) -> tuple[list[int], list[int], int, int]:
    if freq_hz <= 0:
        raise ValueError("frequency must be > 0")

    divider = max(SI5351_MS_INT_MIN, math.ceil(SI5351_PLLA_MIN_HZ / freq_hz))
    if divider > SI5351_PHASE_MAX:
        min_freq_hz = math.ceil(SI5351_PLLA_MIN_HZ / SI5351_PHASE_MAX)
        raise ValueError(
            f"frequency {freq_hz} Hz is too low for the current quadrature phase scheme; "
            f"minimum supported frequency is {min_freq_hz} Hz"
        )

    vco_hz = freq_hz * divider
    if vco_hz < SI5351_PLLA_MIN_HZ or vco_hz > SI5351_PLLA_MAX_HZ:
        raise ValueError(
            f"frequency {freq_hz} Hz produces PLLA VCO {vco_hz} Hz outside the supported range "
            f"{SI5351_PLLA_MIN_HZ}..{SI5351_PLLA_MAX_HZ}"
        )

    a, b, c = ratio_to_abc(vco_hz, SI5351_XTAL_HZ)
    pll_bytes = list(encode_si5351_abc(a, b, c))
    ms_bytes = list(encode_si5351_abc(divider, 0, 1))
    return pll_bytes, ms_bytes, divider, vco_hz


def get_65uino_frequency_record(freq_hz: int) -> list[int]:
    for known_hz, record in FREQ_TABLE:
        if known_hz == freq_hz:
            return list(record)
    supported = ", ".join(str(freq) for freq, _ in FREQ_TABLE)
    raise ValueError(f"unsupported 65uino frequency {freq_hz}; supported values: {supported}")


def set_clk01_frequency(dev, slave: int, freq_hz: int) -> tuple[int, int]:
    pll_bytes, ms_bytes, phase, vco_hz = synthesize_clk01_frequency(freq_hz)
    pll_regs = [(26 + idx, value) for idx, value in enumerate(pll_bytes)]
    ms_regs = []
    for idx, value in enumerate(ms_bytes):
        ms_regs.append((42 + idx, value))
        ms_regs.append((50 + idx, value))

    i2c_write_reg(dev, slave, 3, 0xFF)
    for reg, value in pll_regs:
        i2c_write_reg(dev, slave, reg, value)
    for reg, value in ms_regs:
        i2c_write_reg(dev, slave, reg, value)
    i2c_write_reg(dev, slave, 165, phase)
    i2c_write_reg(dev, slave, 177, 0x20)
    i2c_write_reg(dev, slave, 3, 0xF8)
    return phase, vco_hz


def synthesized_matches_65uino_lut(freq_hz: int) -> bool:
    record = get_65uino_frequency_record(freq_hz)
    pll_bytes, ms_bytes, phase, _ = synthesize_clk01_frequency(freq_hz)
    return record[:8] == pll_bytes and record[8:16] == ms_bytes and record[16] == phase


def cmd_read(args):
    dev = open_device(args.vid, args.pid)
    value = i2c_read_reg(dev, args.addr, args.reg)
    print(f"0x{value:02X}")


def cmd_write(args):
    dev = open_device(args.vid, args.pid)
    i2c_write_reg(dev, args.addr, args.reg, args.value)
    print(f"wrote reg 0x{args.reg:02X} = 0x{args.value:02X}")


def cmd_dump(args):
    dev = open_device(args.vid, args.pid)
    for offset in range(args.count):
        reg = args.start + offset
        value = i2c_read_reg(dev, args.addr, reg)
        print(f"0x{reg:02X}: 0x{value:02X}")


def write_pairs(dev, slave, pairs):
    for reg, value in pairs:
        i2c_write_reg(dev, slave, reg, value)


def program_65uino_default(dev, slave):
    write_pairs(dev, slave, SI5351_INIT_DATA)

    # Same post-init frequency programming path as 65uino index 5 (30.005 MHz).
    i2c_write_reg(dev, slave, 3, 0xFF)

    pll = FRQ_30005000_DATA[:8]
    ms = FRQ_30005000_DATA[8:16]
    phase = FRQ_30005000_DATA[16]

    for idx, value in enumerate(pll):
        i2c_write_reg(dev, slave, 26 + idx, value)

    for idx in range(16):
        i2c_write_reg(dev, slave, 42 + idx, ms[idx & 7])

    i2c_write_reg(dev, slave, 165, phase)
    i2c_write_reg(dev, slave, 177, 0x20)
    i2c_write_reg(dev, slave, 3, 0xF8)


def cmd_program_65uino_default(args):
    dev = open_device(args.vid, args.pid)
    program_65uino_default(dev, args.addr)
    print("programmed SI5351 using the 65uino default init + 30.005 MHz profile")


def cmd_set_sample_rate(args):
    dev = open_device(args.vid, args.pid)
    sample_hz = args.sample_hz
    clk2_hz = sample_hz * 2

    value = sample_hz & 0xFFFF
    index = (sample_hz >> 16) & 0xFFFF
    try_ctrl_transfer(dev, (0x40, 0x41, 0x42), REQ_SET_SAMPLE_RATE, value, index, b"", timeout=2000)
    status = get_i2c_status(dev)
    if status != 0:
        if status == I2C_STATUS_BAD_PARAM:
            matches = find_integer_only_sample_rate_matches(sample_hz)
            if matches:
                _, pll_mult, divider = matches[0]
                raise RuntimeError(
                    f"set sample rate failed: integer-only firmware expected to support {sample_hz} Hz "
                    f"with PLLB mult={pll_mult}, MS2 divider={divider}; device reported bad-param"
                )
            nearest = ", ".join(str(rate) for rate in nearest_integer_only_sample_rates(sample_hz))
            raise RuntimeError(
                f"set sample rate failed: integer-only mode cannot realize {sample_hz} Hz exactly; "
                f"nearest supported rates are {nearest}"
            )
        raise RuntimeError(f"set sample rate failed: {status_to_text(status)}")

    print(f"set sample rate to {sample_hz} Hz (CLK2={clk2_hz} Hz)")


def cmd_list_frequencies(args):
    for freq_hz, _ in FREQ_TABLE:
        print(freq_hz)


def cmd_set_frequency(args):
    dev = open_device(args.vid, args.pid)
    divider, vco_hz = set_clk01_frequency(dev, args.addr, args.freq_hz)
    source = "synthesized"
    try:
        if synthesized_matches_65uino_lut(args.freq_hz):
            source = "matches 65uino LUT"
        else:
            source = "synthesized from target frequency"
    except ValueError:
        pass
    print(
        f"set CLK0/CLK1 frequency to {args.freq_hz} Hz "
        f"({source}, divider={divider}, PLLA={vco_hz} Hz)"
    )


def cmd_dump_startup_profile(args):
    dev = open_device(args.vid, args.pid)
    for reg, value in get_startup_profile(dev):
        print(f"0x{reg:02X}: 0x{value:02X}")


def cmd_compare_startup_profile(args):
    dev = open_device(args.vid, args.pid)
    actual = get_startup_profile(dev)
    expected = EXPECTED_STARTUP_CLK2_PROFILE
    mismatches = []
    for (expected_reg, expected_value), (actual_reg, actual_value) in zip(expected, actual):
        if expected_reg != actual_reg or expected_value != actual_value:
            mismatches.append((expected_reg, expected_value, actual_reg, actual_value))

    if mismatches:
        for expected_reg, expected_value, actual_reg, actual_value in mismatches:
            print(
                f"mismatch expected reg 0x{expected_reg:02X}=0x{expected_value:02X} "
                f"got reg 0x{actual_reg:02X}=0x{actual_value:02X}"
            )
        raise RuntimeError("startup CLK2 profile does not match 65uino expected values")

    print("startup CLK2 profile matches 65uino expected values")


def cmd_fx2_diagnostics(args):
    dev = open_device(args.vid, args.pid)
    diagnostics = [
        ("CPUCS", REQ_GET_CPUCS),
        ("IFCONFIG", REQ_GET_IFCONFIG),
        ("IFCONFIG_BOOT", REQ_GET_IFCONFIG_SNAPSHOT),
        ("EP2468STAT", REQ_GET_EP2468STAT),
        ("EP6CFG", REQ_GET_EP6CFG),
        ("EP6FIFOCFG", REQ_GET_EP6FIFOCFG),
        ("EP2FIFOFLGS", REQ_GET_EP2FIFOFLGS),
        ("EP4FIFOFLGS", REQ_GET_EP4FIFOFLGS),
        ("EP6FIFOFLGS", REQ_GET_EP6FIFOFLGS),
        ("EP8FIFOFLGS", REQ_GET_EP8FIFOFLGS),
        ("EP6CS", REQ_GET_EP6CS),
        ("PORTACFG", REQ_GET_PORTACFG),
        ("FIFOPINPOLAR", REQ_GET_FIFOPINPOLAR),
        ("PINFLAGSAB", REQ_GET_PINFLAGSAB),
        ("PINFLAGSCD", REQ_GET_PINFLAGSCD),
    ]

    for label, request in diagnostics:
        value = get_vendor_byte(dev, request)
        print(f"{label}=0x{value:02X}")

    print(f"I2C_STATUS=0x{get_i2c_status(dev):02X}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Control an SI5351 through FX2LP vendor I2C commands")
    parser.add_argument("--vid", type=parse_int, default=VID)
    parser.add_argument("--pid", type=parse_int, default=PID)
    parser.add_argument("--addr", type=parse_int, default=SI5351_ADDR, help="7-bit I2C slave address")

    sub = parser.add_subparsers(dest="cmd", required=True)

    p_read = sub.add_parser("read", help="Read one SI5351 register")
    p_read.add_argument("reg", type=parse_int)
    p_read.set_defaults(func=cmd_read)

    p_write = sub.add_parser("write", help="Write one SI5351 register")
    p_write.add_argument("reg", type=parse_int)
    p_write.add_argument("value", type=parse_int)
    p_write.set_defaults(func=cmd_write)

    p_dump = sub.add_parser("dump", help="Dump a range of SI5351 registers")
    p_dump.add_argument("start", type=parse_int)
    p_dump.add_argument("count", type=parse_int)
    p_dump.set_defaults(func=cmd_dump)

    p_prog = sub.add_parser("program-65uino-default", help="Program SI5351 the same basic way as 65uino startup")
    p_prog.set_defaults(func=cmd_program_65uino_default)

    p_rate = sub.add_parser("set-sample-rate", help="Set CLK2 to 2x the requested sample rate")
    p_rate.add_argument("sample_hz", type=parse_int, help="Desired sample rate in Hz; CLK2 will be set to 2x this value")
    p_rate.set_defaults(func=cmd_set_sample_rate)

    p_list_freq = sub.add_parser("list-frequencies", help="List the supported 65uino retune frequencies for CLK0/CLK1")
    p_list_freq.set_defaults(func=cmd_list_frequencies)

    p_set_freq = sub.add_parser("set-frequency", help="Set CLK0/CLK1 to an arbitrary frequency while streaming remains active")
    p_set_freq.add_argument("freq_hz", type=parse_int, help="Desired CLK0/CLK1 frequency in Hz")
    p_set_freq.set_defaults(func=cmd_set_frequency)

    p_dump_profile = sub.add_parser("dump-startup-profile", help="Dump the FX2 firmware's exported startup CLK2 profile")
    p_dump_profile.set_defaults(func=cmd_dump_startup_profile)

    p_compare_profile = sub.add_parser("compare-startup-profile", help="Compare the FX2 startup CLK2 profile against the 65uino expected values")
    p_compare_profile.set_defaults(func=cmd_compare_startup_profile)

    p_fx2_diag = sub.add_parser("fx2-diagnostics", help="Dump FX2 interface diagnostic registers")
    p_fx2_diag.set_defaults(func=cmd_fx2_diagnostics)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.func(args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())