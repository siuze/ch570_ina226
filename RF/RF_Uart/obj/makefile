################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include StdPeriphDriver/subdir.mk
-include Startup/subdir.mk
-include APP/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
RF_Uart.hex \

SECONDARY_LIST += \
RF_Uart.lst \

SECONDARY_SIZE += \
RF_Uart.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: RF_Uart.elf secondary-outputs

# Tool invocations
RF_Uart.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-wch-elf-gcc -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common --param=highcode-gen-section-name=1 -g -T "d:/CH570无线串口-2v0/SRC/Ld/Link.ld" -nostartfiles -Xlinker --gc-sections -L"c:\Users\OWNER\Desktop\无线串口源码-1v3@20251215\RF\RF_Uart" -L"d:/CH570无线串口-2v0/RF/LIB" -L"d:/CH570无线串口-2v0/SRC/StdPeriphDriver" -Xlinker --print-memory-usage -Wl,-Map,"RF_Uart.map" --specs=nano.specs --specs=nosys.specs -o "RF_Uart.elf" $(OBJS) $(LIBS)
RF_Uart.hex: RF_Uart.elf
	@	riscv-wch-elf-objcopy -O ihex "RF_Uart.elf" "RF_Uart.hex"
RF_Uart.lst: RF_Uart.elf
	@	riscv-wch-elf-objdump --source --all-headers --demangle -M xw --line-numbers --wide "RF_Uart.elf" > "RF_Uart.lst"
RF_Uart.siz: RF_Uart.elf
	riscv-wch-elf-size --format=berkeley "RF_Uart.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) RF_Uart.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets