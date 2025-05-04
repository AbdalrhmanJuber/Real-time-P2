#include "../include/management.h"
#include "../include/chef.h"
#include "../include/baker.h"
#include "../include/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <time.h>

// Management process
void management_process(int inventory_shm_id, int prod_status_shm_id,
                      int management_msgq_id, int customer_msgq_id,
                      int inventory_sem_id, int prod_sem_id, BakeryConfig config) {
    
    // Attach to shared memory segments
    Inventory *inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (inventory == (void *) -1 || status == (void *) -1) {
        perror("Management: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Define semaphore operations
    struct sembuf inventory_lock = {0, -1, 0};   // Lock inventory
    struct sembuf inventory_unlock = {0, 1, 0};  // Unlock inventory
    struct sembuf prod_lock = {0, -1, 0};        // Lock production status
    struct sembuf prod_unlock = {0, 1, 0};       // Unlock production status
    
    // Create chef teams data
    ChefTeam chef_teams[CHEF_TYPE_COUNT];
    initialize_chef_teams(chef_teams, config);
    
    // Management data
    ManagementData mgmt_data = {
        .last_decision_time = time(NULL),
        .decision_count = 0
    };
    
    // Message structure for supply chain updates or management decisions
    union {
        struct {
            long msg_type;
            RawMaterialType material;
        } supply_chain_msg;
        
        ManagementMsg mgmt_msg;
    } msg;
    
    printf("Management process started (PID: %d)\n", getpid());
    
    // Main processing loop
    while (status->simulation_active) {
        bool should_end = false;
        
        // Check if there are any supply chain messages
        ssize_t recv_size = msgrcv(management_msgq_id, &msg, sizeof(msg) - sizeof(long),
                                 MSG_SUPPLY_CHAIN_UPDATE, IPC_NOWAIT);
        
        if (recv_size != -1) {
            // We received a supply chain update
            printf("Management received supply chain update for material %d\n",
                   msg.supply_chain_msg.material);
        }
        
        // Check if we need to rebalance production
        time_t current_time = time(NULL);
        if (current_time - mgmt_data.last_decision_time >= 60) {  // Make decisions every minute
            // Lock production status
            if (semop(prod_sem_id, &prod_lock, 1) == -1) {
                perror("Management: Failed to lock production status semaphore");
                break;
            }
            
            // Analyze production needs
            ManagementMsg decision;
            analyze_production_needs(status, chef_teams, &decision);
            
            // Execute management decisions
            if (decision.num_chefs_to_move > 0) {
                reassign_chefs(chef_teams, &decision);
            }
            
            // Update management data
            mgmt_data.last_decision_time = current_time;
            mgmt_data.decision_count++;
            
            // Check end conditions
            check_end_conditions(status, config, &should_end);
            
            // Unlock production status
            if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
                perror("Management: Failed to unlock production status semaphore");
                break;
            }
        }
        
        // If end conditions are met, end the simulation
        if (should_end) {
            printf("Management detected simulation end condition\n");
            status->simulation_active = false;
            
            // Notify all processes to terminate
            notify_all_processes(customer_msgq_id, management_msgq_id);
            break;
        }
        
        // Sleep for a while before checking again
        sleep(5);  // Check every 5 seconds
    }
    
    // Print simulation summary
    printf("\n======== BAKERY SIMULATION SUMMARY ========\n");
    printf("Total profit: $%.2f\n", status->total_profit);
    printf("Simulation duration: %ld minutes\n", (time(NULL) - status->start_time) / 60);
    printf("Produced items:\n");
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        printf("  Type %d: %d\n", i, status->produced_items[i]);
    }
    printf("Sold items:\n");
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        printf("  Type %d: %d\n", i, status->sold_items[i]);
    }
    printf("Frustrated customers: %d\n", status->frustrated_customers);
    printf("Complained customers: %d\n", status->complained_customers);
    printf("Missing items requests: %d\n", status->missing_items_requests);
    printf("Management decisions: %d\n", mgmt_data.decision_count);
    printf("==========================================\n");
    
    printf("Management process terminating (PID: %d)\n", getpid());
    
    // Free allocated memory
    for (int i = 0; i < CHEF_TYPE_COUNT; i++) {
        free(chef_teams[i].chefs);
    }
    
    // Detach from shared memory
    shmdt(inventory);
    shmdt(status);
}

