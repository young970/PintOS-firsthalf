#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* Parsing Function 임시 코드 - 김채욱 */
int parsing_str(char *file_name, char* argv[]);
void argument_stack(char **argv, int count, struct intr_frame* if_);

int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
struct thread* get_child_process(int pid);

struct lock file_lock;

#endif /* userprog/process.h */
