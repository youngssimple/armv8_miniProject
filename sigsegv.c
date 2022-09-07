#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ucontext.h>
#include <elf.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#define SIZE 25

void sigsegv_error1(int);
void sigsegv_error2();
void sig_handler(int signo, siginfo_t * sig, void * context);
void func1();
void get_elf_head();
void get_section_head();
void get_tab();

// 用于读取elf的，解析symtab的变量
int fd = -1;	//读取本elf文件的文件描述符

int symtab_ind = -1, strtab_ind = -1;		//记录在段表中的下标
Elf64_Ehdr elfhd;			//拷贝elf文件头
Elf64_Shdr * section_header_ptr = NULL;	//指向elf段表
Elf64_Sym * symtab_ptr = NULL;		//用于存储解析函数明所需要的各节内容
char * strtab_ptr = NULL;
unsigned int sym_num = 0;

int main(int argc, char ** argv){

    pid_t cpid;
	//安装信号处理器
    struct sigaction act;
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGILL, &act, NULL);

	fd = open(argv[0], O_RDONLY);
	
	//触发sigsegv

	func1();
		
	printf("已从问题函数中返回\n");
		
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
    long int fp = p->uc_mcontext.regs[29], lr = p->uc_mcontext.regs[30], \
	 sp = p->uc_mcontext.sp, pc = p->uc_mcontext.pc, ret_lr, ret_sp;		//获取栈回溯所需的值
    long int addr = 0;							//存储栈回溯得到的函数地址
    
    // //设置返回地址和返回栈指针，便于跳过问题函数
    // if(sp != fp){
    // 	ret_lr = lr;
    // 	ret_sp = fp;
    // }else{
    
    // 	asm(
    // 		"ldr %0, [%2, #8]\n"
    // 		"ldr %1, [%2]\n"
    // 		:"=r"(ret_lr), "=r"(ret_sp)
    // 		:"r"(fp)
    // 		:
    // 	);
	// }
    
	

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
    
    get_elf_head();
	get_section_head();
	get_tab();

	int main_ind = 0;
	for(int j = 0; j < sym_num; j++){
		if(strcmp(&strtab_ptr[symtab_ptr[j].st_name], "main") == 0){
			main_ind = j;	
			break;
		}
	}
	long int base = (long int)main - symtab_ptr[main_ind].st_value; 

	printf("*****************backTrace***********************\n");
	printf("调用次序\t函数地址\t函数名称\t偏移\n");
    //栈回溯
    int count = 0;
    if(fp != sp){
    	while(fp != 0){
			char func[SIZE];		//用于暂存函数名
			long int fp_e = fp,  lr_e = lr, offset; 				//备份，用于结果输出
			
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

			offset = pc - addr;
			pc = lr_e - 4;
			
			int i = 0;
			for(; i < sym_num; i++){
				if(symtab_ptr[i].st_info & 0xf == 2 && symtab_ptr[i].st_value == addr){
						strncpy(func, &strtab_ptr[symtab_ptr[i].st_name], SIZE - 1);
						printf("%d\t%s(%lx): fp: %lx sp: %lx\n", count, func, offset, fp_e, sp);

 						break;
				}

			}
			if(i >= sym_num) printf("%d unknown\n", count);
			
			++count;
			sp = fp_e;
		}
	}else{
		char func[SIZE];		//用于暂存函数名
		long int  offset; 

    	while(*(long int *)fp != 0){    		
			//输出该次函数调用的信息
			int i = 0;
			for(; i < sym_num; i++){
				if((base + symtab_ptr[i].st_value) <= pc && pc <= (base + symtab_ptr[i].st_value + symtab_ptr[i].st_size)){
						strncpy(func, &strtab_ptr[symtab_ptr[i].st_name], SIZE - 1);
						offset = pc - symtab_ptr[i].st_value - base;
						
						printf("%d:\t\t(0x%lx)\t%s\t(+0x%lx)\n", count,(base + symtab_ptr[i].st_value), func, offset);
 						break;
				}

			}
			if(i >= sym_num) 
				printf("%d unknown\n", count);
				
			asm(
				"ldp %0, %1, [%0]"
				:"+r"(fp), "=r"(lr)
				:
				:
			);
										
			printf("fp: %lx sp: %lx\n", fp, sp);

			pc = lr - 4;
			
			++count;
			sp = fp;
		}
	}
    
    switch(signo){
        case 11:
            printf("进程[%d]在上述函数调用中出现了SIGSEGV错误，将退出进程\n", cur_pid);
            exit(11);
            break;

        case 4:
            printf("进程[%d]在上述函数调用中出现了SIGILL错误，将退出进程\n", cur_pid);
            exit(4);
            break;

        case 7:
            printf("进程[%d]在上述函数调用中出现了SIGBUS错误，将退出进程\n", cur_pid);
            exit(7);
            break;
    }
    
	exit(0);
}

