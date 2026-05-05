ARM_PREFIX	:=	arm-none-eabi
ARM_AS		:=	$(ARM_PREFIX)-as
ARM_CC		:=	$(ARM_PREFIX)-gcc
ARM_LD		:=	$(ARM_PREFIX)-ld
ARM_OC		:=	$(ARM_PREFIX)-objcopy
ARM_AS_OPTIONS	:=	-mcpu=cortex-m3 -mthumb -g
ARM_CC_OPTIONS	:=	-specs=vp2.specs -Wall -O3 -mcpu=cortex-m3 -mthumb -g
C_SOURCES	:=	syscall.c vp2.c
S_SOURCES	:=	start.s

all: vp2.bin
run: vp2.bin
	qemu-system-arm -M netduino2 -nographic -semihosting -kernel $(<)
debug: vp2.bin
	qemu-system-arm -M netduino2 -nographic -semihosting -s -S -kernel $(<)
clean:
	@rm -rf *.o *.elf *.bin
%.o: %.s
	$(ARM_AS) $(ARM_AS_OPTIONS) $(<) -o $(@)
%.o: %.c
	$(ARM_CC) $(ARM_CC_OPTIONS) -c $(<) -o $(@)
vp2.elf: vp2.ld $(S_SOURCES:.s=.o) $(C_SOURCES:.c=.o)
	$(ARM_CC) $(ARM_CC_OPTIONS) $(filter-out start.o,$(filter %.o,$(^))) -o $(@)
vp2.bin: vp2.elf
	$(ARM_OC) -O binary $(<) $(@)
