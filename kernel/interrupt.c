/*
 *  kernel/interrupt.c
 *
 *  (C) 2021  Ziyang Guo
 */

#include "interrupt.h"
#include "consts.h"
#include "context.h"
#include "def.h"
#include "riscv.h"
#include "stdin.h"
#include "types.h"

asm(".include \"kernel/interrupt.asm\"");

/*
 * OpenSBI默认会关闭所有的外部中断和串口设备中断（键盘中断），以防止初始化过程被打断
 * 这里需要手动打开
 * 写法来源：https://github.com/rcore-os/rCore/blob/3ac4d7a607dbe81167f5d6ad799bc91682ab9f7d/kernel/src/arch/riscv/board/virt/mod.rs
 */

#ifdef NEZHA_D1
void initExternalInterrupt()
{
    const usize PLIC = 0x10000000;
    const usize UART0_IRQ = 18;
    const usize PLIC_PRIOn = PLIC;
    const usize PLIC_SIEn = PLIC + 0x0002080;
    const usize PLIC_MTH = PLIC + 0x201004;
    const usize PLIC_STH = PLIC + 0x201000;
    // set desired IRQ priorities non-zero (otherwise disabled).
    /* PLIC UART0 优先级设置 */
    *(uint32 *)(PLIC_PRIOn + UART0_IRQ * 4 + KERNEL_MAP_OFFSET) = 1;
    /* PLIC UART0 中断使能 */
    *(uint32 *)(PLIC_SIEn + UART0_IRQ / 32 + KERNEL_MAP_OFFSET) = (1 << UART0_IRQ);
    /* PLIC M mode 和 S mode 中断阈值设置, 优先级高于此阈值的中断会被响应 */
    *(uint32 *)(PLIC_MTH + KERNEL_MAP_OFFSET) = 0;
    *(uint32 *)(PLIC_STH + KERNEL_MAP_OFFSET) = 0;
}

void initSerialInterrupt()
{
    const usize UART0 = 0x02500000;
    /* Interrupt Enable Register */
    const usize UART0_IER = UART0 + 0x0004;
    /* Enable Received Data Available Interrupt bit */
    const usize UART0_IER_ERBFI = (1 << 0);
    // /* Line Control Register */
    // const usize UART0_LCR = UART0 + 0x000C;
    // /* Divisor Latch Acces bit */
    // const usize UART0_LCR_DLAB = (1 << 7);

    // uint32 val;
    // val = *(uint32 *)UART0_LCR;
    // val &= ~

    *(uint32 *)(UART0_IER + KERNEL_MAP_OFFSET) = UART0_IER_ERBFI;
}
#else  /* NEZHA_D1 */
void initExternalInterrupt()
{
    *(uint32 *)(0x0C002080 + KERNEL_MAP_OFFSET) = 0x400U;
    *(uint32 *)(0x0C000028 + KERNEL_MAP_OFFSET) = 0x7U;
    *(uint32 *)(0x0C201000 + KERNEL_MAP_OFFSET) = 0x0U;
}

void initSerialInterrupt()
{
    *(uint8 *)(0x10000001 + KERNEL_MAP_OFFSET) = 0x01U;
    *(uint8 *)(0x10000004 + KERNEL_MAP_OFFSET) = 0x0bU;
}
#endif /* NEZHA_D1 */

void initInterrupt()
{
    /*
     * 设置 stvec 寄存器
     * 设置中断处理函数和处理模式
     */
    extern void __interrupt();
    w_stvec((usize)__interrupt | MODE_DIRECT);

    /* 开启外部中断 */
    w_sie(r_sie() | SIE_SEIE);

    /* 打开 OpenSBI 的外部中断响应和串口设备响应 */
    initExternalInterrupt();
    initSerialInterrupt();

    printf("***** Init Interrupt *****\n");
}

/*
 * 处理外部中断
 * 由于目前外部设备只打开了串口设备
 * 所以只需要处理键盘中断
 */
void external()
{
    usize ret = consoleGetchar();
    /*
     * 调用 SBI 接口获得输入的字符，可能出现错误
     * 所有获取到的字符都被存入标准输入缓冲区
     */
    if (ret != -1) {
        char ch = (char)ret;
        if (ch == '\r') {
            pushChar('\n');
        } else {
            pushChar(ch);
        }
    }
}

/*
 * 断点中断
 * 输出断点信息并跳转到下一条指令
 */
void breakpoint(InterruptContext *context)
{
    printf("Breakpoint at %p\n", context->sepc);
    /* ebreak 指令长度 2 字节 */
    context->sepc += 2;
}

/*
 * 处理来自 U-Mode 的系统调用
 * 返回值存储在 x10（a0）寄存器
 */
void handleSyscall(InterruptContext *context)
{
    context->sepc += 4;
    extern usize syscall(usize id, usize args[3], InterruptContext * context);
    usize ret =
        syscall(context->x[17], (usize[]){context->x[10], context->x[11], context->x[12]}, context);
    context->x[10] = ret;
}

/*
 * 时钟中断，主要用于调度
 * 设置下一次时钟中断时间并通知调度器检查当前线程时间片
 */
void supervisorTimer()
{
    // printf("supervisorTimer\n");
    extern void tick();
    tick();
    extern void tickCPU();
    tickCPU();
}

/*
 * 未知中断
 * 直接打印信息并关机
 */
void fault(InterruptContext *context, usize scause, usize stval)
{
    printf("Unhandled interrupt!\nscause\t= %p\nsepc\t= %p\nstval\t= %p\n", scause, context->sepc,
           stval);
    panic("");
}

void handleInterrupt(InterruptContext *context, usize scause, usize stval)
{
    switch (scause) {
    case BREAKPOINT:
        breakpoint(context);
        break;
    case USER_ENV_CALL:
        handleSyscall(context);
        break;
    case SUPERVISOR_TIMER:
        supervisorTimer();
        break;
    case SUPERVISOR_EXTERNAL:
        external();
        break;
    default:
        fault(context, scause, stval);
        break;
    }
}