void get_elf_head(){
	read(fd, elfhd.e_ident, EI_NIDENT);
	read(fd, &elfhd.e_type, sizeof(elfhd.e_type));
	read(fd, &elfhd.e_machine, sizeof(elfhd.e_machine));
	read(fd, &elfhd.e_version, sizeof(elfhd.e_version));
	read(fd, &elfhd.e_entry, sizeof(elfhd.e_entry));
	read(fd, &elfhd.e_phoff, sizeof(elfhd.e_phoff));
	read(fd, &elfhd.e_shoff, sizeof(elfhd.e_shoff));
	read(fd, &elfhd.e_flags, sizeof(elfhd.e_flags));
	read(fd, &elfhd.e_ehsize, sizeof(elfhd.e_ehsize));
	read(fd, &elfhd.e_phentsize, sizeof(elfhd.e_phentsize));
	read(fd, &elfhd.e_phnum, sizeof(elfhd.e_phnum));
	read(fd, &elfhd.e_shentsize, sizeof(elfhd.e_shentsize));
	read(fd, &elfhd.e_shnum, sizeof(elfhd.e_shnum));
	read(fd, &elfhd.e_shstrndx, sizeof(elfhd.e_shstrndx));
}

void get_section_head(){
	section_header_ptr = (Elf64_Shdr *)calloc(elfhd.e_shnum, sizeof(Elf64_Shdr));

	lseek(fd, elfhd.e_shoff, SEEK_SET);

	read (fd, section_header_ptr, elfhd.e_shnum * sizeof(Elf64_Shdr));
}

void get_tab(){
	int shstr_ind = elfhd.e_shstrndx;
	char shstr_ptr[section_header_ptr[shstr_ind].sh_size];

	lseek(fd, section_header_ptr[shstr_ind].sh_offset, SEEK_SET);

	read(fd, shstr_ptr, section_header_ptr[shstr_ind].sh_size);

	for(int i = 0; i < elfhd.e_shnum; i++){
		if(strcmp(&shstr_ptr[section_header_ptr[i].sh_name], ".symtab") == 0) 
			symtab_ind = i;

		if(strcmp(&shstr_ptr[section_header_ptr[i].sh_name], ".strtab") == 0) 
			strtab_ind = i;
	} 

	sym_num = section_header_ptr[symtab_ind].sh_size / sizeof(Elf64_Sym);
	
//获取symtab
	symtab_ptr = calloc(sym_num, sizeof(Elf64_Sym));
	lseek(fd, section_header_ptr[symtab_ind].sh_offset, SEEK_SET);
	read(fd, symtab_ptr, section_header_ptr[symtab_ind].sh_size);
	
//获取strtab
	strtab_ptr = (char *)malloc(section_header_ptr[strtab_ind].sh_size);
	lseek(fd, section_header_ptr[strtab_ind].sh_offset, SEEK_SET);
	read(fd, strtab_ptr, section_header_ptr[strtab_ind].sh_size);
	
}


