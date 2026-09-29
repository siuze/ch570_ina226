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
../APP/rf_uart_rx.c \
../APP/usb_uart.c 

C_DEPS += \
./APP/buf.d \
./APP/main.d \
./APP/my_printf.d \
./APP/rf.d \
./APP/rf_uart_rx.d \
./APP/usb_uart.d 

OBJS += \
./APP/buf.o \
./APP/main.o \
./APP/my_printf.o \
./APP/rf.o \
./APP/rf_uart_rx.o \
./APP/usb_uart.o 

DIR_OBJS += \
./APP/*.o \

DIR_DEPS += \
./APP/*.d \

DIR_EXPANDS += \
./APP/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -DDEBUG -I"d:/CH570无线串口-2v0/SRC/Startup" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/APP/include" -I"d:/CH570无线串口-2v0/RF/RF_UartDongle/Profile/include" -I"d:/CH570无线串口-2v0/SRC/StdPeriphDriver/inc" -I"d:/CH570无线串口-2v0/SRC/Ld" -I"d:/CH570无线串口-2v0/RF/LIB" -I"d:/CH570无线串口-2v0/SRC/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

