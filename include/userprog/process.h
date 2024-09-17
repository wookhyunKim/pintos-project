#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);


#define STDIN  0x1
#define STDOUT 0x2
#define STDERR 0x3
// project 2 - userprog
void setup_argument(char **argv_list, int argc_num, struct intr_frame *if_);
int process_add_file(struct file *f);
struct file *get_file_from_fd(int fd);
int remove_file_in_fd_table(int fd);
struct thread * get_thread(tid_t child_tid);



// project 2 - userprog



#endif /* userprog/process.h */
