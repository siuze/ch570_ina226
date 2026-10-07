#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GCC_DIR="${ROOT}/tools/xpack-riscv-none-elf-gcc-14.2.0-2/bin"
CC="${GCC_DIR}/riscv-none-elf-gcc"
if [ ! -x "$CC" ]; then
  echo "toolchain not found: $CC" >&2
  exit 1
fi
ARCH="-march=rv32imc_zba_zbb_zbc_zbs_zicsr_zifencei -mabi=ilp32 -mcmodel=medany"
CFLAGS="$ARCH -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g"
COMMON_INC="-I${ROOT}/SRC/StdPeriphDriver/inc -I${ROOT}/SRC/RVMSIS -I${ROOT}/RF/LIB"
COMMON_SRC=("${ROOT}"/SRC/StdPeriphDriver/*.c "${ROOT}/SRC/Startup/startup_CH572.S")
build_project() {
  local name="$1" appdir="$2" inc="$3" outdir="$4" rc=0
  mkdir -p "$outdir"
  for src in "${COMMON_SRC[@]}" "$appdir"/*.c; do
    obj="$outdir/$(basename "${src%.*}").o"
    if ! "$CC" $CFLAGS -DCH570Q $COMMON_INC "$inc" -I"$ROOT/SRC/Startup" -I"$ROOT/SRC/Ld" -c "$src" -o "$obj"; then rc=1; fi
  done
  return "$rc"
}
rc=0
build_project "Probe" "$ROOT/RF/RF_Uart/APP" "-I$ROOT/RF/RF_Uart/APP/include" "$ROOT/RF/RF_Uart/obj_linux" || rc=1
build_project "Dongle" "$ROOT/RF/RF_UartDongle/APP" "-I$ROOT/RF/RF_UartDongle/APP/include" "$ROOT/RF/RF_UartDongle/obj_linux" || rc=1
exit "$rc"
