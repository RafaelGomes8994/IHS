#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define	R0(x)	asm volatile("mov r0, %[input]" : : [input] "i"(x) : "r0")
#define	R1(x)	asm volatile("mov r1, %[input]" : : [input] "r"(x) : "r1")
#define BKPT()	asm("bkpt 0xAB")
#undef	errno
extern int errno;

int _close(int file) {
	register int value asm("r0");
	uint32_t p[] = { file };
	R0(0x02);
	R1(p);
	BKPT();
	return value;
}

void _exit(int code) {
	uint32_t p[] = { 0x20026, code };
	R0(0x20);
	R1(p);
	BKPT();
	while(1);
}

int _fstat(int file, struct stat* st) {
	register int value asm("r0");
	//if(file < 4) file++;
	uint32_t p[] = { file };
	R0(0x0C);
	R1(p);
	BKPT();
	return value;
}

int _getpid() {
	return 1;
}

int _isatty(int file) {
	register int value asm("r0");
	uint32_t p[] = { file };
	R0(0x09);
	R1(p);
	BKPT();
	return value;
}

int _kill(int pid, int sig) {
	errno = EINVAL;
	return -1;
}

int _lseek(int file, int ptr, int dir) {
	register int value asm("r0");
	uint32_t p[] = { file, ptr };
	R0(0x0A);
	R1(p);
	BKPT();
	return value;
}

int _open(const char* name, int flags, int mode) {
	register int value asm("r0");
	uint32_t p[] = { (uint32_t)(name), (flags & 0b11) << 2, strlen(name) };
	R0(0x01);
	R1(p);
	BKPT();
	return value;
}

int _read(int file, char* ptr, int len) {
	register int value asm("r0");
	uint32_t p[] = { file, (uint32_t)(ptr), len };
	R0(0x06);
	R1(p);
	BKPT();
	return value;
}

caddr_t _sbrk(int incr) {
	extern char _end;
	extern char _estack;
	static char* heap_end = &_end;
	static char* stack_top = &_estack;
	char* prev_heap_end = heap_end;
	if((int)(heap_end) + incr > (int)(stack_top)) {
		write(1, "Memory allocation error!\n", 25);
		abort();
	}
	heap_end += incr;
	return (caddr_t)(prev_heap_end);
}

int _write(int file, char* ptr, int len) {
	register int value asm("r0");
	uint32_t p[] = { file, (uint32_t)(ptr), len };
	R0(0x05);
	R1(p);
	BKPT();
	return value;
}

void _init_stdio() {
	// stdin
	_open(":tt", 0, 0);
	// stdout
	_open(":tt", 1, 0);
	// stderr
	_open(":tt", 2, 0);
}
