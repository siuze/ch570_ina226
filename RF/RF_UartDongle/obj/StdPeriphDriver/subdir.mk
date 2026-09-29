################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_clk.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_cmp.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_flash.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_gpio.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_i2c.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_keyscan.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_pwm.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_pwr.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_spi.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_sys.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_timer.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_uart.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbdev.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbhostBase.c \
d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbhostClass.c 

C_DEPS += \
./StdPeriphDriver/CH57x_clk.d \
./StdPeriphDriver/CH57x_cmp.d \
./StdPeriphDriver/CH57x_flash.d \
./StdPeriphDriver/CH57x_gpio.d \
./StdPeriphDriver/CH57x_i2c.d \
./StdPeriphDriver/CH57x_keyscan.d \
./StdPeriphDriver/CH57x_pwm.d \
./StdPeriphDriver/CH57x_pwr.d \
./StdPeriphDriver/CH57x_spi.d \
./StdPeriphDriver/CH57x_sys.d \
./StdPeriphDriver/CH57x_timer.d \
./StdPeriphDriver/CH57x_uart.d \
./StdPeriphDriver/CH57x_usbdev.d \
./StdPeriphDriver/CH57x_usbhostBase.d \
./StdPeriphDriver/CH57x_usbhostClass.d 

OBJS += \
./StdPeriphDriver/CH57x_clk.o \
./StdPeriphDriver/CH57x_cmp.o \
./StdPeriphDriver/CH57x_flash.o \
./StdPeriphDriver/CH57x_gpio.o \
./StdPeriphDriver/CH57x_i2c.o \
./StdPeriphDriver/CH57x_keyscan.o \
./StdPeriphDriver/CH57x_pwm.o \
./StdPeriphDriver/CH57x_pwr.o \
./StdPeriphDriver/CH57x_spi.o \
./StdPeriphDriver/CH57x_sys.o \
./StdPeriphDriver/CH57x_timer.o \
./StdPeriphDriver/CH57x_uart.o \
./StdPeriphDriver/CH57x_usbdev.o \
./StdPeriphDriver/CH57x_usbhostBase.o \
./StdPeriphDriver/CH57x_usbhostClass.o 

DIR_OBJS += \
./StdPeriphDriver/*.o \

DIR_DEPS += \
./StdPeriphDriver/*.d \

DIR_EXPANDS += \
./StdPeriphDriver/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
StdPeriphDriver/CH57x_clk.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_clk.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_cmp.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_cmp.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_flash.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_flash.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_gpio.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_gpio.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_i2c.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_i2c.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_keyscan.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_keyscan.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_pwm.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_pwm.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_pwr.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_pwr.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_spi.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_spi.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_sys.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_sys.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_timer.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_timer.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_uart.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_uart.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_usbdev.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbdev.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_usbhostBase.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbhostBase.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
StdPeriphDriver/CH57x_usbhostClass.o: d:/CH570无线串口-2v0/SRC/StdPeriphDriver/CH57x_usbhostClass.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

