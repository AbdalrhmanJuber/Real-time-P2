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
            
            // Wait a shorter random amount of time before generating the next customer
            // Using shorter wait times to generate more customers during the simulation
            int wait_time = 1 + rand() % 3;  // 1-3 seconds between customers instead of using config params
            
            sleep(wait_time);
            
            // Generate multiple waves of customers
            if (customer_id >= 10) {
                // After generating 10 customers, wait a bit
                sleep(5);
                // Continue generating more customers
            }
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
    
    // Keep track of whether all requests were fulfilled
    bool all_requests_fulfilled = true;
    
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
            all_requests_fulfilled = false;
            break;
        }
        
        printf("Customer %d requested %d of product %d (subtype %d)\n", 
               id, request_msg.quantity, request_msg.product_type, request_msg.subtype);
        
        // Wait for response with timeout based on patience
        CustomerMsg response_msg;
        time_t start_time = time(NULL);
        bool got_response = false;
        
        while ((time(NULL) - start_time) < patience) {
            ssize_t recv_size = msgrcv(msg_queue_id, &response_msg, sizeof(CustomerMsg) - sizeof(long),
                                     id + MSG_CUSTOMER_RESPONSE_BASE, IPC_NOWAIT);
            
            if (recv_size != -1) {
                // Got a response for this specific request
                if (response_msg.product_type == request_msg.product_type &&
                    response_msg.subtype == request_msg.subtype) {
                    
                    got_response = true;
                    
                    // Check if the request was fulfilled
                    if (response_msg.fulfilled) {
                        printf("Customer %d received %d of product %d (subtype %d)\n",
                               id, request_msg.quantity, request_msg.product_type, request_msg.subtype);
                    } else {
                        printf("Customer %d could not get product %d (subtype %d)\n",
                               id, request_msg.product_type, request_msg.subtype);
                        all_requests_fulfilled = false;
                    }
                    break;
                }
            }
            
            // Wait a bit before checking again
            usleep(100000);  // 100ms
        }
        
        if (!got_response) {
            printf("Customer %d timed out waiting for product %d\n", 
                   id, request_msg.product_type);
            all_requests_fulfilled = false;
        }
        
        // Wait a short time between different item requests
        usleep(500000);  // 0.5 seconds
    }
    
    // Lock production status to update statistics
    struct sembuf prod_lock = {0, -1, 0};
    struct sembuf prod_unlock = {0, 1, 0};
    
    if (semop(prod_sem_id, &prod_lock, 1) == -1) {
        perror("Customer: Failed to lock production status semaphore");
        shmdt(status);
        exit(EXIT_FAILURE);
    }
    
    // Only mark customer as frustrated if not all requests were fulfilled
    if (!all_requests_fulfilled) {
        status->frustrated_customers++;
        
        // Decide if customer complains
        if ((double)rand() / RAND_MAX < config.complaint_probability) {
            // Send a complaint message
            request_msg.is_complaint = true;
            request_msg.msg_type = MSG_CUSTOMER_REQUEST;
            
            if (msgsnd(msg_queue_id, &request_msg, sizeof(request_msg) - sizeof(long), 0) == -1) {
                perror("Customer: Failed to send complaint message");
            } else {
                printf("Customer %d filed a complaint\n", id);
            }
        }
    }
    
    // Unlock production status
    if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
        perror("Customer: Failed to unlock production status semaphore");
    }
    
    printf("Customer %d leaving %s (PID: %d)\n", 
           id, all_requests_fulfilled ? "satisfied" : "frustrated", getpid());
    
    // Detach from shared memory
    shmdt(status);
}