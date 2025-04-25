#include "../include/chef.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Check if chef has the necessary ingredients for production
bool check_dependencies(ChefType type, Inventory *inventory) {
    switch (type) {
        case CHEF_PASTE:
            // Paste requires wheat, yeast, butter, and water (milk)
            if (inventory->quantities[ITEM_WHEAT] < 2 || 
                inventory->quantities[ITEM_YEAST] < 1 || 
                inventory->quantities[ITEM_BUTTER] < 1 || 
                inventory->quantities[ITEM_MILK] < 1) {
                return false;
            }
            return true;
            
        case CHEF_CAKE:
            // Cakes require flour (wheat), butter, milk, sugar/salt, eggs (sweet items)
            if (inventory->quantities[ITEM_WHEAT] < 3 || 
                inventory->quantities[ITEM_BUTTER] < 2 || 
                inventory->quantities[ITEM_MILK] < 2 ||
                inventory->quantities[ITEM_SUGAR_SALT] < 2 ||
                inventory->quantities[ITEM_SWEET_ITEMS] < 2) {
                return false;
            }
            return true;
            
        case CHEF_SANDWICH:
            // Sandwiches require bread (check if available) and cheese/salami
            if (inventory->quantities[ITEM_CHEESE_SALAMI] < 2) {
                return false;
            }
            return true;
            
        case CHEF_SWEET:
            // Sweets require sugar/salt and sweet items
            if (inventory->quantities[ITEM_SUGAR_SALT] < 2 ||
                inventory->quantities[ITEM_SWEET_ITEMS] < 3) {
                return false;
            }
            return true;
            
        case CHEF_SWEET_PATISSERIE:
            // Sweet patisseries require paste, sweet items, and sugar
            // We'll check if PRODUCT_PASTE is available in produce_item
            if (inventory->quantities[ITEM_SWEET_ITEMS] < 2 ||
                inventory->quantities[ITEM_SUGAR_SALT] < 1) {
                return false;
            }
            return true;
            
        case CHEF_SAVORY_PATISSERIE:
            // Savory patisseries require paste, cheese/salami, and butter
            // We'll check if PRODUCT_PASTE is available in produce_item
            if (inventory->quantities[ITEM_CHEESE_SALAMI] < 1 ||
                inventory->quantities[ITEM_BUTTER] < 1) {
                return false;
            }
            return true;
            
        default:
            return false;
    }
}

// Produce an item (consume ingredients and update production status)
void produce_item(ChefType type, Inventory *inventory, ProductionStatus *status, BakeryConfig config) {
    // No need for semaphores here as locking/unlocking is handled by the calling function
    
    // Determine which product type to produce based on chef type
    ProductType product_type;
    switch (type) {
        case CHEF_PASTE:
            product_type = PRODUCT_PASTE;
            // Consume ingredients for paste
            inventory->quantities[ITEM_WHEAT] -= 2;
            inventory->quantities[ITEM_YEAST] -= 1;
            inventory->quantities[ITEM_BUTTER] -= 1;
            inventory->quantities[ITEM_MILK] -= 1;
            break;
            
        case CHEF_CAKE:
            product_type = PRODUCT_CAKE;
            // Consume ingredients for cake
            inventory->quantities[ITEM_WHEAT] -= 3;
            inventory->quantities[ITEM_BUTTER] -= 2;
            inventory->quantities[ITEM_MILK] -= 2;
            inventory->quantities[ITEM_SUGAR_SALT] -= 2;
            inventory->quantities[ITEM_SWEET_ITEMS] -= 2;
            break;
            
        case CHEF_SANDWICH:
            product_type = PRODUCT_SANDWICH;
            // Consume ingredients for sandwich
            inventory->quantities[ITEM_CHEESE_SALAMI] -= 2;
            // Note: bread consumption is handled by marking it as "sold" in the chef_process function
            break;
            
        case CHEF_SWEET:
            product_type = PRODUCT_SWEET;
            // Consume ingredients for sweet
            inventory->quantities[ITEM_SUGAR_SALT] -= 2;
            inventory->quantities[ITEM_SWEET_ITEMS] -= 3;
            break;
            
        case CHEF_SWEET_PATISSERIE:
            product_type = PRODUCT_SWEET_PATISSERIE;
            // Consume ingredients for sweet patisserie
            inventory->quantities[ITEM_SWEET_ITEMS] -= 2;
            inventory->quantities[ITEM_SUGAR_SALT] -= 1;
            
            // Mark paste as consumed
            status->sold_items[PRODUCT_PASTE]++;
            break;
            
        case CHEF_SAVORY_PATISSERIE:
            product_type = PRODUCT_SAVORY_PATISSERIE;
            // Consume ingredients for savory patisserie
            inventory->quantities[ITEM_CHEESE_SALAMI] -= 1;
            inventory->quantities[ITEM_BUTTER] -= 1;
            
            // Mark paste as consumed
            status->sold_items[PRODUCT_PASTE]++;
            break;
            
        default:
            fprintf(stderr, "Unknown chef type: %d\n", type);
            return;
    }
    
    // Increment produced items counter
    status->produced_items[product_type]++;
}

