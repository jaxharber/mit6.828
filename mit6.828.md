# 一、mit6.828

## 1. Lab1 Booting a PC

## 1.1 启动流程概述

### 1.1.1 加载内核至memory

- x86 PC boots启动BIOS

  BIOS prepare the hardware and then transfmer control to the operating system.

  BIOS’s job：to prepare the hardware and transfer control to code loaded from the boot sector（the first
  512-byte sector of the boot disk.）BIOS loads the boot sector at memory address 0x7c00 and then jumps (sets the processor’s %ip) to that address。（将装载程序加载至内存0X7c00）

- the loader’s（two source files：bootasm.S， bootmain.c） job：to load the xv6 kernel  at physical address 0x100000 from disk into memory, and then to transfer control to the kernel.(执行装载程序-->加载内核执行程序到0x100000)
- executes it starting at entry(entry.s): The x86 paging hardware is not enabled when the kernel starts; virtual addresses map directly to physical addresses.（执行内核程序）

- To allow the rest of the kernel to run，entry sets up a page table（初始化页表，建立内存映射）

  两个条目：

  entry 0: [0, 4M)--->[0, 4M)

  entry 512: virtual : [ 0x80000000 (called KERNBASE), KERBASE+4M)---->physical:[0, 4M)

  ```c
  # xv6/main.c
  1306 pde_t entrypgdir[NPDENTRIES] = {
  1307 // Map VA’s [0, 4MB) to PA’s [0, 4MB)
  1308 [0] = (0) | PTE_P | PTE_W | PTE_PS,
  1309 // Map VA’s [KERNBASE, KERNBASE+4MB) to PA’s [0, 4MB)
  1310 [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
  1311 };
  ```

  

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\v20p.PNG" alt="v20p" style="zoom: 67%;" />

​																					Figure 1-1. Layout of a virtual address space

```c
xv6/entry.S:
# Entering xv6 on boot processor, with paging off.
1043 .globl entry
1044 entry:
1045 # Turn on page size extension for 4Mbyte pages (4M的内核空间)
1046 movl %cr4, %eax
1047 orl $(CR4_PSE), %eax
1048 movl %eax, %cr4
1049 # Set page directory(获取页表入口物理地址)
1050 movl $(V2P_WO(entrypgdir)), %eax
1051 movl %eax, %cr3
1052 # Turn on paging. To enable the paging hardware,xv6 sets the flag CR0_PG in %cr0.启用分页硬件
1053 movl %cr0, %eax
1054 orl $(CR0_PG|CR0_WP), %eax
1055 movl %eax, %cr0
1056 
1057 # Set up the stack pointer.
1058 movl $(stack + KSTACKSIZE), %esp
1059 
1060 # Jump to main(), and switch to executing at
1061 # high addresses. The indirect call is needed because
1062 # the assembler produces a PC−relative instruction
1063 # for a direct jump.
1064 mov $main, %eax
1065 jmp *%eax
1066 
1067 .comm stack, KSTACKSIZE
```



- Now entry needs to transfer to the kernel’s C code, and run it in high memory,Finally entry jumps to main.（转到高地址运行）

  Now the kernel is running in high addresses in the function main

### 1.1.2 建立第一个进程

- main (1217) initializes several devices and subsystems, it creates the first process by calling userinit (2520).初始化并创建进程

```c
//xv6/main.c
1216 int
1217 main(void)
1218 {
1219 kinit1(end, P2V(4*1024*1024)); // phys page allocator
1220 kvmalloc(); // kernel page table
1221 mpinit(); // detect other processors
1222 lapicinit(); // interrupt controller
1223 seginit(); // segment descriptors
1224 picinit(); // disable pic
1225 ioapicinit(); // another interrupt controller
1226 consoleinit(); // console hardware
1227 uartinit(); // serial port
1228 pinit(); // process table
1229 tvinit(); // trap vectors
1230 binit(); // buffer cache
1231 fileinit(); // file table
1232 ideinit(); // disk
1233 startothers(); // start other processors
1234 kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
1235 userinit(); // first user process
1236 mpmain(); // finish this processor’s setup ,calls scheduler to start running processes (1257).
1237 }
```

- userinit创建用户进程

  将initcode.S代码加载到用户空间（0x0)、初始化trap frame、设置栈指针和运行状态

```c
//xv6/proc.c
2518 // Set up first user process.
2519 void
2520 userinit(void)
2521 {
2522 struct proc *p;
2523 extern char _binary_initcode_start[], _binary_initcode_size[];
2524 
2525 p = allocproc();//在进程表中分配一个插槽（一个struct proc）， change state to EMBRYO并初始化在内核执行时所需的进程状态部分
2526 
2527 initproc = p;
2528 if((p−>pgdir = setupkvm()) == 0)//to create a page table创建页表（建立地址空间映射）
2529 panic("userinit: out of memory?");
2530 inituvm(p−>pgdir, _binary_initcode_start, (int)_binary_initcode_size);//copies that binary(initcode.S) 																			into the new process’s memory
2531 p−>sz = PGSIZE;
2532 memset(p−>tf, 0, sizeof(*p−>tf));
2533 p−>tf−>cs = (SEG_UCODE << 3) | DPL_USER;//设置%cs CPL=3
2534 p−>tf−>ds = (SEG_UDATA << 3) | DPL_USER;
2535 p−>tf−>es = p−>tf−>ds;
2536 p−>tf−>ss = p−>tf−>ds;
2537 p−>tf−>eflags = FL_IF;
2538 p−>tf−>esp = PGSIZE;
2539 p−>tf−>eip = 0; // beginning of initcode.S
2540 
2541 safestrcpy(p−>name, "initcode", sizeof(p−>name));
2542 p−>cwd = namei("/");
2543 
2544 // this assignment to p−>state lets other cores
2545 // run this process. the acquire forces the above
2546 // writes to be visible, and the lock is also needed
2547 // because the assignment might not be atomic.
2548 acquire(&ptable.lock);
2549 
2550 p−>state = RUNNABLE;
2551 
2552 release(&ptable.lock);
2553 }
setupkvm: to create a page table for the process with (at first)mappings only for memory that the kernel uses.创建页表
inituvm: allocates one page of physical memory, maps virtual address zero to that memory, and copies the binary to that page (1886).分配一页物理内存
```

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\slot.PNG" style="zoom:67%;" />

​																						Figure 1-2. A new kernel stack.(在内核执行时使用)

### 1.1.3 运行第一个进程

- mpmain calls scheduler（2758） to start running processes (1257).

  ```c
  //xv6/proc.c
  2757 void
  2758 scheduler(void)
  2759 {
  2760 struct proc *p;
  2761 struct cpu *c = mycpu();
  2762 c−>proc = 0;
  2763 
  2764 for(;;){
  2765 // Enable interrupts on this processor.
  2766 sti();
  2767 
  2768 // Loop over process table looking for process to run.
  2769 acquire(&ptable.lock);
  2770 for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  2771 if(p−>state != RUNNABLE)
  2772 continue;
  2773 
  2774 // Switch to chosen process. It is the process’s job
  2775 // to release ptable.lock and then reacquire it
  2776 // before jumping back to us.
  2777 c−>proc = p;
  2778 switchuvm(p);//启用p进程的页表，同时设置TSS段
  2779 p−>state = RUNNING;
      
  2780  #3052 void swtch(struct context **old, struct context *new);
  2781 swtch(&(c−>scheduler), p−>context);//当前cpu上下文环境保存在c−>scheduler中，然后切换到p的上下文中
  2782 switchkvm();
  2783 
  2784 // Process is done running for now.
  2785 // It should have changed its p−>state before coming back.
  2786 c−>proc = 0;
  2787 }
  2788 release(&ptable.lock);
  2789 
  2790 }
  2791 }
  switchuvm: to tell the hardware to start using the target process’s page table (1879).also sets up a task
  			state segment SEG_TSS
  setupkvm: causes all processes’ page tables to have identical mappings for kernel code and data.
  swtch: to perform a context switch to the target process’s kernel thread.
  ```

- starts executing forkret.(执行 forkret)

  ```c
  //xv6/proc.c
  2852 void
  2853 forkret(void)
  2854 {
  2855 static int first = 1;
  2856 // Still holding ptable.lock from scheduler.
  2857 release(&ptable.lock);
  2858 
  2859 if (first) {
  2860 // Some initialization functions must be run in the context
  2861 // of a regular process (e.g., they call sleep), and thus cannot
  2862 // be run from main().
  2863 first = 0;
  2864 iinit(ROOTDEV);//必须运行在具有内核栈的常规进程上下文中
  2865 initlog(ROOTDEV);
  2866 }
  initialization functions: cannot be run from main because they must be run in the context of a regular 							   process with its own kernel stack.
  ```

- trapret begins executing,with %esp set to p->tf.（执行trapret）

  the processor continues at the %eip specified in the trap frame.

  For initproc, that means virtual address zero, the first instruction of initcode.S.

  ```c
  //xv6/trapasm.S
  3323 .globl trapret
  3324 trapret:
  3325 popal
  3326 popl %gs
  3327 popl %fs
  3328 popl %es
  3329 popl %ds
  3330 addl $0x8, %esp #skips over the two fields trapno and errcode 
  3331 iret  //pops %cs, %eip, %flags, %esp, and %ss from the stack.
  //to restore registers from the trap framejust as swtch did with the kernel context
  ```

  At this point, %eip holds zero and %esp holds 4096. These are virtual addresses in the process’s address space.

  The processor’s paging hardware translates them into physical addresses.(处理器进行地址翻译)接下来运行在用户空间中

  ![img](C:\Users\Administrator.MICRO-901192021\Pictures\prot.jpg)

  ​                                                                            Figure 1-3. 实模式寻址过程

  1. 寻址时，先找到gdtr寄存器，从中得到GDT的基址
  2. 有了GDT的基址，又有段寄存器中保存的索引，可以得到段寄存器“所指”的那个表项，既所指的那个描述符
  3. 得到了描述符，就可以从描述符中得到该描述符所描述的那个段的起始地址
  4. 有了段的起始地址，将偏移地址拿过来与之相加，便能得到最后的**线性地址**
  5. 有了线性地址（虚拟地址），经过变换，即可得到相应的物理地址

- begins executing initcode.S（执行initcode.S）

  ```c
  //xv6/initcode.S
  //exec
  8407 # exec(init, argv)//If all goes well, exec never returns: it starts running the program named by 							$init,, which is  "/init\0"
  8408 .globl start
  8409 start:
  8410 pushl $argv
  8411 pushl $init
  8412 pushl $0 // where caller pc would be
  8413 movl $SYS_exec, %eax
  8414 int $T_SYSCALL//asking the kernel to run the exec system call(进入内核运行)
  8415 
  8416 # for(;;) exit();//不会返回
  8417 exit:
  8418 movl $SYS_exit, %eax
  8419 int $T_SYSCALL
  8420 jmp exit
  8421 
  8422 # char init[] = "/init\0";
  8423 init:
  8424 .string "/init\0"
  8425 
  8426 # char *argv[] = { init, 0 };
  8427 .p2align 2
  8428 argv:
  8429 .long init
  8430 .long 0
      
  //SYSCALL
  8453 #define SYSCALL(name) \
  8454 .globl name; \
  8455 name: \
  8456 movl $SYS_ ## name, %eax; \
  8457 int $T_SYSCALL; \
  8458 ret
  8459 
  8460 SYSCALL(fork)
  8461 SYSCALL(exit)
  8462 SYSCALL(wait)
  8463 SYSCALL(pipe)
  8464 SYSCALL(read)
  8465 SYSCALL(write)
  8466 SYSCALL(close)
  8467 SYSCALL(kill)
  8468 SYSCALL(exec)
  8469 SYSCALL(open)
  8470 SYSCALL(mknod)
  8471 SYSCALL(unlink)
  8472 SYSCALL(fstat)
  8473 SYSCALL(link)
  8474 SYSCALL(mkdir)
  8475 SYSCALL(chdir)
  8476 SYSCALL(dup)
  8477 SYSCALL(getpid)
  8478 SYSCALL(sbrk)
  8479 SYSCALL(sleep)
  8480 SYSCALL(uptime)
  ```

- the process will run /init

  ```c
  //xv6/init.c  init: The initial user−level program
  8509 int
  8510 main(void)
  8511 {
  8512 int pid, wpid;
  8513 
  8514 if(open("console", O_RDWR) < 0){//打开新终端
  8515 mknod("console", 1, 1);
  8516 open("console", O_RDWR);
  8517 }
  8518 dup(0); // stdout
  8519 dup(0); // stderr
  8520 
  8521 for(;;){
  8522 printf(1, "init: starting sh\n");
  8523 pid = fork();
  8524 if(pid < 0){
  8525 printf(1, "init: fork failed\n");
  8526 exit();
  8527 }
  8528 if(pid == 0){
  8529 exec("sh", argv);//启动sh
  8530 printf(1, "init: exec sh failed\n");
  8531 exit();
  8532 }
  8533 while((wpid=wait()) >= 0 && wpid != pid)//回收子进程
  8534 printf(1, "zombie!\n");
  8535 }
  ```

  

## 1.2Lab环节

### 1.2.1 git使用

```c
git commit -am ""    //提交
make handin			//提交脚本
git diff 			//追踪变化
make tarball       //上传文件
make grade 		   //打分
    
```

### 1.2.2 Part 1: PC Bootstrap

#### 1.2.2.1 **Exercise 1**

1. 执行make指令，获得镜像文件obj/kern/kernel.img

   ```c
   cd lab
   make
   ```

