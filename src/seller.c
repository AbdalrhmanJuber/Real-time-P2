#include "../include/seller.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Handle a customer complaint
void handle_customer_complaint(CustomerMsg *complaint, ProductionStatus *status) {
    printf("Processing complaint from customer %d about product %d\n", 
           complaint->customer_id, complaint->product_type);
    
    // Increment the complaints counter in the production status
    status->complained_customers++;
    
    // In a real system, we might:
    // 1. Log the complaint
    // 2. Issue a refund
    // 3. Update quality control metrics
    
    // Just simulate some delay for complaint handling
    int handling_time = 1000 + (rand() % 2000);  // 1-3 seconds
    usleep(handling_time * 1000);
    
    printf("Complaint from customer %d processed\n", complaint->customer_id);
}

// Check if a product is available
bool check_product_availability(ProductType type, int subtype, int quantity, 
                              ProductionStatus *status) {
    // Check if we have enough of this product type available
    int available = status->produced_items[type] - status->sold_items[type];
    
    // For now, we don't track subtypes in our inventory model
    // In a more complex system, we would check availability by subtype
    
    return (available >= quantity);
}

// Handle a customer request
void handle_customer_request(CustomerMsg *request, ProductionStatus *status, 
                           BakeryConfig config, int *customers_served) {
    
    printf("Processing request from customer %d for product %d (subtype %d)\n",
           request->customer_id, request->product_type, request->subtype);
    
    // Check if the requested product is available
    bool available = check_product_availability(request->product_type, 
                                             request->subtype,
                                             request->quantity, status);
    
    if (!available) {
        // Product not available
        printf("Product %d not available for customer %d\n", 
               request->product_type, request->customer_id);
        
        // Increment missing items counter
        status->missing_items_requests++;
        
        // Set failure in response (we'll handle this in the seller_process function)
        request->is_complaint = false;  // Not a complaint, just unavailable
        return;
    }
    
    // Product is available - update sold count
    status->sold_items[request->product_type] += request->quantity;
    
    // Calculate price
    float base_price = config.product_prices[request->product_type];
    float price = base_price * request->quantity;
    
    // Apply any variation based on subtype (could add premium for special subtypes)
    // Just a simple example - in a real system this would be more complex
    if (request->subtype > 0) {
        price *= (1.0 + (request->subtype * 0.1));  // 10% price increase per subtype level
    }
    
    // Update profit
    status->total_profit += price;
    
    // Increment customer count
    (*customers_served)++;
    
    printf("Sold %d units of product %d to customer %d for $%.2f\n", 
           request->quantity, request->product_type, request->customer_id, price);
}

// Seller process main function
void seller_process(int id, int customer_msgq_id, int prod_status_shm_id, 
                   int prod_sem_id, BakeryConfig config) {
    
    // Attach to shared memory segment
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (status == (void *) -1) {
        perror("Seller: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Define semaphore operations
    struct sembuf prod_lock = {0, -1, 0};
    struct sembuf prod_unlock = {0, 1, 0};
    
    // Track number of customers served
    int customers_served = 0;
    
    printf("Seller %d started (PID: %d)\n", id, getpid());
    
    // Main processing loop
    while (status->simulation_active) {
        // Message buffer for customer requests
        CustomerMsg customer_msg;
        
        // Try to receive a customer request message
        ssize_t msg_size = msgrcv(customer_msgq_id, &customer_msg, sizeof(CustomerMsg) - sizeof(long),
                                 MSG_CUSTOMER_REQUEST, IPC_NOWAIT);
        
        if (msg_size == -1) {
            // No message, wait a bit
            usleep(100000);  // 100ms
            continue;
        }
        
        // Lock production status to check availability
        if (semop(prod_sem_id, &prod_lock, 1) == -1) {
            perror("Seller: Failed to lock production status semaphore");
            continue;
        }
        
        // Handle the customer request
        handle_customer_request(&customer_msg, status, config, &customers_served);
        
        // Unlock production status
        if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
            perror("Seller: Failed to unlock production status semaphore");
        }
        
        // Send response back to customer
        customer_msg.msg_type = MSG_CUSTOMER_REQUEST;  // Reuse the same message type for simplicity
        if (msgsnd(customer_msgq_id, &customer_msg, sizeof(CustomerMsg) - sizeof(long), 0) == -1) {
            perror("Seller: Failed to send response to customer");
        }
        
        // Process any customer complaints (using the is_complaint field to identify complaints)
        msg_size = msgrcv(customer_msgq_id, &customer_msg, sizeof(CustomerMsg) - sizeof(long),
                         MSG_CUSTOMER_REQUEST, IPC_NOWAIT);
        
        if (msg_size != -1 && customer_msg.is_complaint) {
            // Lock production status for complaint processing
            if (semop(prod_sem_id, &prod_lock, 1) == -1) {
                perror("Seller: Failed to lock production status semaphore");
                continue;
            }
            
            // Handle the complaint
            handle_customer_complaint(&customer_msg, status);
            
            // Unlock production status
            if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
                perror("Seller: Failed to unlock production status semaphore");
            }
        }
    }
    
    printf("Seller %d terminating, served %d customers (PID: %d)\n", 
           id, customers_served, getpid());
    
    // Detach from shared memory
    shmdt(status);
}