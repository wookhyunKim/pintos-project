#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <stddef.h>
typedef int pid_t;
#define PID_ERROR ((pid_t) - 1)

void syscall_init (void);



void check_address(void *addr);
void halt_syscall();
void exit_syscall(int status);
pid_t fork_syscall(const char *thread_name);
int exec_syscall(const char *cmd_line);
int wait_syscall(pid_t tid);
bool create_syscall(const char *file, unsigned initial_size);
bool remove_syscall(const char *file);
int open_syscall(const char *file);
int filesize_syscall(int fd);
int read_syscall(int fd, void *buffer, unsigned length);
int write_syscall(int fd, const void *buffer, unsigned length);
void seek_syscall(int fd, unsigned position);
int tell_syscall(int fd);
void close_syscall(int fd);
/** #Project 2: System Call */
extern struct lock filesys_lock;  // 파일 읽기/쓰기 용 lock
#endif /* userprog/syscall.h */

