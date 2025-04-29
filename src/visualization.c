#include "../include/common.h"
#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <math.h>

// Global variables for visualization
extern ProductionStatus *prod_status;
extern Inventory *inventory;
BakeryConfig config;

// Colors for different elements
float colors[7][3] = {
    {0.9f, 0.7f, 0.4f},  // Bread (light brown)
    {0.9f, 0.8f, 0.6f},  // Sandwich (beige)
    {0.8f, 0.4f, 0.4f},  // Cake (red)
    {0.9f, 0.6f, 0.6f},  // Sweet (pink)
    {0.7f, 0.5f, 0.3f},  // Sweet Patisserie (brown)
    {0.6f, 0.6f, 0.2f},  // Savory Patisserie (olive)
    {0.9f, 0.9f, 0.7f}   // Paste (cream)
};

// Window properties
int win_width = 800;
int win_height = 600;

// Function to draw text on the screen
void renderText(float x, float y, const char *text) {
    glRasterPos2f(x, y);
    
    for (const char *c = text; *c != '\0'; c++) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    }
}

// Draw a bar graph showing production and sales
void drawProductionGraph(float x, float y, float width, float height) {
    float bar_width = width / PRODUCT_TYPE_COUNT;
    float max_value = 0;
    
    // Find maximum value for scaling
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        if (prod_status->produced_items[i] > max_value) {
            max_value = prod_status->produced_items[i];
        }
        if (prod_status->sold_items[i] > max_value) {
            max_value = prod_status->sold_items[i];
        }
    }
    
    // Make sure we have at least some max value for scaling
    if (max_value < 10) max_value = 10;
    
    // Draw axis
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex2f(x, y);
    glVertex2f(x, y - height);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glEnd();
    
    // Draw bars
    for (int i = 0; i < PRODUCT_TYPE_COUNT; i++) {
        float prod_height = 0;
        float sold_height = 0;
        
        if (max_value > 0) {
            prod_height = (float)prod_status->produced_items[i] / max_value * height;
            sold_height = (float)prod_status->sold_items[i] / max_value * height;
        }
        
        // Draw produced items bar
        glColor3fv(colors[i]);
        glBegin(GL_QUADS);
        glVertex2f(x + i * bar_width + 2, y);
        glVertex2f(x + (i + 1) * bar_width - 2, y);
        glVertex2f(x + (i + 1) * bar_width - 2, y - prod_height);
        glVertex2f(x + i * bar_width + 2, y - prod_height);
        glEnd();
        
        // Draw sold items bar (slightly narrower)
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(x + i * bar_width + 4, y);
        glVertex2f(x + (i + 1) * bar_width - 4, y);
        glVertex2f(x + (i + 1) * bar_width - 4, y - sold_height);
        glVertex2f(x + i * bar_width + 4, y - sold_height);
        glEnd();
        
        // Draw product label
        char label[32];
        const char *product_names[] = {"Bread", "Sandwich", "Cake", "Sweet", 
                                      "Sweet P.", "Savory P.", "Paste"};
        sprintf(label, "%s", product_names[i]);
        glColor3f(1.0f, 1.0f, 1.0f);
        renderText(x + i * bar_width + 5, y + 15, label);
        
        sprintf(label, "%d/%d", prod_status->produced_items[i], prod_status->sold_items[i]);
        renderText(x + i * bar_width + 5, y + 5, label);
    }
    
    // Draw legend
    glColor3f(1.0f, 1.0f, 1.0f);
    renderText(x + width - 120, y - height + 20, "Produced items");
    renderText(x + width - 120, y - height + 5, "Sold items");
}

