ARM_PREFIX	:=	arm-none-eabi
ARM_AS		:=	$(ARM_PREFIX)-as
ARM_CC		:=	$(ARM_PREFIX)-gcc
ARM_LD		:=	$(ARM_PREFIX)-ld
ARM_OC		:=	$(ARM_PREFIX)-objcopy
ARM_AS_OPTIONS	:=	-mcpu=cortex-m3 -mthumb -g
ARM_CC_OPTIONS	:=	-Wall -O3 -nostdlib -nostartfiles -mcpu=cortex-m3 -mthumb -c -g

all: vp1.bin
run: vp1.bin
	qemu-system-arm -M netduino2 -nographic -kernel $(<)
clean:
	@rm -rf *.o *.elf *.bin
start.o: start.s
	$(ARM_AS) $(ARM_AS_OPTIONS) $(<) -o $(@)
vp1.o: vp1.c
	$(ARM_CC) $(ARM_CC_OPTIONS) $(<) -o $(@)
vp1.elf: vp1.ld start.o vp1.o
	$(ARM_LD) -T $(^) -o $(@)
vp1.bin: vp1.elf
	$(ARM_OC) -O binary $(<) $(@)