2. run QEMU

   ​	使用obj/kern/kernel.img`文件作为模拟器的磁盘，该文件包含boot loader (`obj/boot/boot`) 和 our kernel (`obj/kernel`).

   ```c
   make qemu or make qemu-nox(无窗口显示)
   该qemu只有两个命令：help kerninfo
   ```

3. The PC's Physical Address Space

   <img src="C:\Users\Administrator.MICRO-901192021\Pictures\phy.PNG" style="zoom: 67%;" />

   ​																Figure 1-2.  A PC's physical address space

   area from 0x000A0000 through 0x000FFFFF(384KB): was reserved by the hardware for special uses such as video display 	             																							  buffers and firmware held in non-volatile memory. 

   0x000F0000 through 0x000FFFFF(64KB):BIOS

4. the rom bios,QEMU's debugging

   启动两个窗口，分别执行命令

   ```c
   //window 1                                                      //window2
   make qemu-nox-gdb                                                make gdb
    																使用si指令
   ```
   
    On processor reset, the (simulated) processor enters **real mode** and sets CS to 0xf000 and the IP to 0xfff0, so that execution begins at that (CS:IP) segment address.[f000:fff0]

 real mode addressing：In real mode (the mode that PC starts off in), address translation works according to the formula: *physical address* = 16 * *segment* + *offset*. （实模式下的地址翻译）

BIOS工作：When the BIOS runs, it sets up **an interrupt descriptor table** and initializes various devices such as the **VGA display**. 					This is where the "`Starting SeaBIOS`" message you see in the QEMU window comes from.After initializing the PCI 					bus and all the important devices the BIOS knows about, it searches for a bootable device such as a floppy, hard drive, 					or CD-ROM. Eventually, when it finds a bootable disk, the BIOS reads **the *boot loader*** from the disk and transfers 					control to it.

### 1.2.2 Part 2: The Boot Loader

#### 1.2.2.1 **Exercise 3**

granularity：粒度	arbitrary：任意的

the *boot sector*（where the boot loader code resides）：the first sector of the disk

When the BIOS finds a bootable floppy or hard disk, it loads the **512-byte** boot sector into memory at physical addresses 0x7c00 through 0x7dff, and then uses a `jmp` instruction to set the CS:IP to `0000:7c00`，passing control to the boot loader.

The **boot loader** consists of one assembly language source file, `boot/boot.S`, and one C source file, `boot/main.c`

1. boot loader部分功能概括

   - First, the boot loader switches the processor from real mode to *32-bit protected mode*（in sections 1.2.7 and 1.2.8 of  pcasm-book.pdf）, because it is only in this mode that software can access all the memory above 1MB in the processor's physical address space.(切换到保护模式---地址翻译方法改变)
   - Second, the boot loader **reads the kernel** from the hard disk by directly accessing the IDE disk device registers via the x86's special I/O instructions.

2.  look at the file `obj/boot/boot.asm`.

   This file is a disassembly of the boot loader that our GNUmakefile creates *after* compiling the boot loader.This disassembly file makes it easy to see exactly where in physical memory all of the boot loader's code resides, and makes it easier to track what's happening while stepping through the boot loader in GDB. （便于gdb时进行地址追踪）

   ```c
   x/Ni ADDR Examine first N instructions of address ADDR
    //获取内存指令   
    x/[NUM][SIZE][FORMAT] where
    NUM = number of objects to display
    SIZE = size of each object (b=byte, h=half-word, w=word,g=giant (quad-word))
    FORMAT = how to display each object (d=decimal, x=hex, o=octal, etc.)
     
     x/Nx ADDR prints N words of memory at ADDR.
    //设置断点
     b *0x7c00
   
   ```
   
   #### 1.2.2.2 **Exercise 5**
   
   ```c
     change the link address in boot/Makefrag//设置了BIOS装载完后执行程序的入口地址为0x7c00
     make clean
     make
     trace into the boot loader again to see what happens.
     
      Examine the 8 words of memory at 0x00100000 at the point the BIOS enters the boot loader, and then 	    again at the point the boot loader enters the kernel.
      检查BIOS进入引导加载程序时0x00100000处的8个内存字，然后再次检查引导加载程序进入内核时的8个内存字
      x/8x 0x100000
      b *0x7c00
      b *0x7d81
   ```
   
   ![](C:\Users\Administrator.MICRO-901192021\Pictures\e5.PNG)
   
   ![](C:\Users\Administrator.MICRO-901192021\Pictures\5e.PNG)

​                                                                                           Figure 1-3. 0x10000地址对比图

## 1.2.3 Part 3: The Kernel

- When you inspected the boot loader's link and load addresses above, they matched perfectly, but there was a (rather large) disparity between the *kernel's* link address (as printed by objdump) and its load address.（链接地址与装载地址不一样?）

  Operating system kernels often like to be linked and run at very high *virtual address*, such as 0xf0100000, in order to **leave the lower part of the processor's virtual address space for user programs to use**. (给用户预留空间)

  Many machines **don't have any physical memory at address 0xf0100000**, so we can't count on being able to store the kernel there.（物理地址有限）

  we will use the processor's memory management hardware to map virtual address 0xf0100000 (the link address at which the kernel code *expects* to run) to physical address 0x00100000 (where the boot loader loaded the kernel into physical memory).（内存映射）

   `kern/entry.S` sets the `CR0_PG` flag（**启用页表**），在此之前线性地址等同于物理地址（boot/boot.S set up an identity mapping from linear addresses to physical addresses）

  ```c
  //kern/entrypgdir.c
  pde_t entry_pgdir[NPDENTRIES] = {
  	// Map VA's [0, 4MB) to PA's [0, 4MB)
  	[0]
  		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
  	// Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB) KERNBASE: 0xf0000000
      //0xf0000000 through 0xf0400000-->0x00000000 through 0x00400000
  	[KERNBASE>>PDXSHIFT]
  		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
  };
  
  
  ```

  #### 1.2.3.1 **Exercise 7**
  
  ```c
  Use QEMU and GDB to trace into the JOS kernel and stop at the `movl %eax, %cr0`. Examine memory at 0x00100000 and at 0xf0100000. Now, single step over that instruction using the stepi GDB command. Again, examine memory at 0x00100000 and at 0xf0100000. Make sure you understand what just happened.  
    
   PG：CR0的位31是分页（Paging）标志。当设置该位时即开启了分页机制；当复位时则禁止分页机制，此时所有线性地址等同于物理地址。在开启这个标志之前必须已经或者同时开启PE标志。即若要启用分页机制，那么PE和PG标志都要置位。
      
   CR3: 是页目录基址寄存器，保存页目录表的物理地址，页目录表总是放在以4K字节为单位的存储器边界上，因此，它的地址的低12位总为  0，不起作用，即使写上内容，也不会被理会。
  ```
  
  //movl %eax, %cr0 之前

![](C:\Users\Administrator.MICRO-901192021\Pictures\e7.PNG)

​	

​		//movl %eax, %cr0 之后

![](C:\Users\Administrator.MICRO-901192021\Pictures\7e.PNG)

此时虚拟地址0xf0100000映射到物理地址0x100000(kernel起始)

#### 1.2.3.2 **Exercise 8**

```c
We have omitted a small fragment of code - the code necessary to print octal numbers using patterns of the form "%o". Find and fill in this code fragment.
Read through kern/printf.c, lib/printfmt.c, and kern/console.c
```

- va_list,va_start,va_end 以 及 lib/print.c 中的 va_arg 就是用于获取不定个数参数的宏.

  具体可参考[va_list博客](http://blog.sina.com.cn/s/blog_149e9d2ec0102wyn8.html )

  当你的函数的**参数个数不确定**时，就可以使用上述宏进行动态处理，这无疑为你的程序增加了灵活性

  1. va_list的使用方法：

  a)  首先在函数中定义一个具有va_list型的变量，这个变量是指向参数的指针。

  b)  然后用va_start宏初始化变量刚定义的va_list变量，使其指向第一个可变参数的地址。

  c)  然后va_arg返回可变参数，va_arg的第二个参数是你要返回的参数的类型（如果多个可变参数，依次调用va_arg获取各个参数）。

  d)  最后使用va_end宏结束可变参数的获取。

  

  2. 在使用va_list是应该注意一下问题：

     1）  可变参数的类型和个数完全由代码控制，它并**不能智能地识别不同参数的个数和类型**。

     2）  如果我们不需要一一详解每个参数，只需要将可变列表拷贝到某个缓冲区，可以用vsprintf函数。

     3）  因为编译器对可变参数的函数原型检查不够严格，对编程查错不利，不利于我们写出高质量的代码。

     

     3. va_list,va_start,va_arg,va_end宏的定义及本质

        ```c
        a)   va_list型变量：
        #ifdef    _M_ALPHA
        typedef   struct{
        char*　　a0;　　　　
        int　　offset;　　　　
        }va_list;
        #else
        typedef   char*　　va_list;#endif
        
        b)  _INTSIZEOF宏，获取类型占用的空间长度，最小占用长度为int的整数倍：
        #define　　_INTSIZEOF(n)　　((sizeof(n)+sizeof(int)-1)&~(sizeof(int)-1))
        |------------------------------------------------|  高地址					（堆栈实现可变参数）
        |-------------函数返回地址-----------------------|
        
        |------------.........................------------------|
        |------------------------------------------------|<--va_arg后ap指向(地址自动增加指向下一个元素起始地址)
        
        |            第n个参数（最后一个可变参数）            |
        |------------------------------------------------|
        					.
            				.
            				.
        |            第2个参数（第一个可变参数）             |
        |------------------------------------------------|<--va_start后ap指向
        |           第1个参数（第一个固定参数）       |
        |------------------------------------------------|<--&v 　　低地址
        
        c)  VA_START宏，获取可变参数列表的第一个参数的地址（ap是类型为va_list的指针，v是可变参数最左边的参数，亦即第一个固定参数）:
        #define　　va_start(ap,v)　　(ap=(va_list)&v+_INTSIZEOF(v))
        
        d)  VA_ARG宏，获取可变参数的当前参数，返回指定类型并将指针指向下一参数（t参数描述了当前参数的类型）：
        #define　　va_arg(ap,t)　　(*(t*)((ap+=_INTSIZEOF(t))-_INTSIZEOF(t)))
        
        e) VA_END宏，清空va_list可变参数列表：
        #define　　va_end(ap)　　(ap=(va_list)0)
        ```

        

     4. vprintfmt（）函數解析

  ```c
  void vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)函数：
  第一部分：找到格式符%，并分析输出格式; 
  第二部分，根据格式符分析结果进行输出。
  ```

A *format specifier* follows this prototype:

```c
%[flags][width][.precision][length]specifier
```

可参考[printf](http://www.cplusplus.com/reference/cstdio/printf/)

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\spe.PNG" style="zoom:67%;" />

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\flag.PNG" style="zoom:67%;" />

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\pre.PNG" style="zoom:67%;" />

- Explain the following from `console.c`:

  ```c
  // What is the purpose of this?
  	if (crt_pos >= CRT_SIZE) { // 显示字符数超过CRT一屏可显示的字符数
  		int i;
  		//屏幕有1~n行
  		//清除buf中"第一行"的字符，将2~n行数据移到1~n-1行（上移一行）
  		memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
  			crt_buf[i] = 0x0700 | ' '; //CRT显示器需要对其用空格擦写才能去掉本来显示了的字符（将第n行数据清除）。
  		crt_pos -= CRT_COLS; //显示起点退回到最后一行起始
  	}
  ```

- 在kern/init.c的`i386_init()`下加入代码，加Lab1_exercise8_3标号的目的是为了在obj/kern/kernel.asm反汇编代码中容易找到添加的代码的位置，地址为0xf01000f9

  ```c
   	cprintf("Lab1_Exercise_8:\n");
      int x = 1, y = 3, z = 4;
      // 
      Lab1_exercise8_3:
      cprintf("x %d, y %x, z %d\n", x, y, z);
  
      unsigned int i = 0x00646c72;
      cprintf("H%x Wo%s", 57616, &i);
  ```

  

  ![](C:\Users\Administrator.MICRO-901192021\Pictures\e8.PNG)

  设置断点进行检查

  ![](C:\Users\Administrator.MICRO-901192021\Pictures\fs.PNG)

  ![](C:\Users\Administrator.MICRO-901192021\Pictures\fs1.PNG)

j经过va_start之后fmt指向格式化字符串，ap指向可变参数的首地址，参数入栈从右向左，0xf010ffc0保存着入栈的参数值情况

从这个练习可以看出来，正是因为C函数调用实参的入栈顺序是从右到左的，才使得调用参数个数可变的函数成为可能(且不用显式地指出参数的个数)。但是必须有一个方式来告诉实际调用时传入的参数到底是几个，这个是在**格式化字符串**中指出的。如果这个格式化字符串指出的参数个数和实际传入的个数不一致，比如说传入的参数比格式化字符串指出的要少，就可能会使用到栈上错误的内存作为传入的参数，编译器必须检查出这样的错误。

- ```
   unsigned int i = 0x00646c72;
      cprintf("H%x Wo%s", 57616, &i);
  ```

  What is the output? Explain how this output is arrived at in the step-by-step manner of the previous exercise. [Here's an ASCII table](http://web.cs.mun.ca/~michael/c/ascii-table.html) that maps bytes to characters.----> HE110 World

  57616的十六进制形式为`E110`, 因为是小端机，i的在内存中为0x72,0x6c,0x64,0x00. 对应ASCII为`rld\0`

### 1.2.4 The Stack

#### 1.2.4.1 **Exercise 9**

```c
确定内核在什么时候初始化了堆栈，以及堆栈所在内存的确切位置。 内核如何为其堆栈保留空间？堆栈指针初始化为指向该保留区域的哪个“端点”？
    //entry.S 77行初始化栈											 栈的位置是0xf0108000-0xf0110000
    # Set the stack pointer
	movl	$(bootstacktop),%esp						//设置栈的方法是在kernel的数据段预留32KB空间(entry.S 94行)
    																		.space		KSTKSIZE
  // 栈顶的初始化位置是0xf0110000									KSTKSIZE = 8 * PGSIZE = 8 * 4096 = 32KB
```

ebp（基指针）寄存器主要通过软件约定与堆栈相关联。进入C函数时，函数的序言代码通常通过将上一个函数的基指针推到堆栈上来保存它，然后在函数运行期间将当前esp值复制到ebp中。通过跟踪保存的ebp指针链并准确通过是什么嵌套的函数调用序列到达程序中的这个特定执行点，可以追溯到堆栈。

#### 1.2.4.2 **Exercise 10**

mon_backtrace : 0xf01008f0

test_backtrace: 0xf0l00040

![](C:\Users\Administrator.MICRO-901192021\Pictures\ba.PNG)

调用test_backtrace的代码在0xf010012f地址处，在该处断点调试：

```c
b *0xf010012f //查看进入test_backtrace(5)之前的栈顶信息 esp = 0xf010ffd0
b *0xf01000a0 //查看进入mon_backtrace(0, 0, 0)之前的栈顶信息（此时x = 0,递归调用开始返回） esp = 0xf010ff10
x/49w 0xf010ff10 //获取test_backtrace(0~5)的调用栈信息 6*8（每个调用栈装入32字节数据）+ 1（初始传入的5值）
    
	0xf010ffb0:  0x00000004      0x00000005      0xf010fff8      0xf010004e --->test_backtrace(5)
	0xf010ffc0:  0xf0111308      0x00010094      0xf010fff8      0xf010013b 
			 +--------------------------------------------------------------+
             |    next x    |   curent x     |  don't know   |  don't know  |
             +--------------+----------------+---------------+--------------+
             |  last ebx    |    last esi    |  last ebp     | return addr  |
             +------ -------------------------------------------------------+
```

![](C:\Users\Administrator.MICRO-901192021\Pictures\test_back.PNG)

一个栈帧(stack frame)的大小计算如下：

1. 在执行call test_backtrace时压入当前指令下一条指令的地址（**返回地址**），压入0xf010013b

2. push **%ebp** 将上一个栈帧的基址压入

3.  mov    %esp,%ebp记录当前栈帧的基址

4. push **%esi**

5. push **%ebx**

6. sub $0x8, %esp //调整空间用

7. push %esi //提供给cprintf的参数2

8. push %eax//提供给cprintf的参数1

   4+4+4+4+8+4+4 = 32字节

#### 1.2.4.3 **Exercise 11**

 **ebp** ：该函数使用的堆栈的基指针

  **eip**`：该函数的返回地址

**args** :  调用该函数时压入的5个值

打印的第一行反映当前正在执行的函数，即mon_backtrace本身，第二行反映调用mon_backtrace的函数，依此类推

在指针和整数之间转换，操作系统经常这样做。

```c
//kern/monitor.c
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, *p;
	ebp = read_ebp();
	while (ebp != 0) {//entry.S中确定
		p = (uint32_t *)ebp;
		cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, p[1], p[2], p[3], p[4], p[5], p[6]);
		ebp = p[0];
	}
	return 0;
}
 			 +--------------+----------------+---------------+--------------+
             |  last ebx    |    last esi    |  last ebp     | return addr  |
             +------ -------------------------------------------------------+
                                             |
                                             |
                                             ebp
```

#### 1.2.4.4 **Exercise 12**

```c
//kern/kdebug.c
先获得type范围，再获得文件名范围，再获得函数名范围，最后获得指令在函数中的偏移
// Hint:
	//	There's a particular stabs type used for line numbers.    N_SLINE
	//	Look at the STABS documentation and <inc/stab.h> to find
	//	which one.
	// Your code here.
	stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);//搜寻ip指令地址所在行
	if (lline <= rline) {
		info->eip_line = stabs[lline].n_desc;//获取EIP在源代码行号
	} else {
		cprintf("line not find\n");
		return -1;
	}

//kern/monitor.c
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, eip, *p;
	ebp = read_ebp();
	while (ebp != 0) {
		p = (uint32_t *)ebp;
		eip = p[1];
		cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, eip, p[2], p[3], p[4], p[5], p[6]);
		if (debuginfo_eip(eip, &info) == 0) {
			int offset = eip - info.eip_fn_addr;
			//kern/monitor.c:143: monitor+106 文件名:行号: 函数名+ip在函数中的偏移
cprintf("%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, offset);
		}
		ebp = p[0];
	}
	return 0;
}
//commands中加入
{ "backtrace", "Display backtrace info", mon_backtrace },//添加命令
git commit -am "lab1 finished"
make handin
```

## 2. Lab 2: Memory Management

apikey: 070WYOLJW9V18YCMO6GT11GO9QA1I4IU

```
git pull
git checkout -b lab2 origin/lab2
```

### 2.1 两级页表机制

- xv6 uses page tables primarily to multiplex address spaces and to protect memory。（复用地址空间同时保护内存）

- instructions (both user and kernel) manipulate virtual addresses.（指令操作的都是虚拟地址）


- 分页硬件通过使用虚拟地址的**前20位**索引到页表中以查找PTE，并用PTE中的PPN替换地址的前20位来转换虚拟地址。

  分页硬件将**低位12位不变**地从虚拟地址复制到转换后的物理地址。因此，页表使操作系统能够以4096（2^12）字节的对齐块粒度控制虚拟到物理地址的转换。

  <img src="C:\Users\Administrator.MICRO-901192021\Pictures\addr.PNG" style="zoom:67%;" />

​                                                                                   Figure 2-1. x86 page table hardware.

页表作为两级树存储在物理内存中。树的根目录是一个4KB的页目录，其中包含1024个类似于PTE的页目录项（存储着二级页表）。每个二级页表的页表项是1024个32位PTE的数组。分页硬件使用虚拟地址的**前10位**来选择页目录项。如果存在页目录项，分页硬件将使用虚拟地址的**下10位**从页目录项所引用的页表项中选择PTE。如果页目录项或PTE不存在，则分页硬件会引发故障。这种两级结构允许页表在大范围虚拟地址没有映射的常见情况下忽略整个页表页。

