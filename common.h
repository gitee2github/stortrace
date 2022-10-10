<<<<<<< HEAD
/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2020 Andrii Nakryiko */
#ifndef __COMMON_H
#define __COMMON_H

struct trace_entry {
	short unsigned int type;
	unsigned char flags;
	unsigned char preempt_count;
	int pid;
};

/* sched_process_exec tracepoint context */
struct trace_event_raw_sched_process_exec {
	struct trace_entry ent;
	unsigned int __data_loc_filename;
	int pid;
	int old_pid;
	char __data[0];
};

#define TASK_COMM_LEN 16
#define MAX_FILENAME_LEN 512

/* definition of a sample sent to user-space from BPF program */
struct event {
	int pid;
	char comm[TASK_COMM_LEN];
	char filename[MAX_FILENAME_LEN];
};

#endif /* __COMMON_H */
=======
// Setup Argument stuff
#pragma once
#ifndef _COMMON_H_
#define _COMMON_H_
#define TASK_COMM_LEN 16
#define TEST_STACK_DEPTH 10
typedef long long unsigned int time_stramp;
#endif
>>>>>>> 6cd684f (finish dio and blk_trace,still bug)
