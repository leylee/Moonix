#
#  Makefile
#  
#  (C) 2021  Ziyang Guo
#

K=kernel
U=user

OBJS = 						\
	$K/sbi.o				\
	$K/printf.o				\
	$K/interrupt.o			\
	$K/timer.o				\
	$K/heap.o				\
	$K/memory.o				\
	$K/mapping.o			\
	$K/thread.o				\
	$K/threadpool.o			\
	$K/processor.o			\
	$K/rrscheduler.o		\
	$K/syscall.o			\
	$K/elf.o				\
	$K/string.o				\
	$K/fs.o					\
	$K/queue.o				\
	$K/condition.o			\
	$K/stdin.o				\
	$K/main.o

UPROSBASE =					\
	$U/entry.o				\
	$U/malloc.o				\
	$U/io.o					\
	$U/string.o				\

UPROS =						\
	hello					\
	fib50					\
	sh

# 设置交叉编译工具链
TOOLPREFIX := riscv64-linux-gnu-
ifeq ($(shell uname),Darwin)
	TOOLPREFIX=riscv64-unknown-elf-
endif
CC = $(TOOLPREFIX)gcc -g
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# QEMU 虚拟机
QEMU = qemu-system-riscv64

# gcc 编译选项
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.

CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# 适配 D1
ifdef PLATFORM
	CFLAGS += -D$(PLATFORM)
endif
ifdef DEBUG
	CFLAGS += -DDEBUG
endif
ifndef OPENSBI_CROSS_COMPILE
	OPENSBI_CROSS_COMPILE := $(TOOLPREFIX)
endif
ifndef DEBUG_SERVER
	DEBUG_SERVER := T-HeadDebugServer
endif

# ld 链接选项
LDFLAGS = -z max-page-size=4096

# QEMU 启动选项
QEMUOPTS = -machine virt -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin -device loader,file=Image,addr=0x80200000 --nographic 

all: Image

Image: Kernel

Kernel: User $(subst .c,.o,$(wildcard $K/*.c)) $K/*.h $K/*.asm $K/*.ld
	$(LD) $(LDFLAGS) -Map=Kernel.map -T $K/kernel.ld -o $K/Kernel $(OBJS)
	$(OBJCOPY) $K/Kernel -O binary Image

User: mksfs $(subst .c,.o,$(wildcard $U/*.c))
	mkdir -p rootfs/bin
	for file in $(UPROS); do											\
		$(LD) $(LDFLAGS) -o rootfs/bin/$$file $(UPROSBASE) $U/$$file.o;	\
	done
	./mksfs
debug: Image
	$(QEMU) $(QEMUOPTS) -s -S
mksfs:
	gcc mkfs/mksfs.c -o mksfs

# compile all .c file to .o file
$K/%.o: $K/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$U/%.o: $U/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f */*.d */*.o $K/Kernel Image Image.asm mksfs fs.img fw_jump.bin
	rm -rf rootfs
	make -C OpenSBI-C906 clean
	
asm: Kernel
	$(OBJDUMP) -S $K/Kernel > Image.asm

qemu: Image
	$(QEMU) $(QEMUOPTS)

GDBPORT = $(shell expr `id -u` % 5000 + 25000)
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

qemu-gdb: Image asm
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

fw_jump.bin:
	make -C OpenSBI-C906 CROSS_COMPILE=$(OPENSBI_CROSS_COMPILE) PLATFORM=thead/c910 FW_JUMP_ADDR=0x80200000 FW_TEXT_START=0x80000000 PLATFORM_RISCV_ISA=rv64gcxthead -j16
	cp OpenSBI-C906/build/platform/thead/c910/firmware/fw_jump.bin ./

.PHONY: xfel-load
xfel-load: fw_jump.bin Image
	xfel version
	xfel ddr d1
	xfel write 0x80000000 fw_jump.bin
	xfel write 0x80200000 Image

.PHONY: xfel-run
xfel-run: xfel-load
	xfel exec 0x80000000

.PHONY: xfel-gdb
xfel-gdb: xfel-load
	$(TOOLPREFIX)gdb -x gdbinit
	
.PHONY: debug-server
debug-server:
	$(DEBUG_SERVER)