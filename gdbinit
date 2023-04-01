file kernel/Kernel
target remote localhost:1025
hb _start
set $pc=0x80000000