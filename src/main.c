#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <GL/glut.h>

#include "../include/common.h"
#include "../include/chef.h"
#include "../include/baker.h"
#include "../include/seller.h"
#include "../include/customer.h"
#include "../include/supply_chain.h"
#include "../include/management.h"

// Global variables
BakeryConfig bakery_config;
ProductionStatus *prod_status = NULL;
Inventory *inventory = NULL;

// IPC resource IDs
int inventory_shm_id = -1;
int prod_status_shm_id = -1;
int inventory_sem_id = -1;
int prod_sem_id = -1;
int customer_msgq_id = -1;
int management_msgq_id = -1;

// Process tracking
pid_t *chef_pids = NULL;
pid_t *baker_pids = NULL;
pid_t *seller_pids = NULL;
pid_t *supply_chain_pids = NULL;
pid_t customer_gen_pid = -1;
pid_t management_pid = -1;

// Forward declarations
void cleanup_resources();
void initialize_ipc_resources();
void start_processes();
void signal_handler(int sig);

// Forward declaration of the setup_opengl function from visualization.c
void setup_opengl(int argc, char *argv[], int inventory_shm_id, int prod_status_shm_id,
                 BakeryConfig bakery_config);

// Main function
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file_path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Initialize random seed
    srand(time(NULL));
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load configuration from file
    bakery_config = load_config(argv[1]);
    
    // Initialize IPC resources
    initialize_ipc_resources();
    
    // Start bakery processes
    start_processes();
    
    // Set up OpenGL visualization (this will block until window is closed)
    setup_opengl(argc, argv, inventory_shm_id, prod_status_shm_id, bakery_config);
    
    // Wait for child processes and clean up
    cleanup_resources();
    
    return EXIT_SUCCESS;
}

