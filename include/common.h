#ifndef BAKERY_COMMON_H
#define BAKERY_COMMON_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

// IPC keys
#define INVENTORY_SHM_KEY 0x1234
#define PRODUCTION_SHM_KEY 0x2345
#define INVENTORY_SEM_KEY 0x3456
#define PRODUCTION_SEM_KEY 0x4567
#define CUSTOMER_MSG_KEY 0x5678
#define MANAGEMENT_MSG_KEY 0x6789

// Message types
#define MSG_CUSTOMER_REQUEST 1
#define MSG_MANAGEMENT_DECISION 2
#define MSG_SUPPLY_CHAIN_UPDATE 3
#define MSG_SIMULATION_END 4
#define MSG_CUSTOMER_RESPONSE_BASE 100  // Base for customer response IDs

// Inventory item types (raw materials)
typedef enum {
    ITEM_WHEAT,
    ITEM_YEAST,
    ITEM_BUTTER,
    ITEM_MILK,
    ITEM_SUGAR_SALT,
    ITEM_SWEET_ITEMS,
    ITEM_CHEESE_SALAMI,
    ITEM_RAW_MATERIAL_COUNT
} RawMaterialType;

// Product types
typedef enum {
    PRODUCT_BREAD,
    PRODUCT_SANDWICH,
    PRODUCT_CAKE,
    PRODUCT_SWEET,
    PRODUCT_SWEET_PATISSERIE,
    PRODUCT_SAVORY_PATISSERIE,
    PRODUCT_PASTE,
    PRODUCT_TYPE_COUNT
} ProductType;

// Chef types
typedef enum {
    CHEF_PASTE,
    CHEF_CAKE,
    CHEF_SANDWICH,
    CHEF_SWEET,
    CHEF_SWEET_PATISSERIE,
    CHEF_SAVORY_PATISSERIE,
    CHEF_TYPE_COUNT
} ChefType;

// Baker types
typedef enum {
    BAKER_CAKE_SWEET,
    BAKER_PATISSERIE,
    BAKER_BREAD,
    BAKER_TYPE_COUNT
} BakerType;

// Shared memory structure for inventory
typedef struct {
    int quantities[ITEM_RAW_MATERIAL_COUNT];
    int min_thresholds[ITEM_RAW_MATERIAL_COUNT];
} Inventory;

// Shared memory structure for production status
typedef struct {
    int produced_items[PRODUCT_TYPE_COUNT];
    int sold_items[PRODUCT_TYPE_COUNT];
    int frustrated_customers;
    int complained_customers;
    int missing_items_requests;
    double total_profit;
    time_t start_time;
    bool simulation_active;
} ProductionStatus;

// Message structure for customer requests
typedef struct {
    long msg_type;
    int customer_id;
    ProductType product_type;
    int subtype;  // Flavor, variety, etc.
    int quantity;
    bool is_complaint;
    bool fulfilled;  // Indicates if request was fulfilled
} CustomerMsg;

// Message structure for management decisions
typedef struct {
    long msg_type;
    ChefType chef_type_from;
    ChefType chef_type_to;
    int num_chefs_to_move;
} ManagementMsg;

// Configuration structure loaded from file
typedef struct {
    // Product categories
    int num_categories[PRODUCT_TYPE_COUNT];
    
    // Staff configuration
    int num_chefs[CHEF_TYPE_COUNT];
    int num_bakers[BAKER_TYPE_COUNT];
    int num_sellers;
    int num_supply_chain;
    
    // Supply chain configuration
    int min_purchases[ITEM_RAW_MATERIAL_COUNT];
    int max_purchases[ITEM_RAW_MATERIAL_COUNT];
    
    // Product prices
    double product_prices[PRODUCT_TYPE_COUNT];
    
    // Production times (in milliseconds)
    int production_times[PRODUCT_TYPE_COUNT];
    
    // Maximum items per product type
    int max_items_per_type[PRODUCT_TYPE_COUNT];
    
    // Simulation thresholds
    int thresholds[4];  // [frustrated, complained, missing, profit]
    int max_simulation_time;  // in minutes
    
    // Customer parameters
    int customer_params[4];  // [arrival_min, arrival_max, patience_min, patience_max]
    double complaint_probability;
    int max_purchase_items;
} BakeryConfig;

// Forward declaration for config loading function
BakeryConfig load_config(const char *config_file);

#endif // BAKERY_COMMON_H