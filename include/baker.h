#ifndef BAKERY_BAKER_H
#define BAKERY_BAKER_H

#include "common.h"

// Baker process data
typedef struct {
    int id;
    BakerType type;
    pid_t pid;
    bool active;
} Baker;

// Baker team structure
typedef struct {
    BakerType type;
    int team_size;
    Baker *bakers;
    int active_bakers;
} BakerTeam;

// Function prototypes
void baker_process(BakerType type, int id, int inventory_shm_id, int prod_status_shm_id,
                  int inventory_sem_id, int prod_sem_id, BakeryConfig config);
void initialize_baker_teams(BakerTeam *teams, BakeryConfig config);
bool can_bake_product(BakerType baker_type, ProductType product_type);
bool bake_products(BakerType type, ProductionStatus *status, BakeryConfig config);

#endif // BAKERY_BAKER_H