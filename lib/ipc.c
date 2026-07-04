/*
 * ipc.c - Simple process-to-process message passing
 *
 * Provides fixed-size message queues for inter-process communication.
 * Each process gets a single queue identified by its PID.
 */

#include <shlte/types.h>
#include <shlte/ipc.h>
#include <shlte/process.h>
#include <shlte/printk.h>

#define MAX_IPC_QUEUES MAX_PROCESSES

static ipc_queue_t ipc_queues[MAX_IPC_QUEUES];
static int ipc_initialized = 0;

void ipc_init(void)
{
    for (int i = 0; i < MAX_IPC_QUEUES; i++) {
        ipc_queues[i].head = 0;
        ipc_queues[i].tail = 0;
        ipc_queues[i].count = 0;
    }
    ipc_initialized = 1;
    printk("[IPC] IPC subsystem initialized (%d queues, %d slots each)\n",
           MAX_IPC_QUEUES, IPC_QUEUE_SIZE);
}

int ipc_register_queue(uint64_t pid)
{
    if (pid >= MAX_IPC_QUEUES) return -1;
    ipc_queues[pid].head = 0;
    ipc_queues[pid].tail = 0;
    ipc_queues[pid].count = 0;
    return 0;
}

int ipc_send(uint64_t target_pid, ipc_message_t *msg)
{
    if (!ipc_initialized) return -1;
    if (target_pid >= MAX_IPC_QUEUES) return -1;

    ipc_queue_t *q = &ipc_queues[target_pid];

    if (q->count >= IPC_QUEUE_SIZE) {
        printk("[IPC] Queue full for pid %lu\n", target_pid);
        return -1;  /* Queue full */
    }

    int slot = q->tail;
    q->msgs[slot] = *msg;
    q->tail = (q->tail + 1) % IPC_QUEUE_SIZE;
    q->count++;

    return 0;
}

int ipc_recv(uint64_t *sender, ipc_message_t *msg)
{
    if (!ipc_initialized) return -1;
    uint64_t my_pid = 0;

    /* Get current pid from the global current_process */
    if (current_process) {
        my_pid = current_process->pid;
    } else {
        return -1;
    }

    ipc_queue_t *q = &ipc_queues[my_pid];

    if (q->count <= 0) return -1;  /* No messages */

    int slot = q->head;
    *msg = q->msgs[slot];
    if (sender) *sender = msg->sender_pid;
    q->head = (q->head + 1) % IPC_QUEUE_SIZE;
    q->count--;

    return 0;
}
