#ifndef BAKERY_CHEF_H
#define BAKERY_CHEF_H

#include "common.h"

// Chef process data
typedef struct {
    int id;
    ChefType type;
    pid_t pid;
    bool active;
} Chef;

// Chef team structure
typedef struct {
    ChefType type;
    int team_size;
    Chef *chefs;
    int active_chefs;
} ChefTeam;

// Function prototypes
void chef_process(ChefType type, int id, int inventory_shm_id, int prod_status_shm_id, 
                  int inventory_sem_id, int prod_sem_id, BakeryConfig config);
void initialize_chef_teams(ChefTeam *teams, BakeryConfig config);
void reallocate_chefs(ChefTeam *teams, ChefType from_team, ChefType to_team, int count);
bool check_dependencies(ChefType type, Inventory *inventory);
void produce_item(ChefType type, Inventory *inventory, ProductionStatus *status, BakeryConfig config);

#endif // BAKERY_CHEF_H