// Analyze production needs and make management decisions
void analyze_production_needs(ProductionStatus *status, ChefTeam *teams, ManagementMsg *msg) {
    // Default decision: no reallocation
    msg->msg_type = MSG_MANAGEMENT_DECISION;
    msg->chef_type_from = 0;
    msg->chef_type_to = 0;
    msg->num_chefs_to_move = 0;
    
    // Calculate production ratios for different product types
    float production_ratios[PRODUCT_TYPE_COUNT];
    int total_produced = 0;
    
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        total_produced += status->produced_items[i];
    }
    
    if (total_produced > 0) {
        for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
            production_ratios[i] = (float)status->produced_items[i] / total_produced;
        }
    } else {
        for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
            production_ratios[i] = 0.0f;
        }
    }
    
    // Calculate sales ratios for different product types
    float sales_ratios[PRODUCT_TYPE_COUNT];
    int total_sold = 0;
    
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        total_sold += status->sold_items[i];
    }
    
    if (total_sold > 0) {
        for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
            sales_ratios[i] = (float)status->sold_items[i] / total_sold;
        }
    } else {
        for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
            sales_ratios[i] = 0.0f;
        }
    }
    
    // Look for imbalances between production and sales
    float max_imbalance = -1.0f;
    int max_imbalance_type = -1;
    
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        float imbalance = sales_ratios[i] - production_ratios[i];
        if (imbalance > max_imbalance) {
            max_imbalance = imbalance;
            max_imbalance_type = i;
        }
    }
    
    // If there is a significant imbalance, try to reallocate chefs
    if (max_imbalance > 0.1f && max_imbalance_type >= 0) {
        // Find which chef team produces the type with highest imbalance
        ChefType team_to = 0;
        switch (max_imbalance_type) {
            case PRODUCT_BREAD:
                // Can't directly allocate chefs to bread (bakers do this)
                return;
            case PRODUCT_SANDWICH:
                team_to = CHEF_SANDWICH;
                break;
            case PRODUCT_CAKE:
                team_to = CHEF_CAKE;
                break;
            case PRODUCT_SWEET:
                team_to = CHEF_SWEET;
                break;
            case PRODUCT_SWEET_PATISSERIE:
                team_to = CHEF_SWEET_PATISSERIE;
                break;
            case PRODUCT_SAVORY_PATISSERIE:
                team_to = CHEF_SAVORY_PATISSERIE;
                break;
            default:
                return;
        }
        
        // Find which team has excess capacity (lowest imbalance)
        float min_imbalance = 1.0f;
        int min_imbalance_type = -1;
        
        for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
            if (i == PRODUCT_PASTE) continue;  // Skip paste (special case)
            
            float imbalance = production_ratios[i] - sales_ratios[i];
            if (imbalance > 0.05f && imbalance < min_imbalance) {
                min_imbalance = imbalance;
                min_imbalance_type = i;
            }
        }
        
        // Find which chef team produces the type with lowest imbalance
        ChefType team_from = 0;
        if (min_imbalance_type >= 0) {
            switch (min_imbalance_type) {
                case PRODUCT_BREAD:
                    // Not handled by chefs
                    return;
                case PRODUCT_SANDWICH:
                    team_from = CHEF_SANDWICH;
                    break;
                case PRODUCT_CAKE:
                    team_from = CHEF_CAKE;
                    break;
                case PRODUCT_SWEET:
                    team_from = CHEF_SWEET;
                    break;
                case PRODUCT_SWEET_PATISSERIE:
                    team_from = CHEF_SWEET_PATISSERIE;
                    break;
                case PRODUCT_SAVORY_PATISSERIE:
                    team_from = CHEF_SAVORY_PATISSERIE;
                    break;
                default:
                    return;
            }
            
            // Create management decision
            msg->chef_type_from = team_from;
            msg->chef_type_to = team_to;
            msg->num_chefs_to_move = 1;  // Move one chef at a time
            
            printf("Management decision: Moving %d chef(s) from team %d to team %d\n",
                   msg->num_chefs_to_move, msg->chef_type_from, msg->chef_type_to);
        }
    }
}

// Check end conditions for the simulation
void check_end_conditions(ProductionStatus *status, BakeryConfig config, bool *should_end) {
    *should_end = false;
    
    // Check thresholds
    if (status->frustrated_customers >= config.thresholds[0]) {
        printf("Simulation ending: Frustrated customer threshold reached (%d/%d)\n",
               status->frustrated_customers, config.thresholds[0]);
        *should_end = true;
    }
    else if (status->complained_customers >= config.thresholds[1]) {
        printf("Simulation ending: Complaint threshold reached (%d/%d)\n",
               status->complained_customers, config.thresholds[1]);
        *should_end = true;
    }
    else if (status->missing_items_requests >= config.thresholds[2]) {
        printf("Simulation ending: Missing items threshold reached (%d/%d)\n",
               status->missing_items_requests, config.thresholds[2]);
        *should_end = true;
    }
    else if (status->total_profit >= config.thresholds[3]) {
        printf("Simulation ending: Profit threshold reached ($%.2f/$%.2f)\n",
               status->total_profit, (double)config.thresholds[3]);
        *should_end = true;
    }
    else if ((time(NULL) - status->start_time) / 60 >= config.max_simulation_time) {
        printf("Simulation ending: Maximum time reached (%ld/%d minutes)\n",
               (time(NULL) - status->start_time) / 60, config.max_simulation_time);
        *should_end = true;
    }
}

// Execute chef reassignment decisions
void reassign_chefs(ChefTeam *teams, ManagementMsg *decision) {
    reallocate_chefs(teams, decision->chef_type_from, decision->chef_type_to, 
                   decision->num_chefs_to_move);
}

// Notify all processes that the simulation is ending
void notify_all_processes(int customer_msgq_id, int management_msgq_id) {
    // Send end message to customer message queue
    struct {
        long msg_type;
        int signal;
    } end_msg;
    
    end_msg.msg_type = MSG_SIMULATION_END;
    end_msg.signal = 1;
    
    if (msgsnd(customer_msgq_id, &end_msg, sizeof(int), 0) == -1) {
        perror("Management: Failed to send end message to customer queue");
    }
    
    // Send end message to management message queue
    if (msgsnd(management_msgq_id, &end_msg, sizeof(int), 0) == -1) {
        perror("Management: Failed to send end message to management queue");
    }
}