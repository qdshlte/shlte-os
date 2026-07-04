#ifndef SHLTE_IPC_H
#define SHLTE_IPC_H

#include <shlte/types.h>

#define MAX_MSG_SIZE    64
#define IPC_QUEUE_SIZE  16

typedef struct {
    uint64_t sender_pid;
    uint64_t receiver_pid;
    uint8_t  type;
    uint8_t  data[MAX_MSG_SIZE];
    uint32_t data_len;
} ipc_message_t;

typedef struct {
    ipc_message_t msgs[IPC_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
    volatile int count;
} ipc_queue_t;

/* Initialize IPC subsystem */
void ipc_init(void);

/* Send a message to a process */
int ipc_send(uint64_t target_pid, ipc_message_t *msg);

/* Receive a message (non-blocking) */
int ipc_recv(uint64_t *sender, ipc_message_t *msg);

/* Register a process's IPC queue */
int ipc_register_queue(uint64_t pid);

#endif
