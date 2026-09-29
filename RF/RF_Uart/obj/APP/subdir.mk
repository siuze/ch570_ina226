################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/buf.c \
../APP/main.c \
../APP/my_printf.c \
../APP/rf.c \
../APP/rf_uart_tx.c \
../APP/uart.c \
../APP/ble_adv.c \
../APP/ina226.c 

C_DEPS += \
./APP/buf.d \
./APP/main.d \
./APP/my_printf.d \
./APP/rf.d \
./APP/rf_uart_tx.d \
./APP/uart.d \
./APP/ble_adv.d \
./APP/ina226.d 

OBJS += \
./APP/buf.o \
./APP/main.o \
./APP/my_printf.o \
./APP/rf.o \
./APP/rf_uart_tx.o \
./APP/uart.o \
./APP/ble_adv.o \
./APP/ina226.o 

DIR_OBJS += \
./APP/*.o \

DIR_DEPS += \
./APP/*.d \

DIR_EXPANDS += \
./APP/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_Uart/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_Uart/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

