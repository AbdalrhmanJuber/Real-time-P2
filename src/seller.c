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
    
    // Add some constraint logic to simulate limited inventory
    // For certain products, enforce stricter availability constraints
    if (type == PRODUCT_CAKE || type == PRODUCT_SWEET_PATISSERIE) {
        // These are specialty items - we want to maintain at least 3 in inventory
        if (available - quantity < 3) {
            return false; // Keep at least 3 items in reserve
        }
    }
    
    // For sandwich, require more inventory available (simulate high demand)
    if (type == PRODUCT_SANDWICH && available < quantity * 2) {
        return false; // Need twice as many sandwiches as requested
    }
    
    // Normal inventory check
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
        
        // Set failure in response
        request->fulfilled = false;
        // Removed the early return so we still send a response
    } else {
        // Product is available - fulfill the request
        request->fulfilled = true;
        
        // Update the production status
        status->sold_items[request->product_type] += request->quantity;
        
        // Calculate and update profit
        double price = config.product_prices[request->product_type];
        double sale_profit = price * request->quantity;
        status->total_profit += sale_profit;
        
        // Count this as a successful transaction
        (*customers_served)++;
        
        // Simulate the time it takes to package and hand over the product
        int service_time = 500 + (rand() % 1000);  // 0.5-1.5 seconds
        usleep(service_time * 1000);
        
        printf("Order for customer %d fulfilled\n", request->customer_id);
    }
    
    // Now we always send a response, whether the product was available or not
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
        
        if (customer_msg.is_complaint) {
            // Handle the complaint
            handle_customer_complaint(&customer_msg, status);
            
            // Unlock production status after handling complaint
            if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
                perror("Seller: Failed to unlock production status semaphore");
            }
        } else {
            // Handle the customer request
            handle_customer_request(&customer_msg, status, config, &customers_served);
            
            // Prepare response - use customer ID + response base for message type
            CustomerMsg response_msg = customer_msg;
            response_msg.msg_type = customer_msg.customer_id + MSG_CUSTOMER_RESPONSE_BASE;
            
            // Unlock production status before sending response
            if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
                perror("Seller: Failed to unlock production status semaphore");
            }
            
            // Send response back to customer
            if (msgsnd(customer_msgq_id, &response_msg, sizeof(CustomerMsg) - sizeof(long), 0) == -1) {
                perror("Seller: Failed to send response to customer");
            }
        }
        
        // We've already unlocked in both branches, so no need to unlock here
    }
    
    printf("Seller %d terminating, served %d customers (PID: %d)\n", 
           id, customers_served, getpid());
    
    // Detach from shared memory
    shmdt(status);
}