// Create and initialize all IPC resources needed for the simulation
void initialize_ipc_resources() {
    // Create shared memory segments
    inventory_shm_id = shmget(INVENTORY_SHM_KEY, sizeof(Inventory), IPC_CREAT | 0666);
    if (inventory_shm_id == -1) {
        perror("Failed to create inventory shared memory");
        exit(EXIT_FAILURE);
    }
    
    prod_status_shm_id = shmget(PRODUCTION_SHM_KEY, sizeof(ProductionStatus), IPC_CREAT | 0666);
    if (prod_status_shm_id == -1) {
        perror("Failed to create production status shared memory");
        shmctl(inventory_shm_id, IPC_RMID, NULL); // Clean up since it the only one exits
        exit(EXIT_FAILURE);
    }
    
    // Attach to shared memory segments
    inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    if (inventory == (void *) -1) {
        perror("Failed to attach to inventory shared memory");
        shmctl(inventory_shm_id, IPC_RMID, NULL);
        shmctl(prod_status_shm_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    
    prod_status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    if (prod_status == (void *) -1) {
        perror("Failed to attach to production status shared memory");
        shmdt(inventory);
        shmctl(inventory_shm_id, IPC_RMID, NULL);
        shmctl(prod_status_shm_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    
    // Create semaphores
    inventory_sem_id = semget(INVENTORY_SEM_KEY, 1, IPC_CREAT | 0666);
    if (inventory_sem_id == -1) {
        perror("Failed to create inventory semaphore");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    // Initialize semaphore to 1 (binary semaphore/mutex)
    union semun {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } sem_arg;
    
    sem_arg.val = 1;
    if (semctl(inventory_sem_id, 0, SETVAL, sem_arg) == -1) {
        perror("Failed to initialize inventory semaphore");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    prod_sem_id = semget(PRODUCTION_SEM_KEY, 1, IPC_CREAT | 0666);
    if (prod_sem_id == -1) {
        perror("Failed to create production semaphore");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    // Initialize semaphore to 1
    if (semctl(prod_sem_id, 0, SETVAL, sem_arg) == -1) {
        perror("Failed to initialize production semaphore");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    // Create message queues

    /*
    Enables communication between customers and sellers
    Allows customers to send product requests to sellers
    Lets customers file complaints about products
    Permits sellers to send responses back to specific customers
    Facilitates asynchronous interaction between customers and bakery staff
    */

    customer_msgq_id = msgget(CUSTOMER_MSG_KEY, IPC_CREAT | 0666);
    if (customer_msgq_id == -1) {
        perror("Failed to create customer message queue");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    /*
    Enables communication between supply chain employees and management
    Used for sending urgent supply updates when inventory runs low
    Allows management to send decisions about resource allocation
    Used for simulation control messages (like termination notifications)
    */

    management_msgq_id = msgget(MANAGEMENT_MSG_KEY, IPC_CREAT | 0666);
    if (management_msgq_id == -1) {
        perror("Failed to create management message queue");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    // Initialize shared memory data
    memset(inventory, 0, sizeof(Inventory));
    memset(prod_status, 0, sizeof(ProductionStatus));
    
    // Set initial values
    prod_status->start_time = time(NULL);
    prod_status->simulation_active = true;
    
    // Initialize inventory with some starting values
    initialize_inventory(inventory, bakery_config);
    
    printf("All IPC resources initialized successfully\n");
}

// Start all bakery processes
void start_processes() {
    int total_chefs = 0;
    int total_bakers = 0;
    
    // Count total number of chefs and bakers
    for (int i = 0; i < CHEF_TYPE_COUNT; i++) {
        total_chefs += bakery_config.num_chefs[i];
    }
    
    for (int i = 0; i < BAKER_TYPE_COUNT; i++) {
        total_bakers += bakery_config.num_bakers[i];
    }
    
    // Allocate arrays for process IDs
    chef_pids = (pid_t *) malloc(total_chefs * sizeof(pid_t));
    baker_pids = (pid_t *) malloc(total_bakers * sizeof(pid_t));
    seller_pids = (pid_t *) malloc(bakery_config.num_sellers * sizeof(pid_t));
    supply_chain_pids = (pid_t *) malloc(bakery_config.num_supply_chain * sizeof(pid_t));
    
    if (!chef_pids || !baker_pids || !seller_pids || !supply_chain_pids) {
        perror("Failed to allocate memory for process IDs");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }
    
    // Create chef processes
    int chef_idx = 0;
    for (int type = 0; type < CHEF_TYPE_COUNT; type++) {
        for (int i = 0; i < bakery_config.num_chefs[type]; i++) {
            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to fork chef process");
                cleanup_resources();
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // Child process (chef)
                chef_process((ChefType)type, i, inventory_shm_id, prod_status_shm_id, 
                             inventory_sem_id, prod_sem_id, bakery_config);
                exit(EXIT_SUCCESS);  // Should not reach here
            } else {
                // Parent process
                chef_pids[chef_idx++] = pid;
                printf("Started chef process %d of type %d with PID %d\n", i, type, pid);
            }
        }
    }
    
    // Create baker processes
    int baker_idx = 0;
    for (int type = 0; type < BAKER_TYPE_COUNT; type++) {
        for (int i = 0; i < bakery_config.num_bakers[type]; i++) {
            pid_t pid = fork();
            if (pid == -1) {
                perror("Failed to fork baker process");
                cleanup_resources();
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // Child process (baker)
                baker_process((BakerType)type, i, inventory_shm_id, prod_status_shm_id, 
                              inventory_sem_id, prod_sem_id, bakery_config);
                exit(EXIT_SUCCESS);  // Should not reach here
            } else {
                // Parent process
                baker_pids[baker_idx++] = pid;
                printf("Started baker process %d of type %d with PID %d\n", i, type, pid);
            }
        }
    }
    
    // Create seller processes
    for (int i = 0; i < bakery_config.num_sellers; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("Failed to fork seller process");
            cleanup_resources();
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Child process (seller)
            seller_process(i, customer_msgq_id, prod_status_shm_id, 
                          prod_sem_id, bakery_config);
            exit(EXIT_SUCCESS);  // Should not reach here
        } else {
            // Parent process
            seller_pids[i] = pid;
            printf("Started seller process %d with PID %d\n", i, pid);
        }
    }
    
    // Create supply chain processes
    for (int i = 0; i < bakery_config.num_supply_chain; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("Failed to fork supply chain process");
            cleanup_resources();
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Child process (supply chain)
            supply_chain_process(i, inventory_shm_id, prod_status_shm_id,
                                inventory_sem_id, management_msgq_id, bakery_config);
            exit(EXIT_SUCCESS);  // Should not reach here
        } else {
            // Parent process
            supply_chain_pids[i] = pid;
            printf("Started supply chain process %d with PID %d\n", i, pid);
        }
    }
    
    // Create customer generator process
    customer_gen_pid = fork();
    if (customer_gen_pid == -1) {
        perror("Failed to fork customer generator process");
        cleanup_resources();
        exit(EXIT_FAILURE);
    } else if (customer_gen_pid == 0) {
        // Child process (customer generator)
        customer_generator(customer_msgq_id, prod_status_shm_id, 
                          prod_sem_id, bakery_config);
        exit(EXIT_SUCCESS);  // Should not reach here
    } else {
        printf("Started customer generator process with PID %d\n", customer_gen_pid);
    }
    
    // Create management process
    management_pid = fork();
    if (management_pid == -1) {
        perror("Failed to fork management process");
        cleanup_resources();
        exit(EXIT_FAILURE);
    } else if (management_pid == 0) {
        // Child process (management)
        management_process(inventory_shm_id, prod_status_shm_id, 
                         management_msgq_id, customer_msgq_id,
                         inventory_sem_id, prod_sem_id, bakery_config);
        exit(EXIT_SUCCESS);  // Should not reach here
    } else {
        printf("Started management process with PID %d\n", management_pid);
    }
    
    printf("All processes started successfully\n");
}

// Signal handler for graceful termination
void signal_handler(int sig) {
    printf("Received signal %d, terminating...\n", sig);
    
    // Mark simulation as inactive
    if (prod_status) {
        prod_status->simulation_active = false;
    }
    
    cleanup_resources();
    exit(EXIT_SUCCESS);
}

// Clean up all allocated resources
void cleanup_resources() {
    int status;
    
    // Kill all child processes if they are still running
    if (chef_pids) {
        for (int i = 0; i < CHEF_TYPE_COUNT; i++) {
            for (int j = 0; j < bakery_config.num_chefs[i]; j++) {
                kill(chef_pids[i * bakery_config.num_chefs[0] + j], SIGTERM);
            }
        }
        free(chef_pids);
    }
    
    if (baker_pids) {
        for (int i = 0; i < BAKER_TYPE_COUNT; i++) {
            for (int j = 0; j < bakery_config.num_bakers[i]; j++) {
                kill(baker_pids[i * bakery_config.num_bakers[0] + j], SIGTERM);
            }
        }
        free(baker_pids);
    }
    
    if (seller_pids) {
        for (int i = 0; i < bakery_config.num_sellers; i++) {
            kill(seller_pids[i], SIGTERM);
        }
        free(seller_pids);
    }
    
    if (supply_chain_pids) {
        for (int i = 0; i < bakery_config.num_supply_chain; i++) {
            kill(supply_chain_pids[i], SIGTERM);
        }
        free(supply_chain_pids);
    }
    
    if (customer_gen_pid > 0) {
        kill(customer_gen_pid, SIGTERM);
    }
    
    if (management_pid > 0) {
        kill(management_pid, SIGTERM);
    }
    
    // Wait for all child processes to terminate
    while (wait(&status) > 0);
    
    // Detach and remove shared memory
    if (inventory != NULL && inventory != (void *) -1) {
        shmdt(inventory);
    }
    
    if (prod_status != NULL && prod_status != (void *) -1) {
        shmdt(prod_status);
    }
    
    if (inventory_shm_id != -1) {
        shmctl(inventory_shm_id, IPC_RMID, NULL);
    }
    
    if (prod_status_shm_id != -1) {
        shmctl(prod_status_shm_id, IPC_RMID, NULL);
    }
    
    // Remove semaphores
    if (inventory_sem_id != -1) {
        semctl(inventory_sem_id, 0, IPC_RMID);
    }
    
    if (prod_sem_id != -1) {
        semctl(prod_sem_id, 0, IPC_RMID);
    }
    
    // Remove message queues
    if (customer_msgq_id != -1) {
        msgctl(customer_msgq_id, IPC_RMID, NULL);
    }
    
    if (management_msgq_id != -1) {
        msgctl(management_msgq_id, IPC_RMID, NULL);
    }
    
    printf("All resources cleaned up\n");
}

// Load bakery configuration from file
BakeryConfig load_config(const char *config_file) {
    BakeryConfig config;
    FILE *fp;
    char line[256];
    char key[64];
    char value[64];
    
    // Initialize config with default values
    memset(&config, 0, sizeof(BakeryConfig));
    
    // Set default maximum production limits
    config.max_items_per_type[PRODUCT_BREAD] = 50;
    config.max_items_per_type[PRODUCT_CAKE] = 30;
    config.max_items_per_type[PRODUCT_SANDWICH] = 40; 
    config.max_items_per_type[PRODUCT_SWEET] = 60;
    config.max_items_per_type[PRODUCT_SWEET_PATISSERIE] = 25;
    config.max_items_per_type[PRODUCT_SAVORY_PATISSERIE] = 25;
    
    fp = fopen(config_file, "r");
    if (!fp) {
        perror("Failed to open configuration file");
        exit(EXIT_FAILURE);
    }
    
    // Read configuration line by line
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        // Parse key=value format
        if (sscanf(line, "%[^=]=%[^\n]", key, value) == 2) {
            // Remove any trailing whitespace
            int len = strlen(key);
            while (len > 0 && (key[len-1] == ' ' || key[len-1] == '\t')) {
                key[--len] = '\0';
            }
            
            // Product categories
            if (strcmp(key, "NUM_BREAD_CATEGORIES") == 0) {
                config.num_categories[PRODUCT_BREAD] = atoi(value);
            } else if (strcmp(key, "NUM_SANDWICH_TYPES") == 0) {
                config.num_categories[PRODUCT_SANDWICH] = atoi(value);
            } else if (strcmp(key, "NUM_CAKE_FLAVORS") == 0) {
                config.num_categories[PRODUCT_CAKE] = atoi(value);
            } else if (strcmp(key, "NUM_SWEET_FLAVORS") == 0) {
                config.num_categories[PRODUCT_SWEET] = atoi(value);
            } else if (strcmp(key, "NUM_SWEET_PATISSERIES") == 0) {
                config.num_categories[PRODUCT_SWEET_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "NUM_SAVORY_PATISSERIES") == 0) {
                config.num_categories[PRODUCT_SAVORY_PATISSERIE] = atoi(value);
            }
            
            // Maximum product limits
            else if (strcmp(key, "MAX_BREAD") == 0) {
                config.max_items_per_type[PRODUCT_BREAD] = atoi(value);
            } else if (strcmp(key, "MAX_CAKE") == 0) {
                config.max_items_per_type[PRODUCT_CAKE] = atoi(value);
            } else if (strcmp(key, "MAX_SANDWICH") == 0) {
                config.max_items_per_type[PRODUCT_SANDWICH] = atoi(value);
            } else if (strcmp(key, "MAX_SWEET") == 0) {
                config.max_items_per_type[PRODUCT_SWEET] = atoi(value);
            } else if (strcmp(key, "MAX_SWEET_PATISSERIE") == 0) {
                config.max_items_per_type[PRODUCT_SWEET_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "MAX_SAVORY_PATISSERIE") == 0) {
                config.max_items_per_type[PRODUCT_SAVORY_PATISSERIE] = atoi(value);
            }
            
            // Staff configuration
            else if (strcmp(key, "NUM_PASTE_CHEFS") == 0) {
                config.num_chefs[CHEF_PASTE] = atoi(value);
            } else if (strcmp(key, "NUM_CAKE_CHEFS") == 0) {
                config.num_chefs[CHEF_CAKE] = atoi(value);
            } else if (strcmp(key, "NUM_SANDWICH_CHEFS") == 0) {
                config.num_chefs[CHEF_SANDWICH] = atoi(value);
            } else if (strcmp(key, "NUM_SWEET_CHEFS") == 0) {
                config.num_chefs[CHEF_SWEET] = atoi(value);
            } else if (strcmp(key, "NUM_SWEET_PATISSERIE_CHEFS") == 0) {
                config.num_chefs[CHEF_SWEET_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "NUM_SAVORY_PATISSERIE_CHEFS") == 0) {
                config.num_chefs[CHEF_SAVORY_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "NUM_CAKE_SWEETS_BAKERS") == 0) {
                config.num_bakers[BAKER_CAKE_SWEET] = atoi(value);
            } else if (strcmp(key, "NUM_PATISSERIE_BAKERS") == 0) {
                config.num_bakers[BAKER_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "NUM_BREAD_BAKERS") == 0) {
                config.num_bakers[BAKER_BREAD] = atoi(value);
            } else if (strcmp(key, "NUM_SELLERS") == 0) {
                config.num_sellers = atoi(value);
            } else if (strcmp(key, "NUM_SUPPLY_CHAIN_EMPLOYEES") == 0) {
                config.num_supply_chain = atoi(value);
            }
            
            // Supply chain configuration
            else if (strcmp(key, "WHEAT_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_WHEAT] = atoi(value);
            } else if (strcmp(key, "WHEAT_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_WHEAT] = atoi(value);
            } else if (strcmp(key, "YEAST_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_YEAST] = atoi(value);
            } else if (strcmp(key, "YEAST_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_YEAST] = atoi(value);
            } else if (strcmp(key, "BUTTER_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_BUTTER] = atoi(value);
            } else if (strcmp(key, "BUTTER_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_BUTTER] = atoi(value);
            } else if (strcmp(key, "MILK_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_MILK] = atoi(value);
            } else if (strcmp(key, "MILK_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_MILK] = atoi(value);
            } else if (strcmp(key, "SUGAR_SALT_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_SUGAR_SALT] = atoi(value);
            } else if (strcmp(key, "SUGAR_SALT_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_SUGAR_SALT] = atoi(value);
            } else if (strcmp(key, "SWEET_ITEMS_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_SWEET_ITEMS] = atoi(value);
            } else if (strcmp(key, "SWEET_ITEMS_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_SWEET_ITEMS] = atoi(value);
            } else if (strcmp(key, "CHEESE_SALAMI_MIN_PURCHASE") == 0) {
                config.min_purchases[ITEM_CHEESE_SALAMI] = atoi(value);
            } else if (strcmp(key, "CHEESE_SALAMI_MAX_PURCHASE") == 0) {
                config.max_purchases[ITEM_CHEESE_SALAMI] = atoi(value);
            }
            
            // Product prices
            else if (strcmp(key, "BREAD_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_BREAD] = atof(value);
            } else if (strcmp(key, "SANDWICH_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_SANDWICH] = atof(value);
            } else if (strcmp(key, "CAKE_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_CAKE] = atof(value);
            } else if (strcmp(key, "SWEET_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_SWEET] = atof(value);
            } else if (strcmp(key, "SWEET_PATISSERIE_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_SWEET_PATISSERIE] = atof(value);
            } else if (strcmp(key, "SAVORY_PATISSERIE_BASE_PRICE") == 0) {
                config.product_prices[PRODUCT_SAVORY_PATISSERIE] = atof(value);
            }
            
            // Simulation thresholds
            else if (strcmp(key, "FRUSTRATED_CUSTOMER_THRESHOLD") == 0) {
                config.thresholds[0] = atoi(value);
            } else if (strcmp(key, "COMPLAINED_CUSTOMER_THRESHOLD") == 0) {
                config.thresholds[1] = atoi(value);
            } else if (strcmp(key, "MISSING_ITEMS_REQUEST_THRESHOLD") == 0) {
                config.thresholds[2] = atoi(value);
            } else if (strcmp(key, "PROFIT_THRESHOLD") == 0) {
                config.thresholds[3] = atoi(value);
            } else if (strcmp(key, "SIMULATION_MAX_TIME_MINUTES") == 0) {
                config.max_simulation_time = atoi(value);
            }
            
            // Customer parameters
            else if (strcmp(key, "CUSTOMER_ARRIVAL_MIN_INTERVAL") == 0) {
                config.customer_params[0] = atoi(value);
            } else if (strcmp(key, "CUSTOMER_ARRIVAL_MAX_INTERVAL") == 0) {
                config.customer_params[1] = atoi(value);
            } else if (strcmp(key, "CUSTOMER_PATIENCE_MIN_SECONDS") == 0) {
                config.customer_params[2] = atoi(value);
            } else if (strcmp(key, "CUSTOMER_PATIENCE_MAX_SECONDS") == 0) {
                config.customer_params[3] = atoi(value);
            } else if (strcmp(key, "CUSTOMER_COMPLAINT_PROBABILITY") == 0) {
                config.complaint_probability = atof(value);
            } else if (strcmp(key, "CUSTOMER_MAX_PURCHASE_ITEMS") == 0) {
                config.max_purchase_items = atoi(value);
            }
            
            // Production times
            else if (strcmp(key, "BREAD_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_BREAD] = atoi(value);
            } else if (strcmp(key, "SANDWICH_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_SANDWICH] = atoi(value);
            } else if (strcmp(key, "CAKE_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_CAKE] = atoi(value);
            } else if (strcmp(key, "SWEET_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_SWEET] = atoi(value);
            } else if (strcmp(key, "PATISSERIE_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_SWEET_PATISSERIE] = 
                config.production_times[PRODUCT_SAVORY_PATISSERIE] = atoi(value);
            } else if (strcmp(key, "PASTE_PRODUCTION_TIME") == 0) {
                config.production_times[PRODUCT_PASTE] = atoi(value);
            }
        }
    }
    
    fclose(fp);
    printf("Configuration loaded successfully\n");
    
    return config;
}