// Chef process main function
void chef_process(ChefType type, int id, int inventory_shm_id, int prod_status_shm_id,
                 int inventory_sem_id, int prod_sem_id, BakeryConfig config) {
    
    // Attach to shared memory segments
    Inventory *inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    ProductionStatus *status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (inventory == (void *) -1 || status == (void *) -1) {
        perror("Chef: Failed to attach to shared memory");
        exit(EXIT_FAILURE);
    }
    
    // Define semaphore operations
    struct sembuf inventory_lock = {0, -1, 0};
    struct sembuf inventory_unlock = {0, 1, 0};
    struct sembuf prod_lock = {0, -1, 0};
    struct sembuf prod_unlock = {0, 1, 0};
    
    printf("Chef %d of type %d started (PID: %d)\n", id, type, getpid());
    
    // Main chef loop
    while (status->simulation_active) {
        // Determine what product to prepare based on chef type
        ProductType product_type;
        int subtype = 0;  // Default subtype
        
        switch (type) {
            case CHEF_PASTE:
                product_type = PRODUCT_PASTE;
                break;
            case CHEF_SANDWICH:
                product_type = PRODUCT_SANDWICH;
                // Choose a random sandwich type
                subtype = rand() % config.num_categories[PRODUCT_SANDWICH];
                break;
            case CHEF_CAKE:
                product_type = PRODUCT_CAKE;
                // Choose a random cake flavor
                subtype = rand() % config.num_categories[PRODUCT_CAKE];
                break;
            case CHEF_SWEET:
                product_type = PRODUCT_SWEET;
                // Choose a random sweet flavor
                subtype = rand() % config.num_categories[PRODUCT_SWEET];
                break;
            case CHEF_SWEET_PATISSERIE:
                product_type = PRODUCT_SWEET_PATISSERIE;
                // Choose a random sweet patisserie type
                subtype = rand() % config.num_categories[PRODUCT_SWEET_PATISSERIE];
                break;
            case CHEF_SAVORY_PATISSERIE:
                product_type = PRODUCT_SAVORY_PATISSERIE;
                // Choose a random savory patisserie type
                subtype = rand() % config.num_categories[PRODUCT_SAVORY_PATISSERIE];
                break;
            default:
                fprintf(stderr, "Unknown chef type: %d\n", type);
                sleep(5); // Wait before retrying
                continue;
        }
        
        // Lock inventory to check ingredients
        if (semop(inventory_sem_id, &inventory_lock, 1) == -1) {
            perror("Chef: Failed to lock inventory semaphore");
            sleep(1);
            continue;
        }
        
        // Check if we have necessary ingredients
        bool ingredients_available = check_dependencies(type, inventory);
        
        if (!ingredients_available) {
            // Not enough ingredients, unlock inventory and wait
            if (semop(inventory_sem_id, &inventory_unlock, 1) == -1) {
                perror("Chef: Failed to unlock inventory semaphore");
            }
            sleep(3);
            continue;
        }
        
        // Lock production status to check pastry dependencies if needed
        if ((type == CHEF_SWEET_PATISSERIE || type == CHEF_SAVORY_PATISSERIE) && 
            (semop(prod_sem_id, &prod_lock, 1) == -1)) {
            perror("Chef: Failed to lock production status semaphore");
            
            // Unlock inventory
            if (semop(inventory_sem_id, &inventory_unlock, 1) == -1) {
                perror("Chef: Failed to unlock inventory semaphore");
            }
            sleep(1);
            continue;
        }
        
        // For patisserie, check if there's available paste
        bool can_proceed = true;
        if (type == CHEF_SWEET_PATISSERIE || type == CHEF_SAVORY_PATISSERIE) {
            if (status->produced_items[PRODUCT_PASTE] - status->sold_items[PRODUCT_PASTE] <= 0) {
                can_proceed = false;
            }
            
            // Unlock production status
            if (semop(prod_sem_id, &prod_unlock, 1) == -1) {
                perror("Chef: Failed to unlock production status semaphore");
            }
        }
        
        if (!can_proceed) {
            // Not enough paste, unlock inventory and wait
            if (semop(inventory_sem_id, &inventory_unlock, 1) == -1) {
                perror("Chef: Failed to unlock inventory semaphore");
            }
            sleep(2);
            continue;
        }
        
        // We have all required ingredients, proceed to produce the item
        produce_item(type, inventory, status, config);
        
        // Unlock inventory
        if (semop(inventory_sem_id, &inventory_unlock, 1) == -1) {
            perror("Chef: Failed to unlock inventory semaphore");
            continue;
        }
        
        printf("Chef %d of type %d prepared product type %d (subtype %d)\n", 
               id, type, product_type, subtype);
        
        // Sleep to simulate work time
        int work_time = config.production_times[product_type] * 1000;
        usleep(work_time);
    }
    
    printf("Chef %d of type %d terminating (PID: %d)\n", id, type, getpid());
    
    // Detach from shared memory
    shmdt(inventory);
    shmdt(status);
}

