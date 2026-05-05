all: modexp.elf

run: modexp.elf
	@./$(<)

%.elf: %.c
	gcc -Wall -O3 $(<) -o $(@)

clean:
	@rm *.elf
