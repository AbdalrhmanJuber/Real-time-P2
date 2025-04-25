#ifndef BAKERY_CUSTOMER_H
#define BAKERY_CUSTOMER_H

#include "common.h"

// Customer data structure
typedef struct {
    int id;
    pid_t pid;
    time_t arrival_time;
    int patience_seconds;
    bool is_satisfied;
    bool made_purchase;
} Customer;

// Function prototypes
void customer_process(int id, int customer_msgq_id, int prod_status_shm_id, 
                     int prod_sem_id, BakeryConfig config);
void generate_customer_request(CustomerMsg *msg, int customer_id, BakeryConfig config);
void handle_timeout(int sig);
void simulate_customer_behavior(int customer_msgq_id, BakeryConfig config, int customer_id);
void customer_generator(int customer_msgq_id, int prod_status_shm_id, 
                       int prod_sem_id, BakeryConfig config);

#endif // BAKERY_CUSTOMER_H