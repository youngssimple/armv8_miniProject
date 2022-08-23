#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ucontext.h>

#define NUM 5
//函数名和其地址建立对应关系的结构体

typedef struct symtab_e{
    void  * fun_addr;
    char name[20];
}symtab_e;

void sigsegv_error1(int);
void sigsegv_error2();
void sig_handler(int signo, siginfo_t * sig, void * context);
void func1();

int main(){
    pid_t cpid;
	//安装信号处理器
    struct sigaction act;
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGILL, &act, NULL);

	//建立子进程。触发sigsegv
    if((cpid = fork()) == 0){
		func1();
		
		printf("已从问题函数中返回\n");

		exit(0);
	}
    waitpid(cpid, NULL, WNOHANG);
    printf("Parent process is still working\n");


    return 0;
}

void func1(){
	int i = 1;

	sigsegv_error1(i);
}


void sigsegv_error1(int i){
	sigsegv_error2();

	printf("已从问题函数中返回\n");
}

void sigsegv_error2(){
    int * p = NULL;
    *p = 1;
}

void sig_handler(int signo, siginfo_t * sig, void * context){
    pid_t cur_pid = getpid();
    ucontext_t * p = (ucontext_t *)context;    //获取异常现场指针
    long int fp = p->uc_mcontext.regs[29], lr = p->uc_mcontext.regs[30], sp = p->uc_mcontext.sp, ret_lr, ret_sp;		//获取栈回溯所需的值
    void * addr = NULL;							//存储栈回溯得到的函数地址

//    //设置返回地址和返回栈指针，便于跳过问题函数
//    if(sp != fp){
//    	ret_lr = lr;
//    	ret_sp = fp;
//    }else{
//
//    	asm(
//    		"ldr %0, [%2, #8]\n"
//    		"ldr %1, [%2]\n"
//    		:"=r"(ret_lr), "=r"(ret_sp)
//    		:"r"(fp)
//    		:
//    	);
//	}

    //建立函数名与地址的对应字典
	symtab_e symtab[NUM] = {
		{main, "main()"},
		{sigsegv_error1, "sigsegv_error1()"},
		{sig_handler, "sig_handler()"},
		{sigsegv_error2, "sigsegv_error2()"},
		{func1, "func1()"}
	};


    //输出异常时各寄存器的值
    printf("异常发生时程序现场如下:\n");

    for(int i = 0; i < 30; i++){
        printf("寄存器X%d的值为：0x%llx\n", i, p->uc_mcontext.regs[i]);
    }

    printf("寄存器LR的值为：0x%llx\n", p->uc_mcontext.regs[30]);
    printf("寄存器SP的值为：0x%llx\n", p->uc_mcontext.sp);
    printf("寄存器PC的值为：0x%llx\n", p->uc_mcontext.pc);
    printf("寄存器PSTATE的值为：0x%llx\n", p->uc_mcontext.pstate);
    printf("error address：0x%llx\n", p->uc_mcontext.fault_address);

    //栈回溯
    int count = 0;
    if(fp != sp){
    	while(fp != 0){
			long int fp_e = fp, lr_e = lr;				//备份，用于结果输出
			asm(								//根据函数栈帧的lr，得到该函数的入口地址
				"mov x1, %2\n"
				"sub x1, x1, #4\n"
				"ldr w2, [x1]\n"				//取得跳转入该函数的bl指令编码
				"and w2, w2,#0x03ffffff\n"
				"mov x3, #0\n"
				"lsl w3, w2, #2\n"				//由bl指令编码得到偏移地址
				"add %0, x1, x3\n"				//得到该函数地址
				"ldr %2, [%1, #8]\n"			//得到父函数的栈帧的fp和lr
				"ldr %1, [%1]\n"
				:"=r"(addr),"+r"(fp), "+r"(lr)
				:
				:"memory"
			);

			//输出该次函数调用的信息
			int i = 0;
			for(; i < NUM; i++){
				if(addr == symtab[i].fun_addr){
					printf("%d: %s: fp: 0x%lx  lr: 0x%lx  sp: 0x%lx\n", count, symtab[i].name, fp_e, lr_e, sp);
					count++;
					break;
				}
			}

			if(i == NUM && fp != 0){
				printf("%d: unknown: fp: 0x%lx  lr: 0x%lx  sp: 0x%lx\n", count, fp_e, lr_e, sp);
				count++;
			}else if(fp == 0) printf("%d: main(): fp: 0x%lx  lr: 0x%lx\n", count, fp_e, lr_e);

			sp = fp_e;
    	}

    }else{
    	count = 0;

    	while(*(long int *)fp != 0){
    		asm(
				"ldp %1, %2, [%1]\n"				//得到本函数的栈底和lr
				"mov x3, %2\n"
				"sub x3, x3, #4\n"
				"mov x4, #0\n"
				"ldr w4, [x3]\n"
				"and w4, w4, #0x03ffffff\n"
				"lsl w4, w4, #2\n"
				"add %0, x3, x4\n"

    			:"=r"(addr),"+r"(fp), "+r"(lr)
    			:
    			:
    		);

			//输出该次函数调用的信息
			int i = 0;
			for(; i < NUM; i++){
				if(addr == symtab[i].fun_addr){
					printf("%d: %s: fp: 0x%lx  lr: 0x%lx  sp: 0x%lx\n", count, symtab[i].name, fp, lr, sp);
					count++;
					break;
				}
			}

			if(i == NUM && *(long int *)fp != 0){
				printf("%d: unknown: fp: 0x%lx  lr: 0x%lx  sp: 0x%lx\n", count, fp, lr, sp);
				count++;
			}

			sp = fp;
    	}

    	//所有子函数的调用信息输出完毕,输出main
    	printf("%d: main(): fp: 0x%lx  lr: 0x%lx\n", count, fp, lr);

    }

    switch(signo){
        case 11:
            printf("进程[%d]在上述函数调用中出现了SIGSEGV错误，将跳过错误函数\n", cur_pid);
            exit(11);
            break;

        case 4:
            printf("进程[%d]在上述函数调用中出现了SIGILL错误，将跳过错误函数\n", cur_pid);
            exit(4);
            break;

        case 7:
            printf("进程[%d]在上述函数调用中出现了SIGBUS错误，将跳过错误函数\n", cur_pid);
            exit(7);
            break;
    }

//	asm(
//	"mov x1,%0\n"
//	"mov sp, %1\n"
//	"mov x29, %1\n"
//	"br x1\n"
//	:
//	:"r"(ret_lr), "r"(ret_sp)
//	:
//	);
}
