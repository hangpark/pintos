#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>

/* Process identifier type. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)      /* Error value for pid_t. */

/* Process status flags. */
#define PROCESS_LOADING 0           /* Process is loading. */
#define PROCESS_RUNNING 1           /* Process is running. */
#define PROCESS_FAIL 2              /* Process loading failed. */
#define PROCESS_EXIT 4              /* Process has exited. */

/* An user process. */
struct process
  {
    pid_t pid;                      /* Process identifier. */
    struct process *parent;         /* Parent process. */
    struct file *exec_file;         /* Process executable file. */
    struct list child_list;         /* List of child processes. */
    struct list file_list;          /* List of files in use. */
    struct list_elem elem;          /* List element. */

    int status;                     /* Process status. */
    int exit_code;                  /* Exit code. */
    bool is_waiting;                /* Whether parent is waiting or not. */
    int fd_next;                    /* File descriptor tracker. */
  };

/* A file held by some process. */
struct process_file
  {
    int fd;                         /* File descriptor. */
    struct file *file;              /* Open file. */
    struct list_elem elem;          /* List element. */
  };

pid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct process *process_current (void);
struct process *process_find_child (struct process *proc, pid_t pid);
struct file *process_get_file (int fd);
int process_set_file (struct file * file);

#endif /* userprog/process.h */