#### 2.1.1 内存管理的必要性

1. 隐藏与保护：因为加入了虚拟内存这一中间层，真实的物理地址对用户级程序是不

可见的，它只能访问操作系统允许其访问的内存区域。

2. 为程序分配连续的内存空间：利用虚拟内存，操作系统可以从物理分散的内存页

中，构建连续的程序空间，这使得我们拥有更高的内存利用率。

3. 扩展地址空间范围：如前文所述，通过虚拟内存，MIPS32 位机拥有了 4GB 的寻

址能力，这真的很 cool。:)

4. 使内存映射适合你的程序：在大型操作系统中，可能存在相同程序的多个副本同时

运行，这时候通过内存翻译这一中间层，你能使他们都使用相同的程序地址，这让

很多工作都简单了很多。

5. 重定位：程序入口地址和预先声明的数据在程序编译的过程中就确定了。但通过

MMU 的内存翻译，我们能够让程序运行在内存中的任何位置。

#### 2.1.2 二级页表机制

我们的操作系统内核采取二级页表结构。如下图所示

第一级表称为**页目录pgdir** (page directory)，一共 1024 个页目录项，每个页目录项 32

位 (4 Byte)，页目录项存储的值为其对应的二级页表入口的物理地址。整个页目录存放

在一个页面 (4KB) 中。

第二级表称为**页表pgtab** (page table)，每一张页表有 1024 个页表项（PTE），每个页表项 32 位

(4 Byte)，页表项存储的是对应页面的页框号 (20 位) 以及标志位 (12 位)。每张页表占

用一个页面大小 (4KB) 的内存空间。

对于一个 32 位的虚存地址，其 31-22 位表示的是页目录项的索引，21-12 位表示的

是页表项的索引，11-0 位表示的是该地址在该页面内的偏移。

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\page.PNG" style="zoom:60%;" />

​                                                                                           Figure 2-1: 二级页表结构示意图

```c
//xv6/mmu.h
0773 // A virtual address ’la’ has a three−part structure as follows:
0774 //
0775 // +−−−−−−−−10−−−−−−+−−−−−−−10−−−−−−−+−−−−−−−−−12−−−−−−−−−−+
0776 // | Page Directory | Page Table     | Offset within Page |
0777 // | Index          | Index          |                    |
0778 // +−−−−−−−−−−−−−−−−+−−−−−−−−−−−−−−−−+−−−−−−−−−−−−−−−−−−−−−+
0779 // \−−− PDX(va) −−/ \−−− PTX(va) −−/
    
#define PTXSHIFT 12 // offset of PTX in a linear address
#define PDXSHIFT 22 // offset of PDX in a linear address
0781 // page directory index
0782 #define PDX(va) (((uint)(va) >> PDXSHIFT) & 0x3FF)
0783 
0784 // page table index
0785 #define PTX(va) (((uint)(va) >> PTXSHIFT) & 0x3FF)
0786 
0787 // construct virtual address from indexes and offset
0788 #define PGADDR(d, t, o) ((uint)((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))
    
0806 // Address in page table or page directory entry
0807 #define PTE_ADDR(pte) ((uint)(pte) & ~0xFFF)
0808 #define PTE_FLAGS(pte) ((uint)(pte) & 0xFFF)
```

#### 2.1.3 Process address space

```c
 //xv6/memlayout.h
0200 // Memory layout
0201 
0202 #define EXTMEM 0x100000 // Start of extended memory
0203 #define PHYSTOP 0xE000000 // Top physical memory
0204 #define DEVSPACE 0xFE000000 // Other devices are at high addresses
0205 
0206 // Key addresses for address space layout (see kmap in vm.c for layout)
0207 #define KERNBASE 0x80000000 // First kernel virtual address
0208 #define KERNLINK (KERNBASE+EXTMEM) // Address where kernel is linked
0209 
0210 #define V2P(a) (((uint) (a)) − KERNBASE)
0211 #define P2V(a) ((void *)(((char *) (a)) + KERNBASE))
0212 
0213 #define V2P_WO(x) ((x) − KERNBASE) // same as V2P, but without casts
0214 #define P2V_WO(x) ((x) + KERNBASE) // same as P2V, but without casts
```

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\vir.PNG" style="zoom: 67%;" />

​                                                                         Figure 2-2： 虚拟地址和物理地址的内存映射布局

#### 2.1.4 Code: creating an address space

main调用kvmalloc（1840）创建并切换到一个页面表，其中包含内核运行所需的KERNBASE以上的映射。

```c
//xv6/vm.c

1816 // Set up kernel part of a page table.
1817 pde_t*
1818 setupkvm(void)
1819 {
1820 pde_t *pgdir;
1821 struct kmap *k;
1822 
1823 if((pgdir = (pde_t*)kalloc()) == 0)//分配一页物理内存来保存页目录表
1824 return 0;
1825 memset(pgdir, 0, PGSIZE);
1826 if (P2V(PHYSTOP) > (void*)DEVSPACE)
1827 panic("PHYSTOP too high");
1828 for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
1829 if(mappages(pgdir, k−>virt, k−>phys_end − k−>phys_start,//调用mappages来安装内核需要的地址翻译。建立PTE
1830 (uint)k−>phys_start, k−>perm) < 0) {//mappages（1760）将映射安装到一个页面表中，将虚拟地址范围映射到相应的物理地址	           mappages它以页面间隔为范围内的每个虚拟地址分别执行此操作		    范围,见Figure 2-2
1831 freevm(pgdir);
1832 return 0;
1833 }
1834 return pgdir;
1835 }
1836
1837 // Allocate one page table for the machine for the kernel address
1838 // space for scheduler processes.
1839 void
1840 kvmalloc(void) //分配页表
1841 {
1842 kpgdir = setupkvm();
1843 switchkvm();
1844 }
		//walkpgdir 查找虚拟地址在二级页表中的的PTE
1802 // This table defines the kernel’s mappings, which are present in
1803 // every process’s page table.
1804 static struct kmap {
1805 void *virt;
1806 uint phys_start;
1807 uint phys_end;
1808 int perm;
1809 } kmap[] = {
1810 { (void*)KERNBASE, 0, EXTMEM, PTE_W}, // I/O space
1811 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kern text+rodata
1812 { (void*)data, V2P(data), PHYSTOP, PTE_W}, // kern data+memory
1813 { (void*)DEVSPACE, DEVSPACE, 0, PTE_W}, // more devices
1814 };
```

内核必须在运行时为页表、进程用户内存、内核堆栈和管道缓冲区分配和释放物理内存。

#### 2.1.5 Physical memory allocator

```c
//xv6/kalloc.c
3115 struct run {
3116 struct run *next;
3117 };
3118 
3119 struct {
3120 struct spinlock lock;
3121 int use_lock;
3122 struct run *freelist;
3123 } kmem;

```

<img src="C:\Users\Administrator.MICRO-901192021\Pictures\user.PNG" style="zoom: 67%;" />

### 2.2 Lab 2: Memory Management 

#### 2.2.1 Part 1: Physical Page Management

memlayout.h : 描述虚拟地址空间的布局

`PageInfo` structure : 用来跟踪哪些物理内存页是空闲的

kclock.c` and `kclock.h: 操作电脑的电池时钟和CMOS RAM硬件，其中BIOS记录电脑包含的物理内存量，以及其他内容

三个主要结构文件：pmap.h、memlayout.h、inc/mmu.h

##### 2.2.1.1 **Exercise 1**

```c
编写物理页面分配器（physical page allocator）。它通过struct PageInfo对象的链接列表跟踪哪些页面是空闲的（与xv6不同，这些对象不嵌入空闲页面本身），每个对象对应于一个物理页面。在编写虚拟内存实现的其余部分之前，需要先编写物理页分配器，因为页表管理代码将需要分配物理内存来存储页表。
In the file kern/pmap.c, you must implement code for the following functions
boot_alloc()	获取物理空间放置页目录表和页表
mem_init() (only up to the call to check_page_free_list(1)) 内存初始化
page_init() 参考 Figure 2-2进行PageInfo结构初始化
page_alloc() 分配一个物理页面
page_free()  释放一个物理页面
```

1. static void * boot_alloc(uint32_t n)用于在没有内存分配函数之间的内存分配，只在page_alloc不可用之前使用。作用是从可用物理内存上返回足够n字节的空间，返回该空间的起始地址(虚拟地址)。并保证每次分配的内存都对齐到PGSIZE页面边界。
2. void mem_init(void)用于进行整个内存初始化。
3. void page_init(void)用于PageInfo数组的初始化。每个物理页面对应一个PageInfo数据结构，用于指出该物理页面被多少个虚拟页面照射。如果没有映射到虚拟页面，则表示物理页面空闲。所有的空间物理页面的数据结构构成一个链表，如果是空闲物理页面，它的PageInfo数据结构还要负责标示出下一个空闲物理页面数据结构的位置。
4. struct PageInfo page_alloc(int alloc_flags)用于分配一个物理页面，返回分配的物理页面的PageInfo数据结构。
5. void page_free(struct PageInfo *pp)负责释放一个物理页面，将该页面放回到空闲链表中。

```c
static inline physaddr_t page2pa(struct PageInfo *pp) // 将pages数组中的相应entry转化为对应的物理地址
#define KADDR(pa) _kaddr(__FILE__, __LINE__, pa)      // 将物理地址转化为内核虚拟地址
static inline void*page2kva(struct PageInfo *pp)	  //  将pages数组中的相应entry转化为内核虚拟地址
#define PADDR(kva) _paddr(__FILE__, __LINE__, kva)    //将内核虚拟地址转化为物理地址
static inline struct PageInfo* pa2page(physaddr_t pa) // // 将物理地址转化为 PageInfo*
```

#### 2.2.2 Part 2: Virtual Memory

##### 2.2.2.1 **Exercise 2**

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\fig5-8.gif)

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\fig5-9.gif)

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\fig5-10.gif)

在x86术语中，**虚拟地址由段选择器和段内偏移**组成。 **线性地址**是段转换之后但在页转换之前获得的。 **物理地址**是在段和页面转换之后得到的，以及最终经过总线到达RAM的内容。

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\20190710150543234.png)

C指针是逻辑地址的“**OFFSET**”组件。

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\fig5-12.gif)

##### 2.2.2.2 **Exercise 3**

```c
映射从虚拟地址0xf0000000开始的前256MB物理内存，并映射虚拟地址空间的许多其他区域。
xp/Nx paddr qemu中使用
    显示从物理地址paddr开始的N个字的十六进制转储。如果省略N，则默认为1。这是GDB的x命令的物理内存模拟。
使用QEMU监视器中的xp命令和GDB中的x命令检查相应物理地址和虚拟地址处的内存，并确保看到相同的数据。QEMU提供了一个info pg命令，该命令显示了当前页表的紧凑但详细的表示，包括所有映射的内存范围、权限和标志。Stock QEMU还提供一个info mem命令，该命令显示映射哪些虚拟地址范围以及具有哪些权限的概览。
```

![](C:\Users\Administrator.MICRO-901192021\Pictures\same.PNG)

- 在boot/boot.S中，我们安装了一个全局描述符表（GDT），通过将所有段基址设置为0并将限制设置为0xFFFFFF，有效地禁用了段转换。因此，“SELECTOR”无效，线性地址始终等于逻辑地址的偏移量。

  ```c
  //boot/boot.S
  # Set up the important data segment registers (DS, ES, SS).
    xorw    %ax,%ax             # Segment number zero
    movw    %ax,%ds             # -> Data Segment
    movw    %ax,%es             # -> Extra Segment
    movw    %ax,%ss             # -> Stack Segmen
  ```

- 从CPU上执行的代码来看，一旦进入保护模式（我们在boot/boot.S中首先进入保护模式），就无法直接使用线性或物理地址。所有内存引用都被解释为逻辑地址，并由MMU进行转换，这意味着C中的所有指针都是**逻辑地址**。JOS内核通常需要将地址转换为不透明值或整数，而不需要对它们解引用，例如在物理内存分配器中。 有时这些是逻辑地址，有时它们是物理地址。 为了帮助规范代码，JOS源代码区分了两种情况：类型`uintptr_t`表示不透明的逻辑地址，`physaddr_t`表示物理地址。 这两种类型实际上只是32位整数（`uint32_t`）的同义词，因此编译器不会阻止您将一种类型分配给另一种类型！ 由于它们是整数类型（不是指针），如果您尝试对它们解引用，编译器就会给出警告或错误。

- JOS内核可以通过首先将uintptr**强制转换为指针类型**来解引用。相反，内核不能合理地解除对物理地址的引用，因为MMU转换所有内存引用。如果将physaddr强制转换为指针并取消引用，则可以加载并存储到结果地址（硬件将其解释为逻辑地址），但可能无法获得预期的内存位置。

  | C type       | Address type |
  | ------------ | ------------ |
  | `T*`         | Virtual      |
  | `uintptr_t`  | Virtual      |
  | `physaddr_t` | Physical     |

- JOS内核有时需要读取或修改它只知道物理地址处的内存。例如，向页表添加映射可能需要分配物理内存来存储页目录，然后初始化该内存。但是，内核不能绕过虚拟地址转换，因此不能直接加载和存储到物理地址。JOS在虚拟地址0xf0000000处重新映射从物理地址0开始的所有物理内存的一个原因是帮助内核读写它**只知道物理地址**的内存。为了将物理地址转换为内核可以实际读写的虚拟地址，内核必须将0xf0000000映射到物理地址中，以在重新映射的区域中找到其相应的虚拟地址。您应该使用KADDR（pa）来进行添加。物理地址--->虚拟地址

  ```c
  struct PageInfo *
  page_alloc(int alloc_flags)
  {
  	// Fill this function in
  	struct PageInfo * page;
  	if (!page_free_list) return NULL;
  	page = page_free_list;//获得一个物理页
  	page_free_list = page->pp_link;
  	page->pp_link = NULL;
  	if (alloc_flags & ALLOC_ZERO) {
  		void *va = page2kva(page);//pages表项对应的物理页的物理地址转化为内核虚拟地址
  		memset(va, '\0', PGSIZE);
  	}
  	return page;
  }
  ```

  

- JOS内核有时还需要能够通过一个**给定的虚拟地址**（存储内核数据结构的内存）找到一个物理地址，。boot_alloc（）分配的内核全局变量和内存位于内核加载的区域，从KERNBASE(0xf0000000)开始，就是我们要映射到所有物理内存的区域。因此，要将该区域中的虚拟地址转换为物理地址，内核只需减去0xf0000000即可。你应该用PADDR（va）来做减法。虚拟地址--->物理地址

  ```c
  //kern/pmap.c 中建立物理页页目录项时
  //  4) Then extended memory [EXTPHYSMEM, ...).[0x10000,...)弄清楚哪些是内核用来存储内核文件和页目录表、页表的区域,哪些是空闲区域
  	//	还得注意此时要把内核虚拟地址转化为物理地址 boot_alloc(0)来获取当前空闲区的地址
  	physaddr_t first_free_addr = PADDR(boot_alloc(0));
  	size_t first_free_page = first_free_addr/PGSIZE;
  ```

- 通常会同时在多个虚拟地址（或在多个环境的地址空间）映射相同的物理页.在对应于物理页面的struct PageInfo的pp_ref字段中记录对每个物理页面的引用数。当物理页的此计数变为零时，该页可以被释放，因为它不再被使用。通常，此计数应等于物理页面在所有页面表中显示在**UTOP**下面的次数（UTOP上面的映射大部分是在引导时由内核设置的，永远不会被释放，因此不需要引用计数）。我们还将使用它来跟踪指向页面目录页面的指针数量，进而跟踪页面目录指向页面表页面的引用数量。使用**page_alloc**时要小心。 它返回的页面的引用计数始终为0，因此只要您对返回的页面执行某些操作（例如将其插入页面表），pp_ref就应该递增。 有时这是由其他函数处理的（例如，page_insert），有时调用page_alloc的函数必须直接执行。

##### 2.2.2.3 **Exercise 4**

```c
Page Table Management: 插入和删除linear-to-physical的映射，并在需要时创建页表页。
In the file kern/pmap.c, you must implement code for the following functions.
     	pgdir_walk()
        boot_map_region()
        page_lookup()
        page_remove()
        page_insert()
```

- pgdir_walk用于查找一个虚拟内存对应的页表项，如果页表不存在，则根据需要可以创建一个
- boot_map_region映射**一片**指定的虚拟页到指定的物理页，填写页表
- page_lookup用于查找一个虚拟内存映射的物理页的数据结构
- page_remove用于移除一个虚拟页到物理页的照射
-  page_insert用于增加**一个**虚拟页到物理页的映射到页表

