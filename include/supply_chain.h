#ifndef BAKERY_SUPPLY_CHAIN_H
#define BAKERY_SUPPLY_CHAIN_H

#include "common.h"

// Supply chain employee data
typedef struct {
    int id;
    pid_t pid;
    RawMaterialType specialization;  // The material this employee is responsible for
} SupplyChainEmployee;

// Function prototypes
void supply_chain_process(int id, int inventory_shm_id, int prod_status_shm_id,
                    int inventory_sem_id, int management_msgq_id, BakeryConfig config);
void purchase_materials(Inventory *inventory, BakeryConfig config, int employee_id);
void check_inventory_levels(Inventory *inventory, int management_msgq_id);
void initialize_inventory(Inventory *inventory, BakeryConfig config);

#endif // BAKERY_SUPPLY_CHAIN_H