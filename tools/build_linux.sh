#!/usr/bin/env bash
# Portable Linux build / compile-check for the CH570Q wireless-uart firmwares.
#
# The MounRiver-generated makefiles under RF/*/obj hard-code Windows paths and the
# WCH-specific 'riscv-wch-elf-gcc' (march ..._xw). This script builds the same
# sources with a standard RISC-V GCC (xPack riscv-none-elf-gcc) using a march
# without the WCH-private 'xw' extension, which is sufficient to compile every
# project source file.
#
# NOTE: final linking additionally requires WCH's proprietary prebuilt libraries
#   libCH57xRF.a  (2.4G RF role stack)  and  libISP572.a  (Flash-ROM routines),
# which are distributed only inside the WCH CH57x EVT/SDK package and are NOT
# part of this repository. Without them the link step reports undefined RF_*/
# FLASH_ROM_* symbols; compiling all objects (the default action) still fully
# validates the firmware source.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GCC_DIR="$ROOT/tools/xpack-riscv-none-elf-gcc-14.2.0-2/bin"
CC="$GCC_DIR/riscv-none-elf-gcc"

if [ ! -x "$CC" ]; then
    echo "ERROR: toolchain not found at $CC" >&2
    echo "Download xPack riscv-none-elf-gcc (linux-x64) into $ROOT/tools/ first." >&2
    exit 1
fi

ARCH="-march=rv32imc_zba_zbb_zbc_zbs_zicsr_zifencei -mabi=ilp32 -mcmodel=medany"
CFLAGS="$ARCH -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 \
        -fsigned-char -ffunction-sections -fdata-sections -fno-common -g"
DEFS="-DCH570Q"

COMMON_INC="-I$ROOT/SRC/StdPeriphDriver/inc -I$ROOT/SRC/RVMSIS -I$ROOT/RF/LIB"
COMMON_SRC="$(ls "$ROOT"/SRC/StdPeriphDriver/*.c) $ROOT/SRC/Startup/startup_CH572.S"

build_project() {
    local name="$1" appdir="$2" inc="$3" outdir="$4"
    mkdir -p "$outdir"
    local fail=0 n=0
    local app_src
    app_src="$(ls "$appdir"/*.c)"
    for src in $COMMON_SRC $app_src; do
        local base obj
        base="$(basename "$src")"
        obj="$outdir/${base%.*}.o"
        n=$((n+1))
        if ! "$CC" $CFLAGS $DEFS $COMMON_INC "$inc" -c "$src" -o "$obj" 2> "$outdir/${base%.*}.err"; then
            echo "  [FAIL] $name: $base"
            sed -n '1,15p' "$outdir/${base%.*}.err"
            fail=1
        else
            if [ -s "$outdir/${base%.*}.err" ]; then
                echo "  [WARN] $name: $base"
                sed -n '1,10p' "$outdir/${base%.*}.err"
            fi
        fi
    done
    if [ "$fail" -eq 0 ]; then
        echo "== $name: all $n translation units compiled OK =="
    else
        echo "== $name: COMPILE ERRORS =="
    fi
    return $fail
}

echo "### Compile-check (objects only; final link needs WCH libCH57xRF.a/libISP572.a) ###"
rc=0
build_project "RF_Uart (Probe)"   "$ROOT/RF/RF_Uart/APP"       "-I$ROOT/RF/RF_Uart/APP/include"       "$ROOT/RF/RF_Uart/obj_linux"       || rc=1
build_project "RF_UartDongle"     "$ROOT/RF/RF_UartDongle/APP"  "-I$ROOT/RF/RF_UartDongle/APP/include" "$ROOT/RF/RF_UartDongle/obj_linux"  || rc=1

exit $rc