// Reallocate chefs between teams (called by management)
void reallocate_chefs(ChefTeam *teams, ChefType from_team, ChefType to_team, int num_to_move) {
    if (from_team >= CHEF_TYPE_COUNT || to_team >= CHEF_TYPE_COUNT || from_team == to_team) {
        fprintf(stderr, "Invalid team reallocation request: %d -> %d\n", from_team, to_team);
        return;
    }
    
    // Check if we have enough chefs to move
    if (teams[from_team].team_size < num_to_move + 1) {  // Always keep at least one chef
        num_to_move = teams[from_team].team_size - 1;
        if (num_to_move <= 0) {
            printf("Cannot move chefs from team %d (only %d available)\n", 
                   from_team, teams[from_team].team_size);
            return;
        }
    }
    
    // Update the team counts
    teams[from_team].team_size -= num_to_move;
    teams[to_team].team_size += num_to_move;
    
    printf("Reallocated %d chef(s) from team %d (%d remaining) to team %d (now %d)\n",
           num_to_move, from_team, teams[from_team].team_size, to_team, teams[to_team].team_size);
}

// Initialize chef teams based on the bakery configuration
void initialize_chef_teams(ChefTeam *teams, BakeryConfig config) {
    for (int i = 0; i < CHEF_TYPE_COUNT; i++) {
        teams[i].type = (ChefType)i;
        teams[i].team_size = config.num_chefs[i];
        teams[i].active_chefs = config.num_chefs[i];
        teams[i].chefs = (Chef *)malloc(teams[i].team_size * sizeof(Chef));
        
        if (teams[i].chefs == NULL) {
            perror("Failed to allocate memory for chef team");
            exit(EXIT_FAILURE);
        }
        
        for (int j = 0; j < teams[i].team_size; j++) {
            teams[i].chefs[j].id = j;
            teams[i].chefs[j].type = (ChefType)i;
            teams[i].chefs[j].pid = 0;  // Will be set when chef processes are created
            teams[i].chefs[j].active = true;
        }
        
        printf("Initialized team %d with %d chefs\n", i, teams[i].team_size);
    }
}