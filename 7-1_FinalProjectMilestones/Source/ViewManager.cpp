///////////////////////////////////////////////////////////////////////////////
// ViewManager.cpp
// ============
// Manages viewing of 3D objects: camera, projection, and input handling
//
// Supports switching between perspective (3D) and orthographic (2D) projections.
//
// AUTHOR: Updated for CS-330, 2026
///////////////////////////////////////////////////////////////////////////////

#include "ViewManager.h"

// GLM Math Header inclusions
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>    

// Namespace for internal globals
namespace
{
    const int WINDOW_WIDTH = 1000;
    const int WINDOW_HEIGHT = 800;

    // Camera pointer for 3D scene interaction
    Camera* g_pCamera = nullptr;

    // Mouse tracking
    float gLastX = WINDOW_WIDTH / 2.0f;
    float gLastY = WINDOW_HEIGHT / 2.0f;
    bool gFirstMouse = true;

    // Frame timing
    float gDeltaTime = 0.0f;
    float gLastFrame = 0.0f;

    // Tracks current projection mode so the mouse callback can check it
    bool g_bOrthographic = false;
}

/***********************************************************
 *  Constructor
 ***********************************************************/
ViewManager::ViewManager(ShaderManager* pShaderManager)
    : bOrthographicProjection(false) // Initialize member
{
    m_pShaderManager = pShaderManager;
    m_pWindow = nullptr;

    g_pCamera = new Camera();
    g_pCamera->Position = glm::vec3(0.5f, 5.5f, 10.0f);
    g_pCamera->Front = glm::vec3(0.0f, -0.5f, -2.0f);
    g_pCamera->Up = glm::vec3(0.0f, 1.0f, 0.0f);
    g_pCamera->Zoom = 80;
    g_pCamera->MovementSpeed = 10;
}

/***********************************************************
 *  Destructor
 ***********************************************************/
ViewManager::~ViewManager()
{
    if (g_pCamera != nullptr)
    {
        delete g_pCamera;
        g_pCamera = nullptr;
    }
    m_pShaderManager = nullptr;
    m_pWindow = nullptr;
}

/***********************************************************
 *  CreateDisplayWindow
 ***********************************************************/
GLFWwindow* ViewManager::CreateDisplayWindow(const char* windowTitle)
{
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, windowTitle, nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);

    // Set mouse callback
    glfwSetCursorPosCallback(window, &ViewManager::Mouse_Position_Callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_pWindow = window;
    return window;
}

/***********************************************************
 *  Mouse_Position_Callback
 ***********************************************************/
void ViewManager::Mouse_Position_Callback(GLFWwindow* window, double xMousePos, double yMousePos)
{
    // In orthographic mode, mouse look is disabled to avoid view conflicts
    if (g_bOrthographic)
        return;

    if (gFirstMouse)
    {
        gLastX = xMousePos;
        gLastY = yMousePos;
        gFirstMouse = false;
    }

    float xOffset = xMousePos - gLastX;
    float yOffset = gLastY - yMousePos; // reversed: y goes bottom-to-top

    gLastX = xMousePos;
    gLastY = yMousePos;

    g_pCamera->ProcessMouseMovement(xOffset, yOffset);
}

/***********************************************************
 *  ProcessKeyboardEvents
 ***********************************************************/
void ViewManager::ProcessKeyboardEvents()
{
    if (!m_pWindow || !g_pCamera) return;

    // Close window
    if (glfwGetKey(m_pWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(m_pWindow, true);

    // Camera movement
    if (glfwGetKey(m_pWindow, GLFW_KEY_W) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(FORWARD, gDeltaTime);
    if (glfwGetKey(m_pWindow, GLFW_KEY_S) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(BACKWARD, gDeltaTime);
    if (glfwGetKey(m_pWindow, GLFW_KEY_A) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(LEFT, gDeltaTime);
    if (glfwGetKey(m_pWindow, GLFW_KEY_D) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(RIGHT, gDeltaTime);
    if (glfwGetKey(m_pWindow, GLFW_KEY_Q) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(UP, gDeltaTime);
    if (glfwGetKey(m_pWindow, GLFW_KEY_E) == GLFW_PRESS)
        g_pCamera->ProcessKeyboard(DOWN, gDeltaTime);

    // Projection switching — reset gFirstMouse when toggling so the camera
    // doesn't jump from stale mouse coordinates when returning to perspective
    if (glfwGetKey(m_pWindow, GLFW_KEY_O) == GLFW_PRESS && !bOrthographicProjection)
    {
        bOrthographicProjection = true;
        g_bOrthographic = true;
        gFirstMouse = true;
    }
    if (glfwGetKey(m_pWindow, GLFW_KEY_P) == GLFW_PRESS && bOrthographicProjection)
    {
        bOrthographicProjection = false;
        g_bOrthographic = false;
        gFirstMouse = true; // Prevent jump when re-enabling mouse look
    }
}

/***********************************************************
 *  PrepareSceneView
 ***********************************************************/
void ViewManager::PrepareSceneView()
{
    if (!m_pWindow) return;

    // Timing
    float currentFrame = glfwGetTime();
    gDeltaTime = currentFrame - gLastFrame;
    gLastFrame = currentFrame;

    // Handle keyboard input
    ProcessKeyboardEvents();

    // View matrix from camera
    glm::mat4 view = g_pCamera->GetViewMatrix();

    // Projection
    glm::mat4 projection;
    if (bOrthographicProjection)
    {
        // Use a fixed orthographic volume that frames the scene.
        // Do NOT touch the camera position here — overriding it every frame
        // conflicts with the mouse callback and freezes mouse control.
        float scale = 10.0f;
        projection = glm::ortho(
            -scale, scale,
            -scale, scale,
            0.1f, 100.0f);
    }
    else
    {
        projection = glm::perspective(glm::radians(g_pCamera->Zoom),
                                      (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT,
                                      0.1f, 100.0f);
    }

    // Send matrices and camera position to shader
    if (m_pShaderManager)
    {
        m_pShaderManager->setMat4Value("view", view);
        m_pShaderManager->setMat4Value("projection", projection);
        m_pShaderManager->setVec3Value("viewPosition", g_pCamera->Position);
    }
}