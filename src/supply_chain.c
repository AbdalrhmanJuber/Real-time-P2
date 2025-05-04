#include "../include/supply_chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Initialize inventory with starting values
void initialize_inventory(Inventory *inventory, BakeryConfig config) {
    // Set initial quantities
    inventory->quantities[ITEM_WHEAT] = config.min_purchases[ITEM_WHEAT] * 2;
    inventory->quantities[ITEM_YEAST] = config.min_purchases[ITEM_YEAST] * 2;
    inventory->quantities[ITEM_BUTTER] = config.min_purchases[ITEM_BUTTER] * 2;
    inventory->quantities[ITEM_MILK] = config.min_purchases[ITEM_MILK] * 2;
    inventory->quantities[ITEM_SUGAR_SALT] = config.min_purchases[ITEM_SUGAR_SALT] * 2;
    inventory->quantities[ITEM_SWEET_ITEMS] = config.min_purchases[ITEM_SWEET_ITEMS] * 2;
    inventory->quantities[ITEM_CHEESE_SALAMI] = config.min_purchases[ITEM_CHEESE_SALAMI] * 2;
    
    // Set minimum thresholds (at what point we need to restock)
    inventory->min_thresholds[ITEM_WHEAT] = config.min_purchases[ITEM_WHEAT] / 2;
    inventory->min_thresholds[ITEM_YEAST] = config.min_purchases[ITEM_YEAST] / 2;
    inventory->min_thresholds[ITEM_BUTTER] = config.min_purchases[ITEM_BUTTER] / 2;
    inventory->min_thresholds[ITEM_MILK] = config.min_purchases[ITEM_MILK] / 2;
    inventory->min_thresholds[ITEM_SUGAR_SALT] = config.min_purchases[ITEM_SUGAR_SALT] / 2;
    inventory->min_thresholds[ITEM_SWEET_ITEMS] = config.min_purchases[ITEM_SWEET_ITEMS] / 2;
    inventory->min_thresholds[ITEM_CHEESE_SALAMI] = config.min_purchases[ITEM_CHEESE_SALAMI] / 2;
    
    printf("Inventory initialized with starting quantities\n");
}

// Supply chain employee process
void supply_chain_process(int id, int inventory_shm_id, int prod_status_shm_id,
                        int inventory_sem_id, int management_msgq_id, BakeryConfig config) {
    
    // Attach to shared memory segments
    Inventory *inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (inventory == (void *) -1 || status == (void *) -1) {
        perror("Supply Chain: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Define semaphore operations
    struct sembuf inventory_lock = {0, -1, 0};   // Lock inventory
    struct sembuf inventory_unlock = {0, 1, 0};  // Unlock inventory
    
    printf("Supply chain employee %d started (PID: %d)\n", id, getpid());
    
    // Message structure for supply chain updates
    struct {
        long msg_type;
        RawMaterialType material;
    } supply_msg;
    
    // Main processing loop
    while (status->simulation_active) {
        bool reordered = false;
        
        // Lock inventory to check levels
        if (semop(inventory_sem_id, &inventory_lock, 1) == -1) {
            perror("Supply Chain: Failed to lock inventory semaphore");
            break;
        }
        
        // Check inventory levels and reorder if necessary
        for (int i = 0; i < ITEM_RAW_MATERIAL_COUNT; i++) {
            if (inventory->quantities[i] < inventory->min_thresholds[i]) {
                // Reorder materials
                int order_amount = config.min_purchases[i] + 
                                  rand() % (config.max_purchases[i] - config.min_purchases[i] + 1);
                
                inventory->quantities[i] += order_amount;
                
                printf("Supply chain employee %d ordered %d of item type %d\n", id, order_amount, i);
                
                reordered = true;
                
                // Sleep briefly to simulate the reordering process
                usleep(500000);  // 0.5 seconds
            }
        }
        
        // Unlock inventory
        if (semop(inventory_sem_id, &inventory_unlock, 1) == -1) {
            perror("Supply Chain: Failed to unlock inventory semaphore");
            break;
        }
        
        if (!reordered) {
            // If no reordering was done, sleep for a while before checking again
            sleep(5);
        }
    }
    
    printf("Supply chain employee %d terminating (PID: %d)\n", id, getpid());
    
    // Detach from shared memory
    shmdt(inventory);
    shmdt(status);
}