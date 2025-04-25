#ifndef BAKERY_MANAGEMENT_H
#define BAKERY_MANAGEMENT_H

#include "common.h"
#include "chef.h"

// Management data structure
typedef struct {
    time_t last_decision_time;
    int decision_count;
} ManagementData;

// Function prototypes
void management_process(int inventory_shm_id, int prod_status_shm_id, 
                      int management_msgq_id, int customer_msgq_id,
                      int inventory_sem_id, int prod_sem_id, BakeryConfig config);
void analyze_production_needs(ProductionStatus *status, ChefTeam *teams, ManagementMsg *msg);
void check_end_conditions(ProductionStatus *status, BakeryConfig config, bool *should_end);
void reassign_chefs(ChefTeam *teams, ManagementMsg *decision);
void notify_all_processes(int customer_msgq_id, int management_msgq_id);

#endif // BAKERY_MANAGEMENT_H