## 2.3 Part 3: Kernel Address Space

#### 2.3.1 **Exercise 5.**

- JOS将处理器的32位线性地址空间分为**两部分**。我们将在实验室3中开始加载和运行的用户环境（进程）将控制下半部分的布局和内容，而内核始终保持对上半部分的完全控制。分界线是由inc/memlayout.h中的符号ULIM任意定义的，为内核保留了大约256MB的虚拟地址空间。这就解释了为什么我们需要在实验室1中为内核提供如此高的链接地址：否则内核的虚拟地址空间将没有足够的空间同时映射到它下面的用户环境中。

- 用户环境无权访问ULIM之上的任何内存，而内核将能够读取和写入该内存。对于地址范围[UTOP，ULIM]，内核和用户环境都具有相同的权限：它们可以读取但不能写入此地址范围。此地址范围用于以**只读方式向用户环境公开某些内核数据结构**。最后，UTOP下面的地址空间供用户环境使用；用户环境将设置访问此内存的权限。请注意，可写权限位（PTE_W）同时影响用户和内核代码！

```c
现在，您将在UTOP上面设置地址空间：地址空间的核心部分。inc/memlayout.h显示您应该使用的布局。您将使用刚才编写的函数设置适当的线性到物理映射。分别映射UPAGES、KSTACKTOP、KERNBASE三个区域。
    Fill in the missing code in mem_init() after the call to check_page().
    1. 建立UPAGES虚拟地址到物理地址（pages数组所在处）的映射
    2. 建立KERNSTACK的结构映射（32K）
    3. 建立整个KERNBASE的映射
    cr2包含发生页面错误时的线性地址。
```

# 3. Lab 3: User Environments

### 3.1 Part A: User Environments and Exception Handling



- 与Unix进程一样，JOS环境将“线程”和“地址空间”的概念结合起来。线程主要由保存的寄存器（env_tf字段）定义，地址空间由env_pgdir指向的页面目录和页面表定义。要运行环境，内核必须使用保存的寄存器和适当的地址空间设置CPU。


- bug记录

  在开始阶段我把代码 merge 到 lab3 分支中，开始运行的时候，发现会出现：

  ```
  kernel panic at kern/pmap.c:154: PADDR called with invalid kva 00000000
  ```

经过打 log，发现在 memset 之后，会把 kern_pgdir 的值覆盖掉；

kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
memset(kern_pgdir, 0, PGSIZE);

```c
kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
memset(kern_pgdir, 0, PGSIZE);
```

继续打log：cprintf("%x\n",&kern_pgdir); 发现 kern_pgdir 这个变量的地址是在 f018f00c…

然而 extern char end[]; 的值是 f018f000…所以就会出现类似的问题，就是 memset 把 kern_pgdir 全局变量的值覆盖掉了；继续深入探讨，发现在 pmap.c 中如果是有 static 修饰的变量值是低于 end 的，如果没有修饰的话值是高于 end 的…在其他文件里面测试类似的变量也能得出类似的结果

#### 3.1.1 **Exercise 2**

```c
In the file env.c, finish coding the following functions:

env_init() : 初始化环境数组中的所有环境结构，并将它们添加到环境空闲列表中。还调用env_init_percpu，这将使用特权级别0（内核）			和特权级别3（用户）的单独段配置分段硬件。

env_setup_vm() : 为新环境分配页面目录，并初始化新环境地址空间的内核部分

region_alloc() : 为用户环境分配和映射物理内存空间

load_icode() : 您将需要解析ELF二进制映像，就像引导加载程序已经做的那样，并将其内容加载到新环境的用户地址空间中。

env_create() : 使用env_alloc分配环境并调用load_icode将ELF二进制文件加载到其中。

env_run() : 启动以用户模式运行的给定环境。
```

- 下面是调用用户代码之前的代码调用图:

  `start` (`kern/entry.S`)

  i386_init（kern/init.c）

  - `cons_init`

  - `mem_init`

  - `env_init`

  - `trap_init` (still incomplete at this point)

  - `env_create`

  - ```
    env_run
    ```

    - `env_pop_tf`

- 中断和异常是一种特殊的控制转移；它们的工作方式有点像未编程的呼叫。它们改变正常的程序流以处理外部事件或报告错误或异常情况。中断和异常之间的区别在于，中断用于处理**处理器外部**的异步事件**，而异常用于处理处理器在**执行指令过程中**检测到的条件。

- 异常和中断都是“受保护的控制传输”，这会导致处理器从用户模式切换到内核模式（CPL=0），而不会给用户模式代码任何干扰内核或其他环境功能的机会。中断是一种受保护的控制传输，由处理器外部的**异步事件**（如外部设备I/O活动通知）引起。相反，异常是由当前运行的**代码同步**引起的受保护的控制传输，例如，由于被零除或无效的内存访问。

1. Interrupts
   - 可屏蔽中断，通过INTR引脚发出信号.
   - 非屏蔽中断，通过NMI（非屏蔽中断）引脚发出信号.
   
2. Exceptions
   - 检测到处理器。这些进一步分类为故障、陷阱和中止.
   - 编程的。INTO、int 3、int n和BOUND中的指令可以触发异常。这些指令通常被称为“软件中断”，但处理器将其作为异常处理。

