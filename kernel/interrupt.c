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
#ifdef NEZHA_D1
    volatile uint32 *const PLIC = (uint32 *)(0x10000000 + KERNEL_MAP_OFFSET);
    const usize PLIC_ID_UART0 = 18;
    const usize PLIC_CLAIM = 0x201004 >> 2;

    uint32 plic_iid;

#ifdef DEBUG
    const usize PLIC_IP_REG_UART0 = (0x1000 >> 2) + (PLIC_ID_UART0 / 32);
    uint32 plic_ip0;
    plic_ip0 = PLIC[PLIC_IP_REG_UART0];
    printf("Init PLIC pending reg is %p\n", (usize)plic_ip0);
#endif /* DEBUG */
    plic_iid = PLIC[PLIC_CLAIM];
#ifdef DEBUG
    plic_ip0 = PLIC[PLIC_IP_REG_UART0];
    printf("PLIC pending after read PLIC_CLAIM is %p\n", (usize)plic_ip0);
#endif /* DEBUG */

    if (plic_iid != PLIC_ID_UART0) {
        printf("PLIC ID is %p\n", (usize)plic_iid);
        panic("Unknown PLIC ID");
    }

    volatile uint32 *const UART0 = (uint32 *)(0x02500000 + KERNEL_MAP_OFFSET);
    const usize UART_RBR = 0x0000 >> 2;
    const usize UART_USR = 0x007C >> 2;
    const usize UART_IIR = 0x0008 >> 2;
    const uint32 UART_FLAG_RFNE = 1 << 3;
    const uint32 UART_FLAG_IID = 0x0F;
    const uint32 UART_IID_RDA = 0b0100;
    const uint32 UART_IID_CTI = 0b1100;
    const uint32 UART_IID_NONE = 0x01;
    // const uint32 UART_FEFLAG_DISABLE = 0x0;

    while (1) {
        uint32 iir = UART0[UART_IIR];
        uint32 iid = iir & UART_FLAG_IID;
#ifdef DEBUG
        const uint32 UART_FLAG_FEFLAG = 0b11000000;
        uint32 feflag = iir & UART_FLAG_FEFLAG;
        printf("FIFO is %s\n", feflag ? "enabled" : "disabled");
#endif /* DEBUG */
        if (iid == UART_IID_RDA || iid == UART_IID_CTI) {
#ifdef DEBUG
            printf("IID: %p\n", (usize)iid);
#endif /* DEBUG */
            while (1) {
                uint32 usr = UART0[UART_USR];
                uint32 rfne = usr & UART_FLAG_RFNE;
                if (!rfne) {
                    break;
                }
                uint32 rbr = UART0[UART_RBR];
                char ch = rbr & 0xFF;
#ifdef DEBUG
                printf("Read character %c, %p\n", ch, (usize)rbr);
#endif /* DEBUG */

                if (ch == '\r') {
                    pushChar('\n');
                } else {
                    pushChar(ch);
                }
            }
        } else if (iid == UART_IID_NONE) {
            break;
        } else {
            printf("Unknown IID: %p\n", (usize)iid);
        }
    }

    PLIC[PLIC_CLAIM] = plic_iid;
#ifdef DEBUG
    plic_ip0 = PLIC[PLIC_IP_REG_UART0];
    printf("PLIC pending after write PLIC_CLAIM is %p\n", (usize)plic_ip0);
#endif /* DEBUG */
#else
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
#endif /* NEZHA_D1 */
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