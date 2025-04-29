#include "../include/baker.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Baker process main function
void baker_process(BakerType type, int id, int inventory_shm_id, int prod_status_shm_id,
                  int inventory_sem_id, int prod_sem_id, BakeryConfig config) {
    
    // Attach to shared memory segments
    Inventory *inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (inventory == (void *) -1 || status == (void *) -1) {
        perror("Baker: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Define semaphore operations
    struct sembuf inventory_lock = {0, -1, 0};   // Lock inventory
    struct sembuf inventory_unlock = {0, 1, 0};  // Unlock inventory
    struct sembuf prod_lock = {0, -1, 0};        // Lock production status
    struct sembuf prod_unlock = {0, 1, 0};       // Unlock production status
    
    // Baker type string for logging
    const char *baker_types[] = {
        "Cake and Sweet", "Patisserie", "Bread"
    };
    
    printf("Baker %d of type %s started (PID: %d)\n", id, baker_types[type], getpid());
    
    // Main processing loop
    while (status->simulation_active) {
        bool baked_something = false;
        
        // Lock production status to check and update
        if (semop(prod_sem_id, &prod_lock, 1) == -1) {
            perror("Baker: Failed to lock production status semaphore");
            break;
        }
        
        // Try to bake products based on baker type
        baked_something = bake_products(type, status, config);
        
        // Unlock production status
        if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
            perror("Baker: Failed to unlock production status semaphore");
            break;
        }
        
        if (!baked_something) {
            // No products to bake, sleep for a while before trying again
            sleep(2);
        } else {
            // Sleep to simulate baking time
            int sleep_time = 0;
            
            switch (type) {
                case BAKER_CAKE_SWEET:
                    // Baking cakes takes longer than sweets
                    sleep_time = (rand() % 2 == 0) ? 
                                config.production_times[PRODUCT_CAKE] / 2 : // Cake
                                config.production_times[PRODUCT_SWEET] / 2;  // Sweet
                    break;
                case BAKER_PATISSERIE:
                    sleep_time = config.production_times[PRODUCT_SWEET_PATISSERIE] / 2;
                    break;
                case BAKER_BREAD:
                    sleep_time = config.production_times[PRODUCT_BREAD] / 2;
                    break;
            }
            
            // Sleep for a random portion of the baking time to add variability
            int actual_sleep = (int)(sleep_time * (0.8 + (rand() % 40) / 100.0));
            usleep(actual_sleep * 1000);  // Convert to microseconds
        }
    }
    
    printf("Baker %d of type %s terminating (PID: %d)\n", id, baker_types[type], getpid());
    
    // Detach from shared memory
    shmdt(inventory);
    shmdt(status);
}

// Bake products based on baker type
bool bake_products(BakerType type, ProductionStatus *status, BakeryConfig config) {
    bool baked_something = false;
    
    switch (type) {
        case BAKER_CAKE_SWEET:
            // Check if we should bake more cakes (limit to 100)
            if (status->produced_items[PRODUCT_CAKE] < config.max_items_per_type[PRODUCT_CAKE]) {
                // Simulate baking a cake
                status->produced_items[PRODUCT_CAKE]++;
                printf("Baker baked a cake. Total: %d\n", status->produced_items[PRODUCT_CAKE]);
                baked_something = true;
            } 
            else if (status->produced_items[PRODUCT_SWEET] < config.max_items_per_type[PRODUCT_SWEET]) {
                // Simulate baking sweets
                status->produced_items[PRODUCT_SWEET]++;
                printf("Baker baked sweets. Total: %d\n", status->produced_items[PRODUCT_SWEET]);
                baked_something = true;
            }
            break;
            
        case BAKER_PATISSERIE:
            // Check if we should bake more patisseries
            if (status->produced_items[PRODUCT_SWEET_PATISSERIE] < 
                config.max_items_per_type[PRODUCT_SWEET_PATISSERIE]) {
                // Simulate baking sweet patisserie
                status->produced_items[PRODUCT_SWEET_PATISSERIE]++;
                printf("Baker baked a sweet patisserie. Total: %d\n", 
                       status->produced_items[PRODUCT_SWEET_PATISSERIE]);
                baked_something = true;
            }
            else if (status->produced_items[PRODUCT_SAVORY_PATISSERIE] < 
                     config.max_items_per_type[PRODUCT_SAVORY_PATISSERIE]) {
                // Simulate baking savory patisserie
                status->produced_items[PRODUCT_SAVORY_PATISSERIE]++;
                printf("Baker baked a savory patisserie. Total: %d\n", 
                       status->produced_items[PRODUCT_SAVORY_PATISSERIE]);
                baked_something = true;
            }
            break;
            
        case BAKER_BREAD:
            // Check if we should bake more bread
            if (status->produced_items[PRODUCT_BREAD] < config.max_items_per_type[PRODUCT_BREAD]) {
                // Simulate baking bread
                status->produced_items[PRODUCT_BREAD]++;
                printf("Baker baked bread. Total: %d\n", status->produced_items[PRODUCT_BREAD]);
                baked_something = true;
            }
            
            // Also handle sandwich production if bread baker is responsible
            if (status->produced_items[PRODUCT_SANDWICH] < config.max_items_per_type[PRODUCT_SANDWICH]) {
                // Simulate making sandwich
                status->produced_items[PRODUCT_SANDWICH]++;
                printf("Baker made sandwich. Total: %d\n", status->produced_items[PRODUCT_SANDWICH]);
                baked_something = true;
            }
            break;
    }
    
    return baked_something;
}