// Draw inventory levels
void drawInventoryLevels(float x, float y, float width, float height) {
    float bar_width = width / ITEM_RAW_MATERIAL_COUNT;
    float max_value = 0;
    
    // Find maximum value for scaling
    for (int i = 0; i < ITEM_RAW_MATERIAL_COUNT; i++) {
        if (inventory->quantities[i] > max_value) {
            max_value = inventory->quantities[i];
        }
    }
    
    // Make sure we have at least some max value for scaling
    if (max_value < 10) max_value = 10;
    
    // Draw bars
    for (int i = 0; i < ITEM_RAW_MATERIAL_COUNT; i++) {
        float level_height = (float)inventory->quantities[i] / max_value * height;
        float threshold_height = (float)inventory->min_thresholds[i] / max_value * height;
        
        // Choose color based on inventory level
        if (inventory->quantities[i] <= inventory->min_thresholds[i]) {
            glColor3f(0.8f, 0.0f, 0.0f);  // Red for low stock
        } else {
            glColor3f(0.0f, 0.7f, 0.0f);  // Green for good stock
        }
        
        // Draw inventory bar
        glBegin(GL_QUADS);
        glVertex2f(x + i * bar_width + 2, y);
        glVertex2f(x + (i + 1) * bar_width - 2, y);
        glVertex2f(x + (i + 1) * bar_width - 2, y - level_height);
        glVertex2f(x + i * bar_width + 2, y - level_height);
        glEnd();
        
        // Draw threshold line
        glColor3f(1.0f, 0.5f, 0.0f);
        glBegin(GL_LINES);
        glVertex2f(x + i * bar_width, y - threshold_height);
        glVertex2f(x + (i + 1) * bar_width, y - threshold_height);
        glEnd();
        
        // Draw inventory label
        char label[32];
        const char *material_names[] = {"Wheat", "Yeast", "Butter", "Milk", 
                                       "Sugar", "Sweets", "Cheese"};
        sprintf(label, "%s", material_names[i]);
        glColor3f(1.0f, 1.0f, 1.0f);
        renderText(x + i * bar_width + 2, y + 15, label);
        
        sprintf(label, "%d", inventory->quantities[i]);
        renderText(x + i * bar_width + 5, y + 5, label);
    }
}

// Draw bakery status info
void drawStatusInfo(float x, float y) {
    char buffer[256];
    glColor3f(1.0f, 1.0f, 1.0f);
    
    sprintf(buffer, "Bakery Simulation Status");
    renderText(x, y, buffer);
    
    sprintf(buffer, "Current profit: $%.2f", prod_status->total_profit);
    renderText(x, y - 20, buffer);
    
    sprintf(buffer, "Frustrated customers: %d/%d", 
            prod_status->frustrated_customers, config.thresholds[0]);
    renderText(x, y - 40, buffer);
    
    sprintf(buffer, "Complained customers: %d/%d", 
            prod_status->complained_customers, config.thresholds[1]);
    renderText(x, y - 60, buffer);
    
    sprintf(buffer, "Missing items requests: %d/%d", 
            prod_status->missing_items_requests, config.thresholds[2]);
    renderText(x, y - 80, buffer);
    
    time_t current_time = time(NULL);
    int elapsed_minutes = (int)((current_time - prod_status->start_time) / 60);
    sprintf(buffer, "Elapsed time: %d/%d minutes", 
            elapsed_minutes, config.max_simulation_time);
    renderText(x, y - 100, buffer);
}

// Display callback function
void display() {
    // Clear the screen
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Draw the production graph
    drawProductionGraph(50, 550, 700, 200);
    
    // Draw inventory levels
    drawInventoryLevels(50, 300, 700, 100);
    
    // Draw status info
    drawStatusInfo(50, 180);
    
    // Swap buffers
    glutSwapBuffers();
}

// Reshape callback function
void reshape(int w, int h) {
    win_width = w;
    win_height = h;
    
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
}

// Timer callback function for animation
void timer(int value) {
    glutPostRedisplay();
    glutTimerFunc(1000, timer, 0);  // Update every second
}

// Initialize OpenGL
void init_opengl(int argc, char *argv[]) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(win_width, win_height);
    glutCreateWindow("Bakery Management Simulation");
    
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutTimerFunc(1000, timer, 0);
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, win_width, 0, win_height);
}

// Initialize OpenGL visualization
void setup_opengl(int argc, char *argv[], int inventory_shm_id, int prod_status_shm_id,
                BakeryConfig bakery_config) {
    // Attach to shared memory segments
    inventory = (Inventory *) shmat(inventory_shm_id, NULL, 0);
    prod_status = (ProductionStatus *) shmat(prod_status_shm_id, NULL, 0);
    
    if (inventory == (void *) -1 || prod_status == (void *) -1) {
        perror("Visualization: Failed to attach to shared memory");
        return;
    }
    
    // Store the bakery configuration
    config = bakery_config;
    
    // Initialize and start the OpenGL visualization
    init_opengl(argc, argv);
    glutMainLoop();
}