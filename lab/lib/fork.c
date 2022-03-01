// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	//写或写时复制才触发 进行实际拷贝
	if (!((err & FEC_WR)  && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault :it's not writable or a non-cow page!\n");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	envid_t envid = sys_getenvid();
	if ((r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P | PTE_W | PTE_U)) < 0)
		panic("pgfault: sys_page_alloc: %e\n", r);
	addr = ROUNDDOWN(addr, PGSIZE);
	memcpy((void *)PFTEMP, (const void *)addr, PGSIZE);
	//此时页面可写
	if ((r = sys_page_map(envid, (void *)PFTEMP, envid, (void *)addr, PTE_P | PTE_W | PTE_U)) < 0)
		panic("pgfault: sys_page_map: %e\n", r);

	if ((r = sys_page_unmap(envid, (void *) PFTEMP)) < 0)
		panic("pgfault: page unmap failed %e\n", r);
	// LAB 4: Your code here.

	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//拷贝映射
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	pte_t *pte;
	void *addr;
	int perm;
	// LAB 4: Your code here.
	addr = (void *)((uint32_t)pn * PGSIZE);
	perm =  PTE_P | PTE_U;//页面不可写
	//可写或者写时复制页进行特殊处理，普通页则不用
	if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) 
		perm |= PTE_COW;
	//target envid
	if ((r = sys_page_map(thisenv->env_id, addr, envid, addr, perm)) < 0)
		return r;
	//self envid 
	if (perm & PTE_COW) {
		if ((r = sys_page_map(thisenv->env_id, addr, thisenv->env_id, addr, perm)) < 0)
			return r;
	}
	//panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//1. Set up our page fault handler appropriately
	set_pgfault_handler(pgfault);

	//2. Create a child.
	envid_t envid;
	if ((envid = sys_exofork()) < 0)
		panic("sys_exofork failed: %e\n", envid);
	//子进程返回
	if (envid == 0) {
		// child
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	//父进程工作
	//3. Copy our address space and page fault handler setup to the child.
	//3.1 [UTEXT, USTACKTOP)范围内的地址空间调用调用duppage
	int r;
	for (size_t pn = PGNUM(UTEXT); pn < PGNUM(USTACKTOP); ++pn) {
		//复制父进程存在的页
		//uvpd: 页目录项 uvpt: 页表项
		if ((uvpd[pn >> 10] & PTE_P) && (uvpt[pn] & PTE_P)) {
			if ((r = duppage(envid, pn)) < 0)
				return r;
		}
	}
	//3.2 allocate a new page for the child's user exception stack.
	//用户异常栈不用写时复制 UXSTACKTOP
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W)) < 0)
		return r;
	//3.3 设置用户页面错误入口点（与自己一样）
	// Assembly language pgfault entrypoint defined in lib/pfentry.S.
	extern void _pgfault_upcall(void);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		return r;

	//4. mark the child as runnable and return.
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e\n", r);

	return envid;
	//panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
