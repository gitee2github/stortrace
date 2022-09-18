#pragma once
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
static inline int if_target_pid(int tpid){ 
	size_t pid_tgid = bpf_get_current_pid_tgid(); 
	pid_t pid = pid_tgid >> 32; 
	if(tpid != 0){ 
		const struct task_struct *task = (struct task_struct *)bpf_get_current_task(); 
		int ppid = BPF_CORE_READ(task,real_parent,tgid); 
		if(ppid != tpid && pid != tpid){ return -1;} 
    } 
	return 0;} 

#define CHECK_TPID(tpid) if(if_target_pid(tpid) != 0){ return -1;}

static inline int my_strncmp(char* src,size_t n,char* target){
    for(int i = 0;i<n;i++){
        if(src[i] != target[i]){
            return -1;
        }
    }
    return 0;
}

static inline int if_target_common(char* tcommon){
	char common[16];
	__builtin_memset(common,0,16);
	if(bpf_get_current_comm(common,16) == 0){
		//success get common
		if(my_strncmp(tcommon,16,common) == 0){
			return 0;
		}
	}
	return -1;
}

#define CHECK_COMMON(tcommon) if(if_target_common(tcommon) != 0){return -1;}


void my_memset_zero(void* ptr,size_t size){
	char* p = (char*)ptr;
	for(unsigned int i = 0;i < size; i++){
		p[i] = 0;
	}
	return;
}

void my_memcopy(void* dst,void* src, size_t size){
	char* to = (char*)dst;
	char* from = (char*)src;
	for(unsigned int i = 0;i < size; i++){
		to[i] = from[i];
		i++;
	}
	return;
}