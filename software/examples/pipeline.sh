#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
SOFTWARE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
HOST_DIR="$SOFTWARE_DIR/host"
FIRMWARE_DIR="$SOFTWARE_DIR/firmware"
TOOL_PYTHON="$SOFTWARE_DIR/../.venv/bin/python"

if [[ ! -x "$TOOL_PYTHON" ]]; then
    TOOL_PYTHON="python3"
fi

usage() {
    cat >&2 <<'EOF'
Usage: ./pipeline.sh [--sample-hz HZ|--sample_hz HZ] [--dcI N --dcQ N] [--kRe X --kIm Y] [--no-dc] [--no-iqbal] [--no-build] [--no-flash]

Streams CF32 IQ to /tmp/iq.fifo for GQRX.

Optional pre-start sample-rate setup:

    ./pipeline.sh --sample-hz 20000000
    ./pipeline.sh --sample_hz 20000000

While the pipeline is running, retune CLK0/CLK1 from another terminal:

    ../firmware/tools/fx2_si5351_tool.py list-frequencies
    ../firmware/tools/fx2_si5351_tool.py set-frequency 98150000

Calibration is run-once and manual; then paste the numbers into the args:

    DC (silence, u8 domain):
        ../host/fx2_stream_stdout -i 0 -t 16384 -n 32 | ../host/iq_dc_cal --samples 65536

    IQ balance (tone, CF32 domain):
        ../host/fx2_stream_stdout -i 0 -t 16384 -n 32 | ../host/iq_to_cf32 -m 8u \
            | ../host/iq_iqbal_tone_cal_cf32 --tone-hz 2657000 --fs-hz 20000000 --samples 65536

Examples:
    ./pipeline.sh --dcI 3 --dcQ 3
    ./pipeline.sh --dcI 3 --dcQ 3 --kRe -0.0378 --kIm 0.4850
EOF
}

DO_BUILD=1
DO_FLASH=1
DO_DC=1
DO_IQBAL=1
SAMPLE_HZ=""

DC_I=0
DC_Q=0
K_RE=""
K_IM=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dcI) DC_I="$2"; shift 2;;
        --dcQ) DC_Q="$2"; shift 2;;
        --kRe) K_RE="$2"; shift 2;;
        --kIm) K_IM="$2"; shift 2;;
        --sample-hz|--sample_hz) SAMPLE_HZ="$2"; shift 2;;
        --no-dc) DO_DC=0; shift;;
        --no-iqbal) DO_IQBAL=0; shift;;
        --no-build) DO_BUILD=0; shift;;
        --no-flash) DO_FLASH=0; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown arg: $1" >&2; usage; exit 2;;
    esac
done

if [[ "$DO_FLASH" == "1" ]]; then
    make -C "$FIRMWARE_DIR" flash-twice
fi
if [[ "$DO_BUILD" == "1" ]]; then
    make -C "$HOST_DIR"
fi
if [[ -n "$SAMPLE_HZ" ]]; then
    "$TOOL_PYTHON" "$FIRMWARE_DIR/tools/fx2_si5351_tool.py" set-sample-rate "$SAMPLE_HZ"
fi

if [[ ! -p /tmp/iq.fifo ]]; then
    rm -f /tmp/iq.fifo
    mkfifo /tmp/iq.fifo
fi

echo "stream -> /tmp/iq.fifo  sample_hz=${SAMPLE_HZ:-unchanged}  dc=${DO_DC}(${DC_I},${DC_Q})  iqbal=${DO_IQBAL}(${K_RE:-none},${K_IM:-none})" >&2

"$HOST_DIR/fx2_stream_stdout" -i 0 -t 16384 -n 32 \
    | ( if [[ "$DO_DC" == "1" ]]; then "$HOST_DIR/iq_dc_apply" --dcI "$DC_I" --dcQ "$DC_Q"; else cat; fi ) \
    | "$HOST_DIR/iq_to_cf32" -m 8u \
    | ( if [[ "$DO_IQBAL" == "1" && -n "$K_RE" && -n "$K_IM" ]]; then "$HOST_DIR/iq_iqbal_apply_cf32" --kRe "$K_RE" --kIm "$K_IM"; else cat; fi ) \
    > /tmp/iq.fifo