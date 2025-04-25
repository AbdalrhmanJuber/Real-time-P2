#include "../include/customer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Customer generator process
void customer_generator(int msg_queue_id, int prod_status_shm_id, 
                      int prod_sem_id, BakeryConfig config) {
    
    // Attach to shared memory
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (status == (void *) -1) {
        perror("Customer Generator: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    printf("Customer generator process started (PID: %d)\n", getpid());
    
    // Customer process counter
    int customer_id = 0;
    
    // Main loop
    while (status->simulation_active) {
        // Generate a new customer
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("Failed to fork customer process");
            break;
        } else if (pid == 0) {
            // Child process (customer)
            customer_process(customer_id, msg_queue_id, prod_status_shm_id, prod_sem_id, config);
            exit(EXIT_SUCCESS);  // Should not reach here
        } else {
            // Parent process (customer generator)
            printf("Generated customer %d with PID %d\n", customer_id, pid);
            customer_id++;
            
            // Wait a random amount of time before generating the next customer
            int wait_time = config.customer_params[0] + 
                           rand() % (config.customer_params[1] - config.customer_params[0] + 1);
            
            sleep(wait_time);
        }
    }
    
    printf("Customer generator process terminating (PID: %d)\n", getpid());
    
    // Detach from shared memory
    shmdt(status);
}

// Individual customer process
void customer_process(int id, int msg_queue_id, int prod_status_shm_id, int prod_sem_id, BakeryConfig config) {
    // Attach to shared memory
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (status == (void *) -1) {
        perror("Customer: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Configure customer patience (how long they'll wait for service)
    int patience = config.customer_params[2] + 
                  rand() % (config.customer_params[3] - config.customer_params[2] + 1);
    
    printf("Customer %d arrived with patience %d seconds (PID: %d)\n", id, patience, getpid());
    
    // Decide what products to request
    int num_items = 1 + rand() % config.max_purchase_items;
    CustomerMsg request_msg;
    
    request_msg.msg_type = MSG_CUSTOMER_REQUEST;
    request_msg.customer_id = id;
    request_msg.is_complaint = false;
    
    // Send the request to a seller
    for (int i = 0; i < num_items; i++) {
        // Pick a random product
        request_msg.product_type = rand() % PRODUCT_TYPE_COUNT;
        
        // Select a subtype (flavor, variety) if applicable
        if (request_msg.product_type < PRODUCT_TYPE_COUNT && 
            config.num_categories[request_msg.product_type] > 0) {
            request_msg.subtype = rand() % config.num_categories[request_msg.product_type];
        } else {
            request_msg.subtype = 0;
        }
        
        // Request 1-3 of the item
        request_msg.quantity = 1 + rand() % 3;
        
        // Send the request to the message queue
        if (msgsnd(msg_queue_id, &request_msg, sizeof(request_msg) - sizeof(long), 0) == -1) {
            perror("Customer: Failed to send message to queue");
            break;
        }
        
        printf("Customer %d requested %d of product %d (subtype %d)\n", 
               id, request_msg.quantity, request_msg.product_type, request_msg.subtype);
        
        // Wait a short time between different item requests
        usleep(500000);  // 0.5 seconds
    }
    
    // Wait for their patience time to be served
    sleep(patience);
    
    // Lock production status to update statistics
    struct sembuf prod_lock = {0, -1, 0};
    struct sembuf prod_unlock = {0, 1, 0};
    
    if (semop(prod_sem_id, &prod_lock, 1) == -1) {
        perror("Customer: Failed to lock production status semaphore");
        shmdt(status);
        exit(EXIT_FAILURE);
    }
    
    // Customer becomes frustrated if they're still waiting after their patience runs out
    status->frustrated_customers++;
    
    // Decide if customer complains
    if ((double)rand() / RAND_MAX < config.complaint_probability) {
        // Send a complaint message
        request_msg.is_complaint = true;
        
        if (msgsnd(msg_queue_id, &request_msg, sizeof(request_msg) - sizeof(long), 0) == -1) {
            perror("Customer: Failed to send complaint message");
        } else {
            printf("Customer %d filed a complaint\n", id);
            status->complained_customers++;
        }
    }
    
    // Unlock production status
    if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
        perror("Customer: Failed to unlock production status semaphore");
    }
    
    printf("Customer %d leaving after %d seconds (PID: %d)\n", id, patience, getpid());
    
    // Detach from shared memory
    shmdt(status);
}