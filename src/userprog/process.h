#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <list.h>

/* Process identifier type. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)      /* Error value for pid_t. */

#ifdef VM
/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)   /* Error value for mapid_t. */
#endif

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
#ifdef VM
    struct list mmap_list;          /* List of memory mapped file. */
#endif
    struct process_info *info;      /* Process information for its parent. */
    int fd_next;                    /* File descriptor tracker. */
#ifdef VM
    mapid_t mapid_next;             /* Mapping identifier tracker. */ 
#endif
  };

/* An user process information for its parent process. */
struct process_info
  {
    pid_t pid;                      /* Process identifier. */
    struct process *process;        /* Process. */
    int status;                     /* Process status. */
    int exit_code;                  /* Exit code. */
    bool is_waiting;                /* Whether parent is waiting or not. */
    struct list_elem elem;          /* List element. */
  };

/* A file held by some process. */
struct process_file
  {
    int fd;                         /* File descriptor. */
    struct file *file;              /* Open file. */
    struct list_elem elem;          /* List element. */
  };

#ifdef VM
/* A memory mapped file information by some process. */
struct process_mmap
  {
    mapid_t id;                     /* Mapping identifier. */
    struct file *file;              /* Memory mapped file. */
    void *addr;                     /* Mapped address. */
    size_t size;                    /* File size. */
    struct list_elem elem;          /* List element. */
  };
#endif

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);
struct process *process_current (void);
struct process_info *process_find_child (pid_t);
struct file *process_get_file (int fd);
int process_set_file (struct file *);
#ifdef VM
struct process_mmap *process_get_mmap (mapid_t);
mapid_t process_set_mmap (struct file *, void *addr, size_t);
#endif

#endif /* userprog/process.h */