3. 处理器的中断/异常机制设计为，当中断或异常发生时，当前运行的代码不会随意选择内核进入的位置或方式。相反，处理器确保只能在仔细控制的条件下进入内核。在x86上，两种机制共同提供此保护：

   - **The Interrupt Descriptor Table（IDT：中断描述符表）.** 处理器确保中断和异常只能导致内核在几个特定的、定义良好的入口点输入，这些入口点由**内核本身**决定，而不是由执行中断或异常时运行的代码决定。x86允许多达**256**个不同的中断或异常入口点进入内核，每个入口点具有不同的中断向量。向量是介于0和255之间的数字。**中断的向量由中断源决定**：不同的**设备**、**错误条件**和**对内核的应用程序请求**生成具有不同向量的中断。CPU使用向量作为处理器中断描述符表（IDT）的索引，内核将其设置在内核专用内存中，与GDT非常相似。处理器从该表中的相应条目加载：

     - 要加载到指令指针（EIP）寄存器的值，指向指定用于处理该类型异常的内核代码。
     - 要加载到代码段（CS）寄存器中的值，它在位0-1中包含异常处理程序运行时的特权级别(在JOS中，所有异常都在内核模式下处理，特权级别为0。）

   - **The Task State Segment(TSS: 任务状态描述符).** 处理器需要一个位置来保存中断或异常发生之前的旧处理器状态，例如处理器调用异常处理程序之前的EIP和CS的原始值，以便异常处理程序可以稍后恢复该旧状态并从中断处恢复中断的代码。但是，旧处理器状态的这个保存区域反过来必须受到保护，不受非特权用户模式代码的影响；否则，错误或恶意用户代码可能会危害内核。

     因此，当x86处理器发生中断或陷阱，导致特权级别从用户模式更改为内核模式时，它也会**切换到内核内存中的堆栈**。称为任务状态段（TSS）的结构指定该堆栈所在的段选择器和地址。处理器（在此新堆栈上）push SS、ESP、EFLAGS、CS、EIP和可选error code。然后从中断描述符加载CS和EIP，并将ESP和SS设置为引用新堆栈。

     尽管TSS很大，并且可能服务于多种用途，但JOS仅使用它来定义处理器在从用户模式转换到内核模式时应切换到的内核堆栈。由于JOS中的“内核模式”在x86上是特权级别0，因此处理器在进入内核模式时使用TSS的ESP0和SS0字段来定义内核堆栈。JOS不使用任何其他TSS字段。

4. 中断和异常的类型

   x86处理器可以在内部生成的所有**同步异常**都使用介于0和31之间的中断向量，因此映射到IDT条目0-31。例如，页面错误总是通过向量14导致异常。大于31的中断向量仅由软件中断使用，可由int指令生成，或者需要注意的外部设备引起的异步硬件中断。

   在本节中，我们将扩展JOS以处理向量0-31中内部生成的x86异常。在下一节中，我们将使JOS处理软件中断向量48（0x30），JOS（相当任意）将其用作其系统调用中断向量。在实验室4中，我们将扩展JOS以处理外部生成的硬件中断，如时钟中断。

5.  ```c
     					 +--------------------+ KSTACKTOP             
                         | 0x00000 | old SS   |     " - 4
                         |      old ESP       |     " - 8
                         |     old EFLAGS     |     " - 12
                         | 0x00000 | old CS   |     " - 16
                         |      old EIP       |     " - 20
                         |     error code     |     " - 24 <---- ESP
                         +--------------------+ 
    ```

6. 处理器可以从内核模式和**用户模式（特权改变时多了ss、esp）**接受异常和中断。然而，只有在**从用户模式进入内核**时，x86处理器才会在将其旧寄存器状态推送到堆栈上并通过IDT调用适当的异常处理程序之前**自动切换内核堆栈**，当中断或异常发生时，如果处理器已经处于内核模式（CS寄存器的低位2位已经为零），那么CPU只是在同一内核堆栈上推送更多值。通过这种方式，内核可以优雅地处理由内核本身中的代码引起的嵌套异常。此功能是实现保护的一个重要工具，我们将在后面的系统调用部分中看到。

7. 如果处理器已经**处于内核模式**，并且发生嵌套异常，因为它不需要切换堆栈，它不会保存旧的SS或ESP寄存器。因此，对于不推送错误代码的异常类型，内核堆栈在异常处理程序的条目中看起来如下所示：

   ```c
    					 +--------------------+ <---- old ESP
                        |     old EFLAGS     |     " - 4
                        | 0x00000 | old CS   |     " - 8
                        |      old EIP       |     " - 12
                        +--------------------+             
   ```

   处理器的嵌套异常功能有一个重要的**警告**。如果处理器在已经处于内核模式时发生异常，并且由于任何原因（例如堆栈空间不足）无法将其旧状态推送到内核堆栈上，那么处理器无法进行恢复，因此它只需重置自身。不用说，内核的设计应该确保不会发生这种情况。

8. 应实现的总体控制流程如下所示：

   ```c
      IDT                   trapentry.S         trap.c
      
   +----------------+                        
   |   &handler1    |---------> handler1:          trap (struct Trapframe *tf)
   |                |             // do stuff      {
   |                |             call trap          // handle the exception/interrupt
   |                |             // ...           }
   +----------------+
   |   &handler2    |--------> handler2:
   |                |            // do stuff
   |                |            call trap
   |                |            // ...
   +----------------+
          .
          .
          .
   +----------------+
   |   &handlerX    |--------> handlerX:
   |                |             // do stuff
   |                |             call trap
   |                |             // ...
   ```

   ![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\201906171933232.png)

   #### 3.1.2 **Exercise 4**

异常是**CPU**在发生“错误”时生成的一种中断( [interrupt](https://wiki.osdev.org/Interrupt))。在大多数情况下，某些异常并不是真正的错误，例如页面错误。

- 异常(EXCEPTION)情况分类如下：

  故障（fault）：报告故障时保存的CS和EIP值指向导致故障的指令。这些故障可以纠正，程序可以继续运行，就好像什么也没有发生一样。

  - #### Divide-by-zero Error

  - #### Bound Range Exceeded(绑定指令) ：This exception can occur when the BOUND instruction is executed。

  - #### Invalid Opcode：当处理器试图执行无效或未定义的操作码或前缀无效的指令时，会发生无效操作码异常。在其他情况下也会发生这种情况，例如：

    - 指令长度超过15个字节，但这仅在使用冗余前缀时发生。


    - 该指令尝试访问不存在的控制寄存器（例如，mov cr6、eax）。


    - 执行UD指令。

  陷阱(trap)：报告陷阱时存储的CS和EIP值动态指向导致陷阱的指令**之后**的指令。如果在改变程序流的指令期间检测到陷阱，则CS和EIP的报告值反映程序流的改变。例如，如果在JMP指令中检测到陷阱，则推送到堆栈上的CS和EIP值指向JMP的目标，而不是JMP之后的指令。陷阱在执行陷阱指令后立即报告。

  中止(abort)：中止是一种异常，它既不允许引起异常的指令的精确位置，也不允许引起异常的程序重新启动。中止用于报告严重错误，如硬件错误和系统表中不一致或非法的值。一些严重的不可恢复的错误。

- 某些异常会将32位“错误代码”推送到堆栈顶部，从而提供有关错误的附加信息。在将控制权返回到当前运行的程序之前，必须从堆栈中提取此值(i.e. before calling IRET)

|                             Name                             |    Vector nr.     |    Type    | Mnemonic | Error code? |
| :----------------------------------------------------------: | :---------------: | :--------: | :------: | :---------: |
| [Divide-by-zero Error](https://wiki.osdev.org/Exceptions#Divide-by-zero_Error) |      0 (0x0)      |   Fault    |   #DE    |     No      |
|       [Debug](https://wiki.osdev.org/Exceptions#Debug)       |      1 (0x1)      | Fault/Trap |   #DB    |     No      |
| [Non-maskable Interrupt](https://wiki.osdev.org/Non_Maskable_Interrupt) |      2 (0x2)      | Interrupt  |    -     |     No      |
|  [Breakpoint](https://wiki.osdev.org/Exceptions#Breakpoint)  |      3 (0x3)      |    Trap    |   #BP    |     No      |
|    [Overflow](https://wiki.osdev.org/Exceptions#Overflow)    |      4 (0x4)      |    Trap    |   #OF    |     No      |
| [Bound Range Exceeded](https://wiki.osdev.org/Exceptions#Bound_Range_Exceeded) |      5 (0x5)      |   Fault    |   #BR    |     No      |
| [Invalid Opcode](https://wiki.osdev.org/Exceptions#Invalid_Opcode) |      6 (0x6)      |   Fault    |   #UD    |     No      |
| [Device Not Available](https://wiki.osdev.org/Exceptions#Device_Not_Available) |      7 (0x7)      |   Fault    |   #NM    |     No      |
| [Double Fault](https://wiki.osdev.org/Exceptions#Double_Fault) |      8 (0x8)      |   Abort    |   #DF    | Yes (Zero)  |
| ~~[Coprocessor Segment Overrun](https://wiki.osdev.org/Exceptions#Coprocessor_Segment_Overrun)~~ |      9 (0x9)      |   Fault    |    -     |     No      |
| [Invalid TSS](https://wiki.osdev.org/Exceptions#Invalid_TSS) |     10 (0xA)      |   Fault    |   #TS    |     Yes     |
| [Segment Not Present](https://wiki.osdev.org/Exceptions#Segment_Not_Present) |     11 (0xB)      |   Fault    |   #NP    |     Yes     |
| [Stack-Segment Fault](https://wiki.osdev.org/Exceptions#Stack-Segment_Fault) |     12 (0xC)      |   Fault    |   #SS    |     Yes     |
| [General Protection Fault](https://wiki.osdev.org/Exceptions#General_Protection_Fault) |     13 (0xD)      |   Fault    |   #GP    |     Yes     |
|  [Page Fault](https://wiki.osdev.org/Exceptions#Page_Fault)  |     14 (0xE)      |   Fault    |   #PF    |     Yes     |
|                           Reserved                           |     15 (0xF)      |     -      |    -     |     No      |
| [x87 Floating-Point Exception](https://wiki.osdev.org/Exceptions#x87_Floating-Point_Exception) |     16 (0x10)     |   Fault    |   #MF    |     No      |
| [Alignment Check](https://wiki.osdev.org/Exceptions#Alignment_Check) |     17 (0x11)     |   Fault    |   #AC    |     Yes     |
| [Machine Check](https://wiki.osdev.org/Exceptions#Machine_Check) |     18 (0x12)     |   Abort    |   #MC    |     No      |
| [SIMD Floating-Point Exception](https://wiki.osdev.org/Exceptions#SIMD_Floating-Point_Exception) |     19 (0x13)     |   Fault    | #XM/#XF  |     No      |
| [Virtualization Exception](https://wiki.osdev.org/Exceptions#Virtualization_Exception) |     20 (0x14)     |   Fault    |   #VE    |     No      |
|                           Reserved                           | 21-29 (0x15-0x1D) |     -      |    -     |     No      |
| [Security Exception](https://wiki.osdev.org/Exceptions#Security_Exception) |     30 (0x1E)     |     -      |   #SX    |     Yes     |
|                           Reserved                           |     31 (0x1F)     |     -      |    -     |     No      |
| [Triple Fault](https://wiki.osdev.org/Exceptions#Triple_Fault) |         -         |     -      |    -     |     No      |
| ~~[FPU Error Interrupt](https://wiki.osdev.org/Exceptions#FPU_Error_Interrupt)~~ |      IRQ 13       | Interrupt  |  #FERR   |     No      |

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\IDT.png)

1. 为每个异常/中断设置单独的处理函数的目的是什么？ （即，如果所有异常/中断都传递给同一个处理程序，则无法提供当前实现中存在哪些功能？）

不同的中断需要不同的中断处理程序。因为对待不同的中断需要进行不同的处理方式，有些中断比如指令错误，就需要直接中断程序的运行。 而I/O中断只需要读取数据后，程序再继续运行。

1. 需要做什么才能使`user/softint`程序正常运行？ 评分脚本期望它产生一般保护错误（trap 13），但softint的代码为`int $14`。 为什么这会产生中断向量13？ 如果内核实际上允许softint的`int $14`指令调用内核的页面错误处理程序（中断向量14）会发生什么？

因为当前系统运行在用户态下，特权级为3，而INT 指令为系统指令，特权级为0。 会引发General Protection Exception。

## 3.2 Part B: Page Faults, Breakpoints Exceptions, and System Calls

#### 3.2.1**Exercise 5**

```c
修改 trap_dispatch() 将 page fault exceptions 分配到 page_fault_handler(). 记住你可以用 make run-x or make run-x-nox是启动JOS执行特定的用户程序. For instance, make run-hello-nox runs the hello user program.
```

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\fig9-3.gif)

### Questions

1. 断点测试例子中，产生断点异常还是通用保护错误取决于我们如何初始化断点异常的IDT项。为什么？

如果设置其DPL为0，则会产生GPF，因为用户程序跳转执行内核态程序。如果我们想要当前执行的程序能够跳转到这个描述符所指向的程序哪里继续执行的话，有个要求，就是要求当前运行程序的CPL，RPL的最大值需要小于等于DPL，否则就会出现优先级低的代码试图去访问优先级高的代码的情况，就会触发general protection exception。

**中断和异常的区别：**

在TF的当前值作为EFLAGS的一部分保存在堆栈上后，通过中断门或陷阱门的向量中断导致TF（陷阱标志）复位。通过此操作，处理器可防止使用单步执行的调试活动影响中断响应。随后的IRET指令将TF恢复为堆栈上EFLAGS映像中的值。

中断门和陷阱门之间的区别在于对IF（中断启用标志）的影响。一种通过中断门进行矢量化的中断，它使IF复位，从而防止其他中断干扰当前的中断处理程序。随后的IRET指令将IF恢复为堆栈上EFLAGS映像中的值。通过陷阱门的中断不会改变IF。

控制中断过程的特权规则与过程调用的特权规则类似：CPU不允许中断将控制权转移到比当前特权级别低（数值上更高的特权级别）的程序段中。试图违反此规则将导致 **a general protection exception**。

由于中断的发生通常是不可预测的，因此此特权规则有效地限制了中断和异常处理过程可以执行的特权级别。可以使用以下任一策略来确保从不违反特权规则。

- 将处理程序放置在一个一致的段中。此策略适合处理某些异常（例如，除法错误）。这样的处理程序必须只使用堆栈中可用的数据。如果它需要数据段中的数据，则该数据段必须具有特权级别3，从而使其不受保护。


- 将处理程序放在特权级别为零的段中。

### System calls

应用程序将在寄存器中传递系统调用号和系统调用参数。这样，内核就不需要在用户环境的堆栈或指令流中四处搜索。系统调用号将位于%eax中，参数（最多五个）将分别位于%edx、%ecx、%ebx、%edi和%esi中。内核将返回值传回%eax。

​		操作系统通常依靠硬件支持来实现内存保护。操作系统会通知硬件哪些虚拟地址有效，哪些无效。当程序试图访问无效地址或没有权限访问的地址时，处理器会在导致故障的指令处停止程序，然后将有关尝试操作的信息放入内核。如果故障是可修复的，内核可以修复它并让程序继续运行。如果故障无法修复，则程序将无法继续，因为它将永远无法通过导致故障的指令。

​		作为可修复故障的一个例子，考虑一个自动扩展堆栈。在许多系统中，内核最初分配一个堆栈页，然后如果程序访问堆栈下的页面时出错，内核将自动分配这些页面并让程序继续。通过这样做，内核只分配程序所需的堆栈内存，但程序可以在其堆栈任意大的假象下工作。

系统调用为内存保护提出了一个有趣的问题。大多数系统调用接口允许用户程序向内核传递指针。这些指针指向要读取或写入的用户缓冲区。然后，内核在执行系统调用时对这些指针解引用。这有两个问题：

1. 内核中的页面错误可能比用户程序中的页面错误严重得多。如果内核页面在操作自己的数据结构时出错，那就是内核错误，错误处理程序应该会使内核（从而使整个系统）崩溃。但是，当内核解引用用户程序提供给它的指针时，它需要一种方法来记住，这些解引用导致的任何页面错误实际上代表用户程序。
2. 内核通常比用户程序拥有更多的内存权限。用户程序可能会传递一个指向系统调用的指针，该系统调用指向内核可以读取或写入但程序无法读取的内存。内核必须小心，不要被骗去引用这样的指针，因为这可能会泄露私有信息或破坏内核的完整性。

出于这两个原因，内核在处理用户程序提供的指针时必须非常小心。

- 现在，您将使用一种机制来解决这两个问题，该机制检查从用户空间传递到内核的所有指针。当程序向内核传递指针时，内核将**检查地址**是否在地址空间的用户部分，以及页表是否允许内存操作。因此，内核永远不会因为解引用用户提供的指针而出现页面错误。如果内核出现页面错误，它应该死机并终止。

## 4. Lab 4: Preemptive Multitasking

​		在A部分中，您将向JO添加多处理器支持，实现循环调度，并添加基本的环境管理系统调用（创建和破坏环境的调用，以及分配/映射内存的调用）。在第B部分中，您将实现一个类似Unix的fork（），它允许用户模式环境创建自身的副本。最后，在C部分中，您将添加对进程间通信（IPC）的支持，允许不同的用户模式环境显式地相互通信和同步。您还将添加对硬件时钟中断和抢占的支持。

### 4.1 Multiprocessor Support

- 我们将使JOS支持“**对称多处理器**”（SMP），这是一种多处理器模型，在该模型中，所有CPU对系统资源（如内存和I/O总线）都具有同等的访问权限。虽然SMP中的所有CPU功能相同，但在引导过程中，它们可以分为两种类型：**引导处理器**（BSP）负责初始化系统和引导操作系统；只有在操作系统启动并运行后，BSP才会激活**应用程序处理器**（AP）。哪个处理器是BSP由硬件和BIOS决定。到目前为止，所有现有的JOS代码都已在BSP上运行。

- 在SMP系统中，每个CPU都有一个附带的本地APIC（**LAPIC**）单元。LAPIC单元负责**在整个系统中提供中断**。LAPIC还为其连接的CPU**提供唯一标识符**。在本实验室中，我们使用了LAPIC单元的以下基本功能（在kern/lapic.c中）：
  - 读取LAPIC标识符（APIC ID）以告知代码当前运行在哪个CPU上（请参阅cpunum（）)
  - 将the `STARTUP` interprocessor interrupt （IPI）从BSP发送到APs以启动其他CPU（请参阅lapic_startap（））。
  - 在C部分中，我们对LAPIC的内置计时器进行编程，以触发时钟中断，从而支持抢占式多任务处理（请参见apic_init（）。
- 处理器使用**内存映射I/O**（MMIO）访问其LAPIC。在MMIO中，物理内存的一部分硬连线到一些I/O设备的寄存器，因此通常用于访问内存的相同加载/存储指令可用于访问设备寄存器。您已经在物理地址0xA0000处看到了一个IO孔（我们使用它来写入VGA显示缓冲区）。LAPIC位于一个从物理地址0xFE000000（32MB短于4GB）开始的洞中，因此它太高了，我们无法在KERNBASE使用通常的直接映射进行访问。JOS虚拟内存映射在 `MMIOBASE`上留下了4MB的空间，因此我们有一个地方可以映射这样的设备。由于后面的实验室引入了更多的MMIO区域，您将编写一个简单的函数来分配该区域的空间，并将设备内存映射到该区域。

```c
Exercise 1. Implement mmio_map_region in kern/pmap.c. To see how this is used, look at the beginning of lapic_init in kern/lapic.c. You'll have to do the next exercise, too, before the tests for mmio_map_region will run.
```

#### 4.1.1 Locking

- Xv6在多处理器上运行：具有多个独立执行的CPU的计算机.这种共享提高了一个CPU读取数据结构，而另一个CPU正在中途更新数据结构，甚至多个CPU同时更新相同的数据的可能性；即使在单处理器上，如果中断发生在错误的时间，使用与某些可中断代码相同数据的中断例程也可能损坏数据。
- 任何并发访问共享数据的代码都必须有一个策略，以在并发情况下保持正确性。并发性可能由多个内核、多个线程或中断代码的访问引起。xv6使用了一些简单的并发控制策略；更复杂的情况是可能的。本章重点介绍xv6和许多其他系统中广泛使用的策略之一：Lock

- 锁提供**互斥**，确保一次只有一个CPU可以持有锁。如果锁与每个共享数据项相关联，并且代码在使用给定项时始终持有关联的锁，那么我们可以确保一次仅从一个CPU使用该项。单个C语句可以是多个机器指令，因此，在这种情况下，我们说锁保护数据项。另一个处理器或中断可能在C语句的中间出现。您不能假定页面上的代码行是**原子执行**的。并发性使得正确性推理更加困难。

#### 4.1.2 Race conditions

- 竞争条件是**同时访问内存**位置，并且至少一次访问是写入的情况。竞争通常是错误的标志，要么是更新丢失（如果访问是写的），要么是读取未完全更新的数据结构。竞争的结果取决于所涉及的两个CPU的确切时间，以及它们的内存操作如何由内存系统排序，这会使竞争引起的错误难以再现和调试。
- 当我们说锁保护数据时，实际上是指锁保护应用于数据的一些**不变量集合**。不变量是跨操作维护的数据结构的属性。通常，操作的正确行为取决于操作开始时不变量是否为真。该操作可能会暂时违反不变量，但必须在完成之前重新建立它们。正确使用锁可以确保每次只有一个CPU可以对临界区的数据结构进行操作，这样，当数据结构的不变量不存在时，任何CPU都不会执行数据结构操作。
- 您可以将锁视为序列化并发关键部分，以便它们一次运行一个，从而保留不变量（假设它们在隔离状态下是正确的）。您还可以将关键部分视为相互之间的原子，因此，获得锁的临界区以后只会看到来自早期临界区的完整更改集，而不会看到部分完成的更新。

#### 4.1.3 Code:Lock

```c
//xv6/spinlock.h
1500 // Mutual exclusion lock.
1501 struct spinlock {
1502 uint locked; // Is the lock held?
1503 
1504 // For debugging:
1505 char *name; // Name of lock.
1506 struct cpu *cpu; // The cpu holding the lock.
1507 uint pcs[10]; // The call stack (an array of program counters)
1508 // that locked the lock.
1509 };
```

- 在启动APs之前，BSP应首先收集有关多处理器系统的信息，如CPU总数、APIC ID和LAPIC单元的MMIO地址。kern/mpconfig.c中的mp_init（）函数通过**读取驻留在BIOS内存区域中的mp配置表来检索此信息**。
- boot_aps（）函数（在kern/init.c中）驱动AP引导过程。AP以**实模式**启动，与boot/boot.S中启动引导加载程序的方式非常相似，因此boot_aps（）将AP条目代码（kern/mpentry.S）**复制**到实模式下可寻址的内存位置。与引导加载程序不同，我们可以控制AP开始执行代码的位置；我们将条目代码复制到0x7000（MPENTRY_PADDR），但任何未使用的、页面对齐的低于640KB的物理地址都可以使用。
- 之后，boot_aps（）通过向相应AP的LAPIC单元发送`STARTUP` IPIs(**处理器间中断**)以及AP开始运行其入口代码的初始CS:IP地址（在本例中为MPENTRY_PADDR），一个接一个地激活AP。kern/mpentry.S中的条目代码与boot/boot.S中的条目代码非常相似。经过一些简短的设置后，它将AP置于启用分页的保护模式，然后调用mp_main（）（也在kern/init.C中）。boot_aps（）**等待AP**在其struct CpuInfo的cpu_status字段中发出CPU_STARTED` flag信号，然后继续唤醒下一个。

```c
Exercise 2. Read boot_aps() and mp_main() in kern/init.c, and the assembly code in kern/mpentry.S. Make sure you understand the control flow transfer during the bootstrap of APs. Then modify your implementation of page_init() in kern/pmap.c to avoid adding the page at MPENTRY_PADDR to the free list, so that we can safely copy and run AP bootstrap code at that physical address. Your code should pass the updated check_page_free_list() test (but might fail the updated check_kern_pgdir() test, which we will fix soon).
    将MPENTRY_PADDR使用的空间标记为used
```

整理一下程序运行过程，此过程一直都运行在CPU0，即BSP上，工作在保护模式。

- i386_init调用了boot_aps()，也就是在BSP中引导其他CPU开始运行
- boot_aps调用`memmove`将每个CPU的boot代码加载到固定位置
- 最后调用`lapic_startap`执行其bootloader启动对应的CPU

mp_init（）读取配置信息----> boot_aps（）复制mpentry.S到内存，激活APs--->mp_main()

#### 4.1.3.1 Question

1. 将kern/mpentry.S与boot/boot.S比较。 请记住，就像内核中的其他内容一样，kern/mpentry.S被编译、链接并运行在`KERNBASE`之上，宏MPBOOTPHYS的目的是什么？ 为什么这在在kern/mpentry.S很关键？换句话说，如果在kern/mpentry.S中省略了什么可能会出错？*提示：回忆链接地址与加载地址的区别。*

```c
	宏MPBOOTPHYS是为求得变量的物理地址
    
    boot.S中，由于尚没有启用分页机制，所以我们能够指定程序开始执行的地方以及程序加载的地址；但是，在mpentry.S的时候，由于主CPU	  已经处于保护模式下了，因此是不能直接指定物理地址的，给定线性地址，映射到相应的物理地址是允许的。
```

### 4.1.4 Per-CPU State and Initialization

在编写多处理器操作系统时，区分每个处理器专用的每个CPU状态和整个系统共享的全局状态非常重要。kern/cpu.h定义了每个cpu的大多数状态，包括struct CpuInfo。cpunum（）总是返回调用它的CPU的ID，它可以用作数组cpus的索引。或者，宏thiscpu是the current CPU's `struct CpuInfo`的缩写。

以下是您应该注意的每CPU状态：

```c
// Per-CPU state
struct CpuInfo {
	uint8_t cpu_id;                 // Local APIC ID; index into cpus[] below
	volatile unsigned cpu_status;   // The status of the CPU
	struct Env *cpu_env;            // The currently-running environment.
	struct Taskstate cpu_ts;        // Used by x86 to find stack for interrupt
};
```

- **Per-CPU kernel stack**.

  - 因为多个CPU可以同时trap到内核中，所以我们需要为每个处理器提供一个单独的内核堆栈，以防止它们干扰彼此的执行。数组perpu_kstacks[NCPU] [KSTKSIZE]为NCPU的内核堆栈保留空间.

  - 在实验2中，您将引导堆栈所指的物理内存映射为BSP的内核堆栈，该堆栈位于KSTACKTOP的正下方。类似地，在本实验室中，您将把每个CPU的内核堆栈映射到这个区域，并使用保护页作为它们之间的缓冲区。CPU 0的堆栈仍将从顶部向下增长；CPU 1的堆栈将从CPU 0堆栈底部以下的KSTKGAP字节开始，依此类推。inc/memlayout.h显示映射布局。

- **Per-CPU TSS and TSS descriptor**.

  还需要每个CPU任务状态段（TSS）来指定每个CPU的内核堆栈所在的位置。CPU i的TSS存储在cpus[i].cpu_ts中，相应的TSS描述符在GDT条目gdt[(GD_TSS0 >> 3) + i]中定义。在kern/trap.c中定义的全局ts变量将不再有用。

- **Per-CPU current environment pointer**.

  由于每个CPU都可以同时运行不同的用户进程，因此我们重新定义了符号curenv，以引用`cpus[cpunum()].cpu_env` (or `thiscpu->cpu_env`)，它指向当前CPU（代码运行的CPU）上当前执行的环境。

- **Per-CPU system registers**.

  所有寄存器，包括系统寄存器，都是CPU专用的。因此，初始化这些寄存器的指令，如lcr3（）、ltr（）、lgdt（）、lidt（）等，必须在每个CPU上执行一次。函数env_init_perpu（）和trap_init_perpu（）就是为此而定义的。

```c
Exercise 3. Modify mem_init_mp() (in kern/pmap.c) to map per-CPU stacks starting at KSTACKTOP, as shown in inc/memlayout.h. The size of each stack is KSTKSIZE bytes plus KSTKGAP bytes of unmapped guard pages. Your code should pass the new check in check_kern_pgdir().
    
Exercise 4. The code in trap_init_percpu() (kern/trap.c) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs. (Note: your new code should not use the global ts variable any more.)
```

```C
// Normal segment 设置GDT表表项
#define SEG(type, base, lim, dpl) 					\
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,	\
    type, 1, dpl, 1, (unsigned) (lim) >> 28, 0, 0, 1, 1,		\
    (unsigned) (base) >> 24 }
#define SEG16(type, base, lim, dpl) (struct Segdesc)			\
{ (lim) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff,		\
    type, 1, dpl, 1, (unsigned) (lim) >> 16, 0, 0, 1, 0,		\
    (unsigned) (base) >> 24 }
```

- 内核栈的**栈地址存储在TSS段**中,而每个CPU都有一个独立的TSS段,在用户态向内核态切换的时候,需要用到TSS段中的部分信息,其中就包括esp0和ss0两个信息,esp0指向相应的CPU的内核栈,ss0指向内核的堆栈段寄存器(实际上现在的系统基本不用这寄存器,这个寄存器唯一的作用应该就是指明其特权级,即指出处于内核态还是用户态而已)所以,进程在进入中断的时候,栈指针能够通过对应的CPU的TSS寄存器自动的把栈转换为内核栈.

GDT中的一个描述符一共有8个字节组成：

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\20141001142133171.jpg)

保护模式下的段寄存器 由 **16位的选择器** 与 **64位的段描述符寄存器** 构成：

- 选择器：存储段描述符的索引（GDT索引）

- 段描述符寄存器： 存储段描述符（GDT表项）

  ![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\seg.PNG)

寄存器GDTR（16位表界限 + 32位基地址）用来存放GDT的入口地址，可以通过LGDT指令将GDT的入口地址装入此寄存器

- 段选择子（Selector）：由GDTR访问GDT是通过“段选择子”（实模式下的段寄存器）来完成的。

  ![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\sel.PNG)

  - 描述符索引（index）：表示所需要的段的描述符在描述符表的位置
  - TI ： TI值只有一位0或1，0代表在GDT，1代表在LDT
  - 请求特权级（RPL）：代表选择子的特权级，共有4个特权级（0级、1级、2级、3级）

#### 4.1.5 Locking

- 大内核锁简单易用。然而，它消除了内核模式下的所有并发。大多数现代操作系统使用不同的锁来保护其**共享状态的不同部分**（如下），这种方法称为**细粒度锁**。细粒度锁定可以显著提高性能，但更难实现且更容易出错。
  - The page allocator.
  - The console driver.
  - The scheduler.
  - The inter-process communication (IPC) state 

- 我们当前的代码在mp_main（）中初始化AP后旋转。在让AP更进一步之前，我们需要首先解决**多个CPU同时运行内核代码**时的竞争条件。实现这一点的最简单方法是使用一个大的内核锁。大内核锁是一个单独的全局锁，每当环境进入内核模式时都会保持该锁，当环境返回到用户模式时会释放该锁。在这个模型中，**用户模式**下的环境可以在任何可用的CPU上并发运行，但**内核模式**下只能运行一个环境；任何其他试图进入内核模式的环境都将被迫等待。

- kern/spinlock.h声明了大的内核锁，即 `kernel_lock`锁。它还提供了lock_kernel（）和unlock_kernel（），这是获取和释放锁的快捷方式。您应该在四个位置应用大内核锁：

  - 在i386_init（）中，在BSP唤醒其他CPU之前获取锁
  - 在mp_main（）中，初始化AP后获取锁，然后调用sched_yield（）开始在此AP上运行环境。
  - 在trap（）中，从用户模式trap时获取锁。要确定陷阱是在用户模式还是在内核模式下发生，请检查tf_cs的低位。
  - 在env_run（）中，在切换到用户模式之前释放锁。不要做得太早或太迟，否则你将经历竞争或死锁。

```c
Question：
   1. big kernel lock似乎已经确保每次仅仅一个CPU能运行内核代码， 为什么我们仍然需要为每个CPU设定一个内核栈
    
	答：进程自旋的位置是在内核态里面的，而不是在用户态里面的，所以即使是用了大内核锁，CPU的内核栈也不能共享，而要单独独立出来。因为在_alltraps到 lock_kernel()的过程中，进程已经切换到了内核态，但并没有上内核锁，此时如果有其他CPU进入内核，如果用同一个内核栈，则_alltraps中保存的上下文信息会被破坏，所以即使有大内核栈，CPU也不能用用同一个内核栈。同样的，解锁也是在内核态内解锁，在解锁到真正返回用户态这段过程中，也存在上述这种情况。
```

Question分析：

1. 每次进入内核态的时候，CPU是在trap（）里面才锁上内核锁，总共有两种情况：

```c
void
trap(struct Trapframe *tf)
{
    // Re-acqurie the big kernel lock if we were halted in
	// sched_yield() 时钟中断
	if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
		lock_kernel();
    //用户态进入
    if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Acquire the big kernel lock before doing any
		// serious kernel work.
		// LAB 4: Your code here.
		lock_kernel();//自旋锁
		assert(curenv);
        ......
    }
```

- CPU从停止状态，由于时钟中断进入内核态，此时会锁内核锁
- CPU从用户态进入内核态

2. 进程进入内核态都要通过内核栈保存原进程寄存器的状态，然后调用trap函数的过程，所以所有的CPU在进入内核的时候都会进行lock_kernel的过程。如果此时已经有其他CPU在内核态中，那么CPU就会自旋，一直等到内核锁放开。所以CPU自旋是在**内核态自旋**的，而不是用户态。

### 4.1.6 Round-Robin Scheduling

- kern/sched.c中的函数sched_yield（）负责选择要运行的新环境。它以循环方式顺序搜索envs[]数组，从之前运行的环境之后开始（如果没有以前运行的环境，则从数组的开头开始），选择找到的第一个状态为ENV_RUNNABLE的环境（请参见inc/ENV.h），并调用ENV_run（）跳入该环境。

- sched_yield（）决不能同时在两个CPU上运行相同的环境。它可以判断某个环境当前正在某个CPU（可能是当前CPU）上运行，因为该环境的状态将是ENV_running。

- 我们已经为您实现了一个新的系统调用sys_yield（），用户环境可以调用它来调用内核的sched_yield（）函数，从而自动将CPU交给另一个环境。

  ```c
  Exercise 6. Implement round-robin scheduling in sched_yield() as described above. Don't forget to modify syscall() to dispatch sys_yield().//实现轮转调度
  ```

  ```c
  Question:
  3. 在env_run（）的实现中，应该调用lcr3（）。在调用lcr3（）之前和之后，您的代码都会引用（至少应该如此）变量e，即env_run的参数。加载%cr3寄存器后，MMU使用的寻址上下文立即更改。但是虚拟地址（即e）相对于给定的地址上下文有意义——地址上下文指定虚拟地址映射到的物理地址。为什么指针e可以在寻址模式转换（内核和用户空间的转换）之前和之后解引用？
      	lcr3(PADDR(curenv->env_pgdir));//切换用户空间下页目录
  答：因为当前是运行在系统内核中的，而每个进程的页目录表中都是存在内核映射的。每个进程页目录表中虚拟地址高于UTOP之上的地方，只有UVPT不一样，其余的都是一样的，只不过在用户态下是看不到的。所以虽然这个时候的页目录表换成了下一个要运行的进程的页目录表，但是curenv的地址没变，映射也没变，还是依然有效的。
  4. 每当内核从一个环境切换到另一个环境时，它必须确保保存旧环境的寄存器，以便以后可以正确恢复它们。为什么？这在哪里发生？
  答：因为不保存下来就无法正确地恢复到原来的环境。用户进程之间的切换，会调用系统调用sched_yield()；用户态陷入到内核态，可以通过中断、异常、系统调用；这样的切换之处都是要在系统栈上建立用户态的TrapFrame，在进入trap()函数后，语句curenv->env_tf = *tf;将内核栈上需要保存的寄存器的状态实际保存在用户环境的env_tf域中。
  ```

  ### 4.1.7 System Calls for Environment Creation

- 尽管您的内核现在能够在多个用户级环境之间运行和切换，但它仍然限于内核最初设置的运行环境。现在，您将实现必要的JOS系统调用，以允许用户环境创建和启动其他新的用户环境。
- Unix提供fork（）系统调用作为其进程创建原语。Unix fork（）复制调用进程（父进程）的整个地址空间以创建新进程（子进程）。从用户空间中观察到的两个进程之间的唯一区别是它们的进程ID和父进程ID（由getpid和getppid返回）。在父进程中，fork（）返回子进程ID，而在子进程中，fork（）返回0。默认情况下，每个进程都有自己的私有地址空间，并且两个进程对内存的修改对另一个进程都不可见。

- 需要实现以下几个系统调用

| 系统调用           | 功能                                                         |
| ------------------ | ------------------------------------------------------------ |
| sys_exofork        | 创建一个新环境， 设定运行状态为`ENV_NOT_RUNNABLE`，拷贝父进程的寄存器值，置子进程返回值`eax`为0。 |
| sys_env_set_status | 修改环境的status                                             |
| sys_page_alloc     | 分配一个物理页，并插入到页表中（映射到虚拟地址上）           |
| sys_page_map       | 将源进程中的某个虚拟地址对应的页（**注意不是拷贝页里面的内容！**）映射到目标进程的某个虚拟地址上。 |
| sys_page_unmap     | 解除给定虚拟地址上的的映射                                   |

## 4.1.8 Part B: Copy-on-Write Fork

- 如前所述，Unix提供fork（）系统调用作为其主要进程创建原语。fork（）系统调用复制调用进程（父进程）的地址空间以创建新进程（子进程）。
- xv6 Unix通过将父级页面中的所有**数据复制**到分配给子级的新页面中来实现fork（）。这与dumbfork（）采用的方法基本相同。将父地址空间复制到子地址空间是fork（）操作中最昂贵的部分。
- 但是，对fork（）的调用通常紧接着子进程中对exec（）的调用，这将用新程序替换子进程的内存。例如，这就是shell的典型功能。在这种情况下，复制父进程地址空间所花费的时间在很大程度上是浪费的，因为子进程在调用exec（）之前只会占用很少的内存。
- 因此，Unix的更高版本利用虚拟内存硬件，允许父级和子级共享物理内存并映射到各自地址空间中，直到其中一个进程实际修改它为止。这种技术被称为**写时复制**。为此，在fork（）上，内核会将**地址空间映射**从父级复制到子级，而不是复制映射页面的内容，同时将现在**共享的页面标记为只读**。当两个进程中的一个试图写入其中一个共享页面时，该进程会出现**页面错误**。此时，Unix内核意识到页面实际上是一个“虚拟”或“写时复制”副本，因此它为出错进程创建了一个新的、私有的、可写的页面副本。通过这种方式，单个页面的内容在实际写入之前不会被复制。这种优化使得子对象中fork（）后跟exec（）的成本更低：子对象在调用exec（）之前可能只需要复制一个页面（其堆栈的当前页面）。
- 在**用户空间中实现fork（）**和**写时拷贝**支持的好处是内核仍然简单得多，因此更可能是正确的。它还允许各个用户模式程序为fork（）定义自己的语义。如果一个程序想要一个稍微不同的实现（例如，像dumbfork（）这样昂贵的总是复制的版本，或者一个父级和子级在之后实际共享内存的版本），那么它可以很容易地提供自己的实现。

### 4.1.9 User-level page fault handling

- 用户级别的写时复制需要知道**写保护页面**上的页面错误（page faults），所以这是您首先要实现的。写时复制只是用户级页面错误处理的许多可能用途之一。
- 通常设置地址空间，以便页面错误指示何时需要执行某些操作。例如，大多数Unix内核最初只映射一个新进程**堆栈区域**中的一个页面，然后随着进程堆栈消耗的增加并导致尚未映射的堆栈地址出现页面错误，**“按需”分配和映射**额外的堆栈页面。典型的Unix内核必须跟踪在进程空间的每个区域发生**页面错误**时要采取的操作。例如，**堆栈区域**中的故障通常会分配和映射物理内存的新页。程序的**BSS区域**中的故障通常会分配一个新页面，用零填充它，然后映射它。在具有按需分页可执行文件的系统中，文本区域中的错误将从磁盘读取二进制文件的相应页面，然后映射它。
- 这是内核需要跟踪的大量信息。您将决定如何处理用户空间中的每个页面错误，而不是采用传统的Unix方法，因为在用户空间中，bug的破坏性较小。这种设计的另一个好处是允许程序在定义其内存区域时具有极大的灵活性；稍后，您将使用用户级页面错误处理来映射和访问基于磁盘的文件系统上的文件。

#### 4.1.9.1 Setting the Page Fault Handler

- 为了处理自己的页面错误，用户环境需要向JOS内核注册页面错误处理程序入口点。用户环境通过新的sys_env_set_pgfault_upcall系统调用注册其页面错误入口点。我们在Env结构中添加了一个新成员**env_pgfault_upcall**，以记录此信息。


```c
Exercise 8. Implement the sys_env_set_pgfault_upcall system call. Be sure to enable permission checking when looking up the environment ID of the target environment, since this is a "dangerous" system call.
```

#### 4.1.9.2 Normal and Exception Stacks in User Environments

- 在正常执行期间，JOS中的用户环境将在**正常用户堆栈**上运行：其ESP寄存器开始指向USTACKTOP，它推送的堆栈数据位于USTACKTOP-PGSIZE和USTACKTOP-1（含USTACKTOP-1）之间的页面上。但是，当在**用户模式下发生页面错误**时，内核将重新启动用户环境，在不同堆栈（即**用户异常堆栈**）上运行指定的**用户级页面错误处理程序**。本质上，我们将使JOS内核代表用户环境实现自动“堆栈切换”，这与x86处理器在从用户模式转换到内核模式时已经代表JOS实现堆栈切换的方式非常相似！
- JOS**用户异常堆栈**的大小也是一页，其顶部定义为虚拟地址UXSTACKTOP，因此用户异常堆栈的有效字节从UXSTACKTOP-PGSIZE到UXSTACKTOP-1（包括UXSTACKTOP-1）。在这个异常堆栈上运行时，用户级页面错误处理程序可以使用JOS的常规系统调用来映射新页面或调整映射，以便修复最初导致页面错误的任何问题。然后，用户级页面错误处理程序通过汇编语言存根返回到原始堆栈上的错误代码。
- 每个希望支持用户级页面错误处理的用户环境都需要使用A部分介绍的sys_page_alloc（）系统调用为自己的异常堆栈分配内存。
- 在正常运行期间，用户进程运行在用户栈上，栈顶寄存器ESP指向USTACKTOP处，堆栈数据位于USTACKTOP-PGSIZE 与USTACKTOP-1之间的页。当在用户模式发生1个page fault时，内核将在专门处理page fault的用户异常栈上重新启动进程。
- 而异常栈则是为了上面设置的异常处理例程设立的。当异常发生时，而且该用户进程注册了该异常的处理例程，那么就会转到异常栈上，运行异常处理例程。
- 到目前位置出现了三个栈：
  - [KSTACKTOP, KSTACKTOP-KSTKSIZE] : 内核态系统栈
  - [UXSTACKTOP, UXSTACKTOP - PGSIZE] : 用户异常堆栈
  - [USTACKTOP, UTEXT] : 用户态运行栈
- 内核态系统栈是运行内核相关程序的栈，在有中断被触发之后，CPU会将栈自动切换到内核栈上来，而内核栈的设置是在kern/trap.c的trap_init_percpu()中设置的。

#### 4.1.9.3 Invoking the User Page Fault Handler

- 您现在需要更改kern/trap.c中的页面错误处理代码，以从用户模式处理页面错误，如下所示。我们将故障发生时的用户环境状态称为**trap-time**状态。
- 如果没有注册页面错误处理程序，那么JOS内核会像以前一样用一条消息破坏用户环境。否则，内核会在异常堆栈上设置一个陷阱帧，看起来像inc/trap.h中的struct UTrapframe：

```
               <-- UXSTACKTOP
trap-time esp
trap-time eflags
trap-time eip
trap-time eax       start of struct PushRegs
trap-time ecx
trap-time edx
trap-time ebx
trap-time esp
trap-time ebp
trap-time esi
trap-time edi       end of struct PushRegs
tf_err (error code)
fault_va            <-- %esp when handler is run
```

- 然后，内核安排用户环境恢复执行，页面错误处理程序在这个堆栈帧的异常堆栈上运行；你必须弄清楚如何做到这一点。fault_va是导致页面错误的虚拟地址。

- 如果发生异常时用户环境已在用户异常堆栈上运行，则页面错误处理程序本身已发生故障。在这种情况下，您应该在当前tf->tf_esp下启动新的堆栈帧，而不是在UXSTACKTOP。您应该首先推送一个32位的空字，然后推送一个struct UTrapframe。

- 要测试tf->tf_esp是否已在用户异常堆栈上，请检查它是否在UXSTACKTOP-PGSIZE和UXSTACKTOP-1（包括）之间。

```c
Exercise 9. Implement the code in page_fault_handler in kern/trap.c required to dispatch page faults to the user-mode handler. Be sure to take appropriate precautions（预防措施） when writing into the exception stack. (What happens if the user environment runs out of space on the exception stack?)
```

- 相比于Trapframe，UTrapframe这里多了utf_fault_va，因为要记录触发错误的内存地址，同时还少了es,ds,ss等。因为从用户态栈切换到异常栈，或者从异常栈再切换回去，实际上都是一个用户进程，所以不涉及到段的切换，不用记录。在实际使用中，Trapframe是作为记录进程完整状态的结构体存在的，也作为函数参数进行传递；而UTrapframe只在处理用户定义错误的时候用。


- 当**正常执行**过程中发生了页错误，那么栈的切换是
  - 用户运行栈—>内核栈—>异常栈
- 而如果在**异常处理程序**中发生了也错误，那么栈的切换是
  - 异常栈—>内核栈—>异常栈

- 当异常发生时，若用户环境已经运行在异常栈上，应该在当前的栈指针tf->tf_esp下建立新的栈帧。应该首先压入32 bit的空字，然后再压入struct UTrapframe结构体。


- 　用户定义注册了自己的中断处理程序之后，相应的例程运行时的栈，整个过程如下：
  1. 首先陷入到内核，栈位置从用户运行栈切换到内核栈，进入到trap中，进行中断处理分发，进入到page_fault_handler()
  2. 当确认是用户程序触发的page fault的时候(内核触发的直接panic了)，为其在用户异常栈里分配一个UTrapframe的大小
     把栈切换到用户异常栈，运行相应的用户中断处理程序，中断处理程序可能会触发另外一个同类型的中断，这个时候就会产生递归式的处理。处理完成之后，返回到用户运行栈。
- 如果当前已经在用户错误栈上了，那么需要留出4个字节，否则不需要，具体和跳转机制有关系。简单说就是在当前的错误栈顶的位置向下留出保存UTrapframe的空间，然后将tf中的参数复制过来。修改当前进程的**程序计数器和栈指针**，然后重启这个进程，此时就会在用户错误栈上运行中断处理程序了。当然，中断处理程序运行结束之后，需要再回到用户运行栈中，这个就是异常处理程序需要做的了

#### 4.1.9.4 User-mode Page Fault Entrypoint

- 接下来，您需要实现汇编例程，该例程将负责调用C页面错误处理程序，并在原始错误指令下恢复执行。此汇编例程是将使用sys\u sys_env_set_pgfault_upcall()向内核注册的处理程序。


```c
//当从用户定义的处理函数返回之后，如何从用户错误栈直接返回到用户运行栈。
Exercise 10. Implement the _pgfault_upcall routine in lib/pfentry.S. The interesting part is returning to the original point in the user code that caused the page fault. You'll return directly there, without going back through the kernel. The hard part is simultaneously switching stacks and re-loading the EIP.

//实现C库用户态的page fault处理函数。
//进程在运行前注册自己的页错误处理程序，重点是申请用户异常栈空间，最后添加上系统调用号。
Exercise 11. Finish set_pgfault_handler() in lib/pgfault.c.
```

- `_pgfault_upcall`是所有用户页错误处理程序的入口，在这里调用用户自定义的处理程序，并在处理完成后，从错误栈中保存的UTrapframe中恢复相应信息，然后跳回到发生错误之前的指令，恢复原来的进程运行。这一部分需要严格注意`struct UTrapFrame`各个字段的大小

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\utrap.png)

- 这里trap-time esp上的空间有1个4字节的保留空间，是做为**中断递归**的情形。
- 然后将栈中的trap-time esp取出减去4，再存回栈中。此时如果是中断递归中，esp-4即是保留的4字节地址；如果不是则是用户运行栈的栈顶。

- 再将原来出错程序的EIP(即trap-time eip)取出放入保留的4字节，以便后来恢复运行。此时的异常栈布局如下：

  ![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\20160709160808705.png)

- 紧接着恢复通用寄存器和EFLAG标志寄存器，此时的异常栈结构如下：

  ![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\20160709161503153.png)

- 然后pop esp切换为原来出错程序的运行栈，最后使用ret返回出错程序。

### 4.1.10 Implementing Copy-on-Write Fork

- 我们在lib/fork.c中为fork（）提供了一个框架。与dumbfork（）类似，fork（）应该创建一个新环境，然后扫描父环境的整个地址空间，并在子环境中设置相应的页面映射。关键区别在于，dumbfork（）复制页面时，fork（）最初只复制页面映射。fork（）将仅在其中一个环境尝试写入每个页面时复制该页面。

- `fork`控制流如下：

  1. 父进程使用上面实现的set_pgfault_handler（）函数将pgfault（）安装为C级页面错误处理程序。

  2. 父进程调用sys_exofork（）来创建子环境。

  3. 对于UTOP下方地址空间中的每个可写或写时复制页，父进程调用duppage，**duppage**应将写时复制页**映射到子进程**的地址空间，然后在其**自己的地址空间中**（标记为COW）**重新映射**写时复制页。[注意：此处的**顺序**（即在父项中标记页面之前，在子项中将页面标记为COW）实际上很重要！你知道为什么吗？试着考虑一个特定的情况，在这种情况下，逆转顺序可能会造成麻烦。]duppage设置这两个PTE，**使页面不可写**，并在“avail”字段中包含PTE_COW，以区分写时复制页面和真正的只读页面。

     但是，**异常堆栈**不会以这种方式重新映射。相反，您需要在子进程中为异常堆栈分配一个新页面。由于页面错误处理程序将执行实际复制，并且页面错误处理程序在异常堆栈上运行，因此无法使异常堆栈在写入时复制：谁将复制它？

     fork（）还需要处理存在但不可写或写时复制的页面（普通页面）。

  4. 父进程将子进程的**用户页面错误入口点**设置为其自己的入口点。

  5. 子进程现在已准备好运行，因此父进程将其标记为可运行。

- 每次其中一个进程在其尚未写过的写入页上写入副本写时复制页进行写操作时，都会出现页面错误。以下是用户页面错误处理程序的控制流：

  1. 内核将页面错误传播到_pgfault_upcall，后者调用fork（）的pgfault（）处理程序。
  2. pgfault（）检查故障是否为写入故障（检查**错误代码**中的FEC_WR）以及页面的**PTE**是否标记为PTE_COW。如果没有，那就panic.
  3. pgfault（）分配在临时位置映射的新页，并将出错页的内容复制到其中。然后，故障处理程序将新页面映射到具有读/写权限的适当地址，而不是旧的只读映射。

- 用户级lib/fork.c代码必须参考环境的页面表，以执行上述操作（例如，页面的PTE标记为PTE_COW）。内核将环境的页面表映射到UVPT，正是出于此目的。它使用了一个聪明的映射技巧，使得查找用户代码的PTE变得容易。lib/entry.S设置了uvpt和uvpd，以便您可以轻松地在lib/fork.c中查找页表信息。

```c
Exercise 12. Implement fork, duppage and pgfault in lib/fork.c.
    pgfault处理page fault时的写时拷贝
    duppage函数，负责进行COW方式的页复制，将当前进程的第pn页对应的物理页映射到envid的第pn页上去，同时将这一页都标记为COW。
```

#### 4.1.10.1 UVPT

- 页表的一个很好的概念模型是一个2^20条目数组，它可以通过物理页码进行索引。x86的2级分页方案打破了这种简单的模式，将巨大的页面表分割成多个页面表和一个页面目录。在内核中，我们使用pgdir_walk（）通过遍历两级页面表来查找条目。以某种方式将这个巨大的简单页面表取回会很好——JOS中的进程将查看它，以了解它们的地址空间中发生了什么。但是怎么做呢？


- JOS利用**分页硬件**使用的一个巧妙技巧——分页硬件非常适合将一组碎片页面组合到一个连续的地址空间中。事实证明，我们已经有了一个表，其中包含指向所有碎片页面表的指针：它是页面目录！


- 因此，我们可以使用**页面目录**作为页面表，在虚拟地址空间中的某个连续的2^22^字节范围内映射我们的2^22^字节页面表（由1024个页面表示）。通过将PDE条目标记为只读，我们可以确保用户进程不能修改其页面表。


![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\pagetables.png)

- CR3指向页面目录。地址的PDX部分将索引到页面目录中，为您提供一个页面表。PTX部件索引到页面表中，为您提供一个页面，然后您在中添加offset。


- 但是处理器没有页面目录、页面表和页面是普通内存以外的任何内容的概念。因此，没有任何东西表明内存中的某个页面不能同时充当其中的两个或三个页面。处理器只遵循指针：pd = lcr3(); pt = *(pd+4*PDX); page = *(pt+4*PTX);


- 如果我们在页面目录中放一个指针，指向索引为V处的自身，当我们尝试用PDX，PTX操作转换虚拟地址时，最后又会回到页目录。即一个虚拟页指向了存储页目录的页。在Jos中，V是0x3BD，因此UVPD的虚拟地址是（0x3BD<<22）|（0x3BD<<12）。相当于我们在之前实验遇到的一条语句。` kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir) | PTE_U | PTE_P;`如下图所示：

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\vpt.png)

- 现在，如果我们尝试用PDX=V转换一个虚拟地址，但是转换一个任意的PTX!=V、 然后，在CR3中的三个箭头后面结束一个级别（而不是上一个案例中的两个），也就是说在页面表中。因此，PDX=V的虚拟页面集形成一个4MB的区域，就处理器而言，该区域的页面内容就是页面表本身。在JOS中，V是0x3BD，因此UVPT的虚拟地址是（0x3BD<<22）

  0x3BD = 0011_1011_1101 << 22 ---> 1110_1111_0100_0000_0000_0000_0000_0000; PDX = 0X3BD= 957;1110_1111_0100_0000_0000_0000_0000_0000 = 0xef400000，而 UVPT = 0xef400000 !

- 因此，由于我们巧妙地将“no op”箭头插入到页面目录中，我们将用作页面目录和页面表（通常不可见）的页面映射到虚拟地址空间中。所以我们能通过`uvpt`and `uvpd`直接访问页表和页目录，其初始化在`lib/entry.S`中实现。


- 与虚拟地址范围[UVPT，UVPT+PTSIZE]对应的页面目录条目指向页面目录本身。因此，页面目录既是页面表又是页面目录。


1. 将页面目录视为页面表的一个结果是，可以通过虚拟地址UVPT（在lib/entry.S中设置了UVPT）处的“虚拟页面表”访问所有PTE。页码N的PTE存储在uvpt[N]中(值得为此画一张图表！
2. 第二个结果是，当前页面目录的内容将始终在虚拟地址（UVPT+（UVPT>>PGSHIFT））处可用，在lib/entry.S中设置了uvpd。

```c
extern volatile pte_t uvpt[];     // VA of "virtual page table"
extern volatile pde_t uvpd[];     // VA of current page directory
```

- 与之前的dumbfork不同，fork出一个子进程之后，首先要进行的就是将父进程的页表的全部映射拷贝到子进程的地址空间中去。这个时候物理页会被两个进程同时映射，但是在写的时候是应该隔离的。采取的方法是在子进程映射的时候，将父进程空间中所有可以写的页表的部分全部标记为可读且COW。而当父进程或者子进程任意一个发生了写的时候，因为页表现在都是不可写的，所以会触发异常，进入到我们设定的page fault处理例程，当检测到是对COW页的写操作的情况下，就可以将要写入的页的内容全部拷贝一份，重新映射。

- 在pgfault函数中先判断是否页错误是由写时拷贝造成的，如果不是则panic。借用了一个一定不会被用到的位置PFTEMP，专门用来发生page fault的时候拷贝内容用的。然后将addr映射到PFTEMP映射的页，然后解除PFTEMP的页映射关系。


# 4.2 Part C: Preemptive Multitasking and Inter-Process communication (IPC)

- 在实验4的最后一部分中，您将修改内核以抢占不合作的环境，并允许环境显式地相互传递消息。

### 4.2.1 Clock Interrupts and Preemption

- 运行user/spin程序。这个测试程序派生出一个子环境，一旦它接收到CPU的控制，它就会在一个紧密的循环中永远旋转。无论是父环境还是内核都不会重新获得CPU。这显然不是在用户模式环境中保护系统免受bug或恶意代码攻击的理想情况，因为任何用户模式环境都可以通过进入无限循环而停止整个系统，并且永远不会返回CPU。为了允许内核抢占运行环境，强行重获CPU控制权，我们必须扩展JOS内核以支持来自时钟硬件的外部硬件中断。

#### 4.2.2 Interrupt discipline

- **外部中断**（即设备中断）称为IRQ。有16个可能的IRQ，编号为0到15。从IRQ编号到IDT条目的映射不是固定的。picirq.c中的pic_init通过IRQ_OFFSET+15将IRQ 0-15映射到IDT条目IRQ_OFFSET。外部中断的初始化，实际上就是对硬件 8259A的初始化。
- 在inc/trap.h中，IRQ_OFFSET定义为十进制32。因此，IDT条目32-47对应于IRQ 0-15。例如，时钟中断为IRQ 0。因此，IDT[IRQ_OFFSET+0]（即IDT[32]）包含内核中**时钟中断处理程序**的地址。选择此IRQ_OFFSET是为了使设备中断不会与处理器异常重叠，这显然会导致混淆(事实上，在运行MS-DOS的PC早期，IRQ_偏移量实际上为零，这确实在处理硬件中断和处理处理器异常之间造成了巨大的混乱！）
- 在JOS中，与xv6 Unix相比，我们进行了简化。外部设备中断**在内核中总是被禁用**的（和xv6一样，在用户空间中也是启用的）。外部中断由%eflags寄存器的FL_IF标志位控制（参见inc/mmu.h）。设置此位时，外部中断被启用。虽然可以通过多种方式修改位，但由于我们的简化，我们将在进入和离开用户模式时，仅通过保存和恢复%eflags寄存器的过程来处理它。
- 您必须确保**在用户环境中运行时设置FL_IF标志**，以便当中断到达时，它会传递到处理器并由中断代码处理。否则，中断将被屏蔽或忽略，直到中断被重新启用。我们用Bootloader的第一条指令屏蔽了中断，到目前为止，我们还没来得及重新启用它们。

```c
Exercise 13. Modify kern/trapentry.S and kern/trap.c to initialize the appropriate entries in the IDT and provide handlers for IRQs 0 through 15. Then modify the code in env_alloc() in kern/env.c to ensure that user environments are always run with interrupts enabled.

Also uncomment the sti instruction in sched_halt() so that idle CPUs unmask interrupts.
    
    1. 修改Trapentry.s，当调用硬件中断处理时，处理器不会传入错误代码，因此我们需要调用TRAPHANDLER_NOEC宏。
    2. 修改trap.c， 注册IDT。
    3. 在env_alloc中设置FL_IF位（用户模式下启用中断）
    4. 取消sched_halt（）中的注释
    处理器在调用硬件中断处理程序时从不推送错误代码，在完成此练习后，如果您使用任何运行时间非常长（例如spin）的测试程序运行内核，您应该会看到内核打印硬件中断陷阱帧。虽然中断现在已在处理器中启用，但JOS尚未处理它们，因此您应该看到它将每个中断错误地归因于当前运行的用户环境，并将其销毁。最终，它应该会耗尽环境来销毁并进入监视器。
```

#### 4.2.3 Handling Clock Interrupts

- 在user/spin程序中，在第一次运行子环境之后，它只是在一个循环中旋转，内核再也没有得到控制权。我们需要对硬件进行编程，定期生成时钟中断，这将迫使控制返回到内核，在那里我们可以将控制切换到不同的用户环境。

- 我们为您编写的对lapic_init和pic_init（来自init.c中的i386_init）的调用设置了时钟和中断控制器以生成中断。现在需要编写代码来处理这些中断。

  ```c
  Exercise 14. Modify the kernel's trap_dispatch() function so that it calls sched_yield() to find and run a different environment whenever a clock interrupt takes place.
      您现在应该能够让user/spin测试开始工作了：父环境应该将子环境、sys_yield（）交给它几次，但在每种情况下，在一个时间片后重新获得对CPU的控制，并最终杀死子环境并优雅地终止。
  ```

### 4.3 Inter-Process communication (IPC)

- 您将实现几个附加的JOS内核系统调用，这些调用共同提供了一个简单的进程间通信机制。您将实现两个系统调用，sys_ipc_recv and sys_ipc_try_send。然后您将实现两个库包装器ipc_recv和ipc_send。

- 用户环境可以使用JOS的IPC机制相互发送的“消息”由两个组件组成：一个32位的值和一个可选的单页映射。允许环境在消息中传递页面映射提供了一种有效的方法，可以传输比单个32位整数更多的数据，还允许环境轻松设置共享内存安排。

#### 4.3.1 Sending and Receiving Messages

- 要接收消息，进程调用sys_ipc_recv。此系统调用取消当前环境的计划，并且在收到消息之前不会再次运行它。当一个环境正在等待接收消息时，任何其他环境都可以向其发送消息—不仅仅是特定环境，也不仅仅是与接收环境有父/子关系的环境。换句话说，您在A部分中实现的权限检查将不适用于IPC，因为IPC系统调用经过精心设计，以确保“安全”：一个环境不能仅仅通过发送消息就导致另一个环境出现故障（除非目标环境也有缺陷）。
- 要尝试发送消息，进程调用sys_ipc_try_send，同时使用接收方的进程id和要发送的值。如果命名环境实际正在接收（它调用了sys_ipc_recv，但尚未获得值），则发送将传递消息并返回0。否则，发送将返回-E_IPC_NOT_RECV，以指示目标进程当前不希望接收值。
- 用户空间中的库函数ipc_recv将负责调用sys_ipc_recv，然后在当前环境的struct Env中查找有关接收值的信息。
- 类似地，库函数ipc_send将负责反复调用sys_ipc_try_send，直到发送成功。

#### 4.3.2 Transferring Pages

- 当环境使用有效的dstva参数（在UTOP下面）调用sys_ipc_recv时，环境表示它愿意接收页面映射。如果发送方发送一个页面，则该页面应映射到接收方地址空间中的dstva。如果接收器已经在dstva上映射了一个页面，则上一个页面将被取消映射。
- 当环境使用有效的srcva调用sys_ipc_try_send（在UTOP下面）时，这意味着发送方希望将当前映射在srcva的页面发送给接收方，并具有perm权限。IPC成功后，发送方在其地址空间中保留srcva页面的原始映射，但接收方也在接收方最初指定的dstva地址空间中获得该物理页面的映射。因此，此页面将在发送方和接收方之间**共享**。

- 如果发送者或接收者未指示应传输页面，则不传输页面。在任何IPC之后，内核将接收方的env结构中的新字段env_ipc_perm设置为接收到的页面的权限，如果没有接收到页面，则设置为零。

```c
Exercise 15. Implement sys_ipc_recv and sys_ipc_try_send in kern/syscall.c. Read the comments on both before implementing them, since they have to work together. When you call envid2env in these routines, you should set the checkperm flag to 0, meaning that any environment is allowed to send IPC messages to any other environment, and the kernel does no special permission checking other than verifying that the target envid is valid.

Then implement the ipc_recv and ipc_send functions in lib/ipc.c.
```

- `thisenv`在`lib/libmain.c`中定义。`const volatile struct Env *thisenv;`。`currenv`在`kern/env.h`中定义`#define curenv (thiscpu->cpu_env) `。thisenv到底起到了什么作用？记录需要运行的用户环境，可以传递给`env_run()`去运行。 而curenv实实在在得记录了当前CUP运行的环境。

# 5. Lab 5: File system, Spawn and Shell

- 在本实验室中，您将实现spawn，这是一个在磁盘可执行文件上加载和运行的库调用。然后，您将充实内核和库操作系统，使其足以在控制台上运行shell。这些特性需要一个文件系统，本实验室介绍了一个简单的读/写文件系统。

- ```
  fs/fs.c代码，用于维护文件系统的磁盘结构。
  fs/bc.c一个简单的块缓存，构建在我们的用户级页面错误处理工具之上。
  fs/ide.c最小基于PIO（非中断驱动）的ide驱动程序代码。
  fs/serv.c使用文件系统IPC与客户机环境交互的文件系统服务器。
  lib/fd.c代码，它实现了通用的类UNIX文件描述符接口。
  lib/file.c磁盘文件类型的驱动程序，作为文件系统IPC客户端实现。
  lib/console.c控制台输入/输出文件类型的驱动程序。
  lib/spawn.c生成库调用的代码框架。
  ```

- 在合并到新的lab 5代码后，您应该再次运行lab 4中的pingpong, primes, and forktree 测试用例。您需要注释掉kern/init.c中的ENV_CREATE（fs_fs）行，因为fs/fs.c尝试执行一些I/O，而JOS还不允许这样做。类似地，临时注释掉lib/exit.c中close_all()的调用；此函数调用您稍后将在实验室中实现的子例程，因此，如果被调用，它将死机。如果您的Lab4代码不包含任何bug，那么测试用例应该可以正常运行。在他们工作之前不要继续。开始练习1时，不要忘记取消注释这些行。

  如果它们不起作用，请使用git diff lab4检查所有更改，确保您为lab4（或之前）编写的代码没有从lab 5中丢失。确保lab 4仍然有效。

# 5.1 File system preliminaries(文件系统预备工作)

- 您将使用的文件系统比大多数“真实”文件系统（包括xv6 unix）要简单得多，但它的功能强大到足以提供基本功能：创建、读取、写入和删除以分层目录结构组织的文件。

- 我们（无论如何目前）只开发一个单用户操作系统，它提供的保护足以捕获bug，但不能保护多个相互可疑的用户。因此，我们的文件系统不支持UNIX文件所有权或权限的概念。与大多数UNIX文件系统一样，我们的文件系统目前也不支持硬链接、符号链接、时间戳或特殊设备文件。

## 5.1.1 On-Disk File System Structure

- 大多数UNIX文件系统将可用磁盘空间分为两种主要类型的区域：**inode和data区域**。UNIX文件系统为文件系统中的每个文件分配一个inode；文件的**inode区域**保存有关文件的关键元数据，例如文件的stat属性和指向其数据块的指针。**data区域**被划分为更大（通常为8KB或更大）的数据块，文件系统在其中存储文件数据和目录元数据。**目录项**包含文件名和指向inode的指针；如果文件系统中的多个目录项引用文件的inode，则称该文件为**硬链接文件**。由于我们的文件系统不支持硬链接，因此我们不需要这种级别的间接寻址，因此可以进行方便的简化：我们的文件系统根本不使用inode，而是将文件（或子目录）的所有元数据存储在描述该文件的（一个且唯一的）目录项中。

- 文件和目录在逻辑上都由一系列数据块组成，这些数据块可能分散在磁盘上，就像环境的虚拟地址空间的页面分散在物理内存中一样。文件系统环境隐藏了块布局的细节，提供了在文件中以任意偏移量读取和写入字节序列的接口。作为执行文件创建和删除等操作的一部分，文件系统环境在内部处理对目录的所有修改。我们的文件系统允许用户环境直接读取目录元数据（例如，通过read），这意味着用户环境可以自己执行目录扫描操作（例如，实现ls程序），而不必依赖于对文件系统的额外特殊调用。这种目录扫描方法的缺点，也是大多数现代UNIX变体不鼓励这种方法的原因，是它使应用程序依赖于目录元数据的格式，使得在不更改或至少不重新编译应用程序的情况下，很难更改文件系统的内部布局。

### 5.1.2 Sectors and Blocks

- 大多数磁盘不能以字节粒度执行读写操作，而是以**扇区**为单位执行读写操作。在JOS中，每个扇区为512字节。文件系统实际上以块为单位分配和使用磁盘存储。请注意这两个术语之间的区别：扇区大小是**磁盘硬件**的属性，而**块**大小是使用磁盘的**操作系统**的一个方面。文件系统的块大小必须是底层磁盘扇区大小的倍数。

- UNIX xv6文件系统使用512字节的块大小，与底层磁盘的扇区大小相同。然而，大多数现代文件系统使用更大的块大小，因为存储空间变得更便宜，而且以更大的粒度管理存储更有效。我们的文件系统将使用**4096字节**的块大小，方便地匹配处理器的页面大小。

### 5.1.3 Superblocks

- 文件系统通常在磁盘上的“易于查找”位置（如最开始或最结束）**保留**某些磁盘块，以**保存描述文件系统整体属性的元数据**，如块大小、磁盘大小、查找根目录所需的任何元数据、文件系统上次装入的时间、，上次检查文件系统错误的时间，等等。这些特殊块称为超级块。

- 我们的文件系统将有一个**超级块**，它将始终位于磁盘上的块1。其布局由inc/fs.h中的struct Super定义。块0通常保留用于保存boot loaders and partition tables,，因此文件系统通常不使用第一个磁盘块。许多“真实”文件系统维护多个超级块，这些超级块在磁盘的多个间隔较宽的区域中复制，因此，如果其中一个超级块损坏或磁盘在该区域出现介质错误，则仍可找到其他超级块并使用它们访问文件系统。

  ```c
  struct Super {
  	uint32_t s_magic;		// Magic number: FS_MAGIC
  	uint32_t s_nblocks;		// Total number of blocks on disk
  	struct File s_root;		// Root directory node
  };
  ```

### 5.1.4 File Meta-data

- 文件系统中描述文件的元数据布局由inc/fs.h中的struct file描述。此元数据包括文件的名称、大小、类型（常规文件或目录）以及指向组成文件的块的指针。如上所述，我们没有索引节点，所以元数据存储在磁盘上的目录项中。与大多数“真实”文件系统不同，为简单起见，我们将使用此单文件结构来表示文件元数据，因为它同时出现在磁盘和内存中。

  ```c
  struct File {
  	char f_name[MAXNAMELEN];	// filename
  	off_t f_size;			// file size in bytes
  	uint32_t f_type;		// file type
  
  	// Block pointers.
  	// A block is allocated iff its value is != 0.
  	uint32_t f_direct[NDIRECT];	// direct blocks
  	uint32_t f_indirect;		// indirect block
  
  	// Pad out to 256 bytes; must do arithmetic in case we're compiling
  	// fsformat on a 64-bit machine.
  	uint8_t f_pad[256 - MAXNAMELEN - 8 - 4*NDIRECT - 4];
  } __attribute__((packed));	// required only on some 64-bit machines
  ```

- struct文件中的f_direct数组包含存储文件前10个（NDIRECT）块的块号的空间，我们称之为文件的direct块。对于大小不超过10*4096=40KB的小文件，这意味着所有文件块的块号将直接适合文件结构本身。但是，对于较大的文件，我们需要一个位置来保存文件的其余块号。因此，对于任何大于40KB的文件，我们分配一个额外的磁盘块，称为文件的间接块，以容纳多达4096/4=1024个额外的块号。因此，我们的文件系统允许文件的大小达到1034个块，或略大于4兆字节。为了支持较大的文件，“真实”文件系统通常也支持双间接块和三间接块。

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\file.png)

![](C:\Users\Administrator.MICRO-901192021\Desktop\mit6.828\图片\disk.png)

### 5.1.5 Directories versus Regular Files

- 文件系统中的`File` structure可以表示**常规文件或目录**；这两种类型的“文件”通过文件结构中的类型字段进行区分。文件系统以完全相同的方式管理常规文件和目录文件，只是它根本不解释与常规文件关联的数据块的内容，而文件系统将**目录文件**的内容解释为描述目录中的**文件和子目录**的一系列`File` structure。

- 文件系统中的**超级块**包含一个文件结构（struct Super中的**s_root**字段），它保存文件系统根目录的元数据。此目录文件的内容是一系列`File` structure，描述文件系统**根目录**中的文件和目录。根目录中的任何子目录都可能包含更多表示子目录的文件结构，以此类推。

- 本实验室的目标不是让您实现整个文件系统，而是让您只实现某些关键组件。特别是，您将负责将块读入块缓存并将其刷新回磁盘；分配磁盘块；将文件偏移量映射到磁盘块；在IPC接口中实现read, write,open。由于您不会自己实现所有文件系统，因此熟悉提供的代码和各种文件系统接口非常重要。

## 5.2 Disk Access

- 我们操作系统中的文件系统环境需要能够访问磁盘，但我们尚未在内核中实现任何磁盘访问功能。我们没有采用传统的“单片”操作系统策略，即向内核添加IDE(Integrated Drive Electronics，电子集成驱动器)磁盘驱动程序以及必要的系统调用以允许文件系统访问它，而是将IDE磁盘驱动程序作为用户级文件系统环境的一部分来实现。我们仍然需要稍微修改内核，以便进行设置，使文件系统环境具有实现磁盘访问本身所需的权限。

- 只要我们依靠轮询、“基于编程I/O”（PIO）的磁盘访问，并且不使用磁盘中断，就可以很容易地在用户空间中实现磁盘访问。在用户模式下也可以实现中断驱动的设备驱动程序（例如，L3和L4内核可以这样做），但更困难的是，内核必须记录设备中断并将其分配到正确的用户模式环境。

- x86处理器使用**EFLAGS寄存器**中的IOPL位来确定是否允许**受保护模式代码**执行特殊设备I/O指令，如IN和OUT指令。由于我们需要访问的所有IDE**磁盘寄存器**都位于x86的I/O空间中，而不是内存映射，因此我们只需向文件系统环境授予“I/O权限”即可允许文件系统访问这些寄存器。实际上，EFLAGS寄存器中的IOPL位为内核提供了一种简单的“all-or-nothing”方法来控制用户模式代码是否可以访问I/O空间。在本例中，我们希望**文件系统环境**能够访问I/O空间，但我们不希望任何其他环境能够访问I/O空间。

```c
Exercise 1. i386_init identifies the file system environment by passing the type ENV_TYPE_FS to your environment creation function, env_create. Modify env_create in env.c, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.

Make sure you can start the file environment without causing a General Protection fault. You should pass the "fs i/o" test in make grade.
```

编译时出错-Werror=address-of-packed-member，在GNUmakefile中加入-Wno-address-of-packed-member

请注意，本实验室中的GNUmakefile文件将QEMU设置为使用文件obj/kern/kernel.img作为磁盘0的映像（通常在DOS/Windows下为“驱动器C”），并使用（新）文件obj/fs/fs.img作为磁盘1的映像（“驱动器D”）。在这个实验室里，我们的文件系统应该只接触磁盘1；磁盘0仅用于引导内核。如果您试图以某种方式损坏任一磁盘映像，只需键入以下内容，即可将它们重置为原始的“原始”版本：

```c
$ rm obj/kern/kernel.img obj/fs/fs.img
$ make
    
or by doing:
$ make clean
$ make
```

## 5.3 The Block Cache

- 在我们的文件系统中，我们将在处理器的虚拟内存系统的帮助下实现一个简单的“缓冲缓存”（实际上只是块缓存）。块缓存的代码在fs/bc.c中。

- 我们的文件系统将仅限于处理3GB或更小大小的磁盘。我们保留文件系统环境地址空间的一个大的、固定的3GB区域，从0x10000000（DISKMAP）到0xD0000000（DISKMAP+DISKMAX），作为磁盘的“内存映射”版本。例如，磁盘块0映射到虚拟地址0x10000000，磁盘块1映射到虚拟地址0x10000000，依此类推。fs/bc.c中的diskaddr函数实现了从磁盘块号到虚拟地址的转换（以及一些健全性检查）。

- 由于我们的文件系统环境有自己的虚拟地址空间，独立于系统中所有其他环境的虚拟地址空间，并且文件系统环境只需要实现文件访问，因此以这种方式保留大部分文件系统环境的地址空间是合理的。如果在32位机器上实现真正的文件系统，那么这样做会很尴尬，因为现代磁盘大于3GB。在具有64位地址空间的机器上，这种**缓冲区高速缓存管理**(a buffer cache management)方法仍然是合理的。

- 当然，将整个磁盘读入内存需要很长时间，因此我们将实现一种请求分页的形式，其中我们只在磁盘映射区域分配页面，并在该区域出现页面错误时从磁盘读取相应的块。这样，我们可以假装整个磁盘都在内存中。

```c
Exercise 2. Implement the bc_pgfault and flush_block functions in fs/bc.c. bc_pgfault is a page fault handler, just like the one your wrote in the previous lab for copy-on-write fork, except that its job is to load pages in from the disk in response to a page fault. When writing this, keep in mind that (1) addr may not be aligned to a block boundary and (2) ide_read operates in sectors, not blocks.
```































