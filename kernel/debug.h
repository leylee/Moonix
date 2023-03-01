#ifndef _DEBUG_H
#define _DEBUG_H
#define __PRINT_PC                                                                                 \
    {                                                                                              \
        usize pc;                                                                                  \
        asm volatile("auipc %[dest],0" : [dest] "=r"(pc));                                         \
        printf("f:%s,line:%d,PC:%p\n", __FILE__, __LINE__, pc);                                    \
    }

#define __DEBUG_PRINTREG(a)                                                                        \
    {                                                                                              \
        usize reg;                                                                                 \
        asm volatile("add %[dest]," #a ",x0" : [dest] "=r"(reg));                                  \
        printf("f:%s,line:%d,%s:%p\n", __FILE__, __LINE__, #a, reg);                               \
    }
#define __DEBUG_PRINTCSR(csr)                                                                      \
    {                                                                                              \
        usize reg;                                                                                 \
        asm volatile("csrrs %[dest]," #csr ",x0" : [dest] "=r"(reg));                              \
        printf("f:%s,line:%d,%s:%p\n", __FILE__, __LINE__, #csr, reg);                             \
    }
#define __WORKAROUD_NEED_FIX_WRONG_ROOT_PAGE_NUMBER

inline int __DEBUG_isLeaf(usize PTE);
inline usize __DEBUG_memconvert(usize vaddr, usize rootPpn);
#define printf(...)
inline usize __DEBUG_memconvert(usize vaddr, usize rootPpn)
{
    /* 虚拟地址转换为物理地址Sv39 */
    usize offset = vaddr & 0x0FFFL;
    usize orig_vaddr = vaddr;
    usize paddr;
    usize ppn;
    char flag;
    vaddr = vaddr >> 12;
    usize vpn0 = vaddr & 0x1FFL;
    vaddr = vaddr >> 9;
    usize vpn1 = vaddr & 0x1FFL;
    vaddr = vaddr >> 9;
    usize vpn2 = vaddr & 0x1FFL;
    vaddr = vaddr >> 9;
    vaddr = orig_vaddr;
    usize *L2tableAddr;
    usize *L1tableAddr;
    usize *L0tableAddr;
    L2tableAddr = (usize *)(rootPpn << 12) + vpn2;
    printf("L2 PTE: %p\n", *L2tableAddr);
    flag = __DEBUG_isLeaf(*L2tableAddr);
    if (flag == -1) {
        printf("Page Fault at L2,VA%p\n", vaddr);
        return -1;
    }
    ppn = ((*L2tableAddr) << 10) >> 20 & 0xFFFFFFFFFFF;
    if (flag == 1) {
        paddr = (ppn << 12) + (vpn0 << 12) + (vpn1 << 21) + offset;
        printf("Conversion Stop at L2, %p -> %p\n", vaddr, paddr);
        return paddr;
    } else {
        L1tableAddr = (usize *)(ppn << 12) + vpn1;
        flag = __DEBUG_isLeaf(*L1tableAddr);
        if (flag == -1) {
            printf("Page Fault at L1,VA%p\n", vaddr);
            return -1;
        }
        ppn = ((*L1tableAddr) << 10) >> 20 & 0xFFFFFFFFFFF;
        if (flag == 1) {
            paddr = (ppn << 12) + (vpn0 << 12) + offset;
            printf("Conversion Stop at L1, %p -> %p\n", vaddr, paddr);
            return paddr;
        } else {
            L0tableAddr = (usize *)(ppn << 12) + vpn0;
            flag = __DEBUG_isLeaf(*L0tableAddr);
            if (flag == -1) {
                printf("Page Fault at L0,VA%p\n", vaddr);
                return -1;
            }
            ppn = ((*L0tableAddr) << 10) >> 20 & 0xFFFFFFFFFFF;
            if (flag == 1) {
                paddr = (ppn << 12) + offset;
                printf("Conversion Finished at L0, %p -> %p\n", vaddr, paddr);
                return paddr;
            } else
                printf("Error in Conversion VA:%p, unfinished at L0\n", vaddr);
        }
    }
    return -1;
}
#undef printf
inline int __DEBUG_isLeaf(usize PTE)
{
    usize v = PTE & 0x1L;
    usize r = (PTE & 0x2L) >> 1;
    usize w = (PTE & 0x4L) >> 2;
    usize x = (PTE & 0x8L) >> 3;
    if (!v)
        return -1;
    if ((r | w | x) == 1)
        return 1;
    else
        return 0;
}

#endif