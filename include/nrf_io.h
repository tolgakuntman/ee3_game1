#ifndef IO_THREAD_H
#define IO_THREAD_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mirf.h"

#define IO_QUEUE_SIZE 10
#define MESSAGE_TYPE_SIZE 8   // Fixed size for message type
#define PAYLOAD_SIZE 24       // Remaining bytes for actual data
#define MAX_PAYLOAD_SIZE 32

typedef struct {
    uint8_t slave_id;               // ID of the slave sending the message
    char message_type[MESSAGE_TYPE_SIZE];  // First 8 bytes: message type as a string
    uint8_t payload[PAYLOAD_SIZE];  // Last 24 bytes: actual payload
    uint8_t payload_length;         // Length of the payload
} io_message_t;

// FreeRTOS Queue and Semaphore handles
extern QueueHandle_t io_send_queue;
extern QueueHandle_t io_receive_queue;
extern SemaphoreHandle_t io_receive_semaphore;
extern SemaphoreHandle_t io_send_semaphore;

// Initializes the IO thread tasks and queues
void io_thread_init(void);

// Enqueues a message for sending
bool io_enqueue_send(io_message_t *msg);

// Dequeues a received message
bool io_dequeue_receive(io_message_t *msg);

// FreeRTOS tasks for send/receive handling
void io_send_task(void *pvParameters);
void io_receive_task(void *pvParameters);

#endif // IO_THREAD_H
