///////////////////////////////////////////////////////////////////////////////
// ViewManager.h
// ============
// Manages viewing of 3D objects: camera, projection, and input handling
//
// Supports switching between perspective (3D) and orthographic (2D) projections.
//
// AUTHOR: Updated for CS-330, 2026
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "ShaderManager.h"
#include "camera.h"

/***********************************************************
 *  ViewManager
 *
 *  Handles camera, projection, input, and sending
 *  matrices to the shader.
 ***********************************************************/
class ViewManager
{
public:
    // constructor
    ViewManager(ShaderManager* pShaderManager);
    // destructor
    ~ViewManager();

    // Create the GLFW window and initialize OpenGL context
    GLFWwindow* CreateDisplayWindow(const char* windowTitle);

    // Update the view and projection matrices, handle input
    void PrepareSceneView();

    // Static callback for mouse movement
    static void Mouse_Position_Callback(GLFWwindow* window, double xPos, double yPos);

private:
    // Process keyboard events each frame
    void ProcessKeyboardEvents();

    // Pointer to shader manager
    ShaderManager* m_pShaderManager;

    // Pointer to GLFW window
    GLFWwindow* m_pWindow;

    // Track if orthographic projection is active
    bool bOrthographicProjection;
};
