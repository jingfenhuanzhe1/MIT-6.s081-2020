	#    
        # code to switch between user and kernel space.
        #
        # this code is mapped at the same virtual address
        # (TRAMPOLINE) in user and kernel space so that
        # it continues to work when it switches page tables.
	#
	# kernel.ld causes this to be aligned
        # to a page boundary.                  1、系统调用的ecall指令会将用户模式改为管理者模式
        #                                      2、ecall将程序计数器的值保存在sepc寄存器
	.section trampsec                     #3、ecall会跳转到stvec寄存器指向的指令（stvec指向uservec）
.globl trampoline         #物理页面的开始
trampoline:                                   # 1.保存现场（32个通用寄存器）                                                        
.align 4                                      # 2.把内核的pagetable、内核的stack以及执行进程的CPU号转载到寄存器中
.globl uservec             #该页面中的开始     # 3.跳转到usertrap继续执行
uservec:                                      # 4.设置scause以反映产生陷阱的原因
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #
        # sscratch points to where the process's p->trapframe is    sscratch指向trapframe
        # mapped into user space, at TRAPFRAME.
        #
        
	# swap a0 and sscratch
        # so that a0 is TRAPFRAME
        csrrw a0, sscratch, a0             #执行完之后，a0包含sscratch的值， sscratch中的包含a0的值

        # save the user registers in TRAPFRAME
        sd ra, 40(a0)     #从a0的40偏移个偏移量开始存储， 0偏移量-32偏移量不用存储，是用来加载(load)的
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)                #sd (store double word)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0           sscratch中包含的是trapframe的值
        csrr t0, sscratch    #之前a0未保存，在一开始与sscratch交换了值，现在将其赋给t0，再保存到trapframe中
        sd t0, 112(a0)        #现在a0中是trapframe的地址了

        # restore kernel stack pointer from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), p->trapframe->kernel_trap
        ld t0, 16(a0)

        # restore kernel page table from p->trapframe->kernel_satp
        ld t1, 0(a0)
        csrw satp, t1
        sfence.vma zero, zero              #刷新快表

        # a0 is no longer valid, since the kernel page
        # table does not specially map p->tf.

        # jump to usertrap(), which does not return
        jr t0

.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # switch from kernel to user.
        # usertrapret() calls here.
        # a0: TRAPFRAME, in user page table.            TRAPFRAME
        # a1: user page table, for satp.                satp

        # switch to the user page table.
        csrw satp, a1                           #切换pagetable
        sfence.vma zero, zero                   #清除页表缓存

        # put the saved user a0 in sscratch, so we
        # can swap it with our a0 (TRAPFRAME) in the last step.
        ld t0, 112(a0)              #a0中是 trapframe        112(a0)中是函数调用的返回值
        csrw sscratch, t0                     #将返回值保存到sscratch中

        # restore all but a0 from TRAPFRAME
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0, and save TRAPFRAME in sscratch
        csrrw a0, sscratch, a0           #a0中包含将要返回的值， sscratch中包含trapfram页的地址
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        sret          #切换会用户模式，SEPC寄存器的数值会被拷贝到PC寄存器中, 重新打开中断
                      # sstatus中的SPP位来设置返回的模式。SPIE位来打开中断