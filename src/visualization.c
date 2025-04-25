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

// Enhanced colors for different elements
float colors[7][3] = {
    {0.95f, 0.8f, 0.6f},  // Bread (warm brown)
    {0.85f, 0.7f, 0.5f},  // Sandwich (tan)
    {0.9f, 0.5f, 0.5f},   // Cake (soft red)
    {1.0f, 0.7f, 0.8f},   // Sweet (soft pink)
    {0.8f, 0.6f, 0.4f},   // Sweet Patisserie (caramel)
    {0.7f, 0.75f, 0.45f}, // Savory Patisserie (olive green)
    {0.95f, 0.95f, 0.8f}  // Paste (cream)
};

// Background panel colors
float panel_bg[4] = {0.2f, 0.2f, 0.25f, 1.0f};
float header_bg[4] = {0.3f, 0.3f, 0.35f, 1.0f};
float highlight_color[3] = {1.0f, 0.8f, 0.2f}; // Gold highlight

// Window properties
int win_width = 900;
int win_height = 700;

// Function to draw text on the screen with different sizes
void renderText(float x, float y, const char *text, void *font) {
    glRasterPos2f(x, y);
    
    for (const char *c = text; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

// Helper function to draw a rounded rectangle
void drawRoundedRect(float x, float y, float width, float height, float radius) {
    int segments = 20;
    float angle;
    
    glBegin(GL_POLYGON);
    
    // Top-left corner
    for (int i = 0; i <= segments; i++) {
        angle = M_PI / 2 + i * M_PI / (2 * segments);
        glVertex2f(x + radius + radius * cos(angle), y - radius + radius * sin(angle));
    }
    
    // Top-right corner
    for (int i = 0; i <= segments; i++) {
        angle = i * M_PI / (2 * segments);
        glVertex2f(x + width - radius + radius * cos(angle), y - radius + radius * sin(angle));
    }
    
    // Bottom-right corner
    for (int i = 0; i <= segments; i++) {
        angle = 3 * M_PI / 2 + i * M_PI / (2 * segments);
        glVertex2f(x + width - radius + radius * cos(angle), y - height + radius + radius * sin(angle));
    }
    
    // Bottom-left corner
    for (int i = 0; i <= segments; i++) {
        angle = M_PI + i * M_PI / (2 * segments);
        glVertex2f(x + radius + radius * cos(angle), y - height + radius + radius * sin(angle));
    }
    
    glEnd();
}

// Draw a panel with a title
void drawPanel(float x, float y, float width, float height, const char *title) {
    // Draw panel background
    glColor4fv(panel_bg);
    drawRoundedRect(x, y, width, height, 10.0f);
    
    // Draw header
    glColor4fv(header_bg);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y - 30);
    glVertex2f(x, y - 30);
    glEnd();
    
    // Draw title
    glColor3fv(highlight_color);
    renderText(x + 10, y - 20, title, GLUT_BITMAP_HELVETICA_18);
    
    // Draw border
    glLineWidth(2.0f);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y - height);
    glVertex2f(x, y - height);
    glEnd();
    glLineWidth(1.0f);
}