#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

void sig_handler(int signo){
    pid_t cur_pid = getpid();
    

    printf("process[%d] is going to exit!\n", cur_pid);
    printf("异常发生时程序现场如下:\n");
    
    for(int i = 0; i < 30; i++){
    	long reg;

        asm volatile(
            "mov x7, #8\n"
            "mul w7, %w1, w7\n"
            "ldr %0, [sp, x7]\n"

            : "=r" (reg)
            : "r" (i)
            : "memory"
        );

        printf("寄存器X%d的值为：%lx\n", i, reg);
    }

    long lr, sp, pc, cpsr;

    asm volatile(
        "ldr %0, [sp, # 240]\n"
        "ldr %1, [sp, #248]\n"
        "ldr %2, [sp, #256]\n"
        "ldr %3, [sp, #264]\n"
        :"=r"(lr), "=r"(sp), "=r"(pc), "=r"(cpsr)
        :
        :"memory"
    );

    printf("寄存器LR的值为：%lx\n", lr);
    printf("寄存器SP的值为：%lx\n", sp);
    printf("寄存器PC的值为：%lx\n", pc);
    printf("寄存器PSTATE的值为：%lx\n", cpsr);
    exit(0);
}

int main(){
    pid_t cpid;
    
    signal(SIGSEGV, sig_handler);
    signal(SIGILL, sig_handler);
    signal(SIGBUS, sig_handler);

    if((cpid = fork()) == 0){
        int *p = NULL;
          
        *p = 1;
    }
    while (1);
    
    waitpid(cpid, NULL, WNOHANG);
    return 0;
}
