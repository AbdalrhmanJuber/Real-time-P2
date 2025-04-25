#ifndef BAKERY_SELLER_H
#define BAKERY_SELLER_H

#include "common.h"

// Seller process data
typedef struct {
    int id;
    pid_t pid;
    bool active;
    int customers_served;
} Seller;

// Function prototypes
void seller_process(int id, int customer_msgq_id, int prod_status_shm_id, 
                    int prod_sem_id, BakeryConfig config);
void handle_customer_request(CustomerMsg *request, ProductionStatus *status, 
                           BakeryConfig config, int *customers_served);
void handle_customer_complaint(CustomerMsg *complaint, ProductionStatus *status);
bool check_product_availability(ProductType type, int subtype, int quantity, 
                              ProductionStatus *status);

#endif // BAKERY_SELLER_H