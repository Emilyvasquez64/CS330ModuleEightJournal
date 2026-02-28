///////////////////////////////////////////////////////////////////////////////
// SceneManager.cpp
// ============
// Handles all the setup and drawing for the 3D scene —
// textures, materials, lights, and every object you see on screen.
//
// AUTHOR: Brian Battersby - SNHU Instructor / Computer Science
// Updated by Emily V Feb 2026
///////////////////////////////////////////////////////////////////////////////

#include "SceneManager.h"

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

#include <glm/gtx/transform.hpp>
#include <iostream>
#include <cmath>

// Shader uniform name strings — stored here so we're not hardcoding
// the same string literals all over the place
namespace {
    const char* g_ModelName = "model";
    const char* g_ColorValueName = "objectColor";
    const char* g_TextureValueName = "objectTexture";
    const char* g_UseTextureName = "bUseTexture";
    const char* g_UseLightingName = "bUseLighting";
}

/***********************************************************
 * SceneManager()
 * Constructor — hooks up the shader manager and allocates
 * the mesh helper. Texture count starts at zero.
 ***********************************************************/
SceneManager::SceneManager(ShaderManager *pShaderManager)
{
    m_pShaderManager = pShaderManager;
    m_basicMeshes = new ShapeMeshes();
    m_loadedTextures = 0;
}

/***********************************************************
 * ~SceneManager()
 * Destructor — cleans up the mesh helper so we don't leak
 * memory when the scene gets torn down.
 ***********************************************************/
SceneManager::~SceneManager()
{
    m_pShaderManager = nullptr;
    delete m_basicMeshes;
    m_basicMeshes = nullptr;
}

/***********************************************************
 * CreateGLTexture()
 * Loads an image file from disk and uploads it to the GPU
 * as an OpenGL texture. The texture gets stored with a
 * string tag so we can look it up by name later.
 * Returns true if the load worked, false if it didn't.
 ***********************************************************/
bool SceneManager::CreateGLTexture(const char* filename, std::string tag)
{
    int width = 0;
    int height = 0;
    int colorChannels = 0;
    GLuint textureID = 0;

    // Flip the image vertically — OpenGL's UV origin is bottom-left,
    // but most image formats start from the top-left
    stbi_set_flip_vertically_on_load(true);
    unsigned char* image = stbi_load(filename, &width, &height, &colorChannels, 0);

    if (image)
    {
        std::cout << "Successfully loaded image:" << filename 
                  << ", width:" << width << ", height:" << height 
                  << ", channels:" << colorChannels << std::endl;

        // Generate and bind a new texture slot on the GPU
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // GL_REPEAT tiles the texture when UVs go past 1.0
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        // GL_LINEAR gives smooth interpolation instead of a blocky pixelated look
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Upload pixel data — handle both RGB and RGBA images
        if (colorChannels == 3)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
        else if (colorChannels == 4)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        else
        {
            // Anything other than RGB or RGBA isn't supported right now
            std::cout << "Not implemented to handle image with " << colorChannels << " channels" << std::endl;
            stbi_image_free(image);
            return false;
        }

        // Build mipmaps so the texture looks good at different distances
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(image); // done with CPU-side pixel data
        glBindTexture(GL_TEXTURE_2D, 0); // unbind to keep state clean

        // Save the texture ID and tag for later lookup
        m_textureIDs[m_loadedTextures].ID = textureID;
        m_textureIDs[m_loadedTextures].tag = tag;
        m_loadedTextures++;

        return true;
    }

    std::cout << "Could not load image:" << filename << std::endl;
    return false;
}

/***********************************************************
 * BindGLTextures()
 * Activates every loaded texture on its own texture unit
 * so the shader can sample all of them at once.
 ***********************************************************/
void SceneManager::BindGLTextures()
{
    for (int i = 0; i < m_loadedTextures; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_textureIDs[i].ID);
    }
}

/***********************************************************
 * DestroyGLTextures()
 * Frees all GPU texture memory — call this on shutdown
 * so we're not leaving anything sitting on the GPU.
 ***********************************************************/
void SceneManager::DestroyGLTextures()
{
    for (int i = 0; i < m_loadedTextures; i++)
    {
        glDeleteTextures(1, &m_textureIDs[i].ID);
    }
}

/***********************************************************
 * FindTextureID()
 * Looks up a texture's OpenGL ID by its string tag.
 * Returns -1 if the tag doesn't match anything loaded.
 ***********************************************************/
int SceneManager::FindTextureID(std::string tag)
{
    for (int i = 0; i < m_loadedTextures; i++)
    {
        if (m_textureIDs[i].tag == tag)
            return m_textureIDs[i].ID;
    }
    return -1;
}

/***********************************************************
 * FindTextureSlot()
 * Returns the texture unit index (slot number) for a tag.
 * The shader needs the slot number, not the raw GL ID.
 * Returns -1 if not found.
 ***********************************************************/
int SceneManager::FindTextureSlot(std::string tag)
{
    for (int i = 0; i < m_loadedTextures; i++)
    {
        if (m_textureIDs[i].tag == tag)
            return i;
    }
    return -1;
}

/***********************************************************
 * FindMaterial()
 * Searches the material list by tag and fills in the output
 * struct if found. Returns true on success, false if the
 * tag doesn't exist.
 ***********************************************************/
bool SceneManager::FindMaterial(std::string tag, OBJECT_MATERIAL& material)
{
    for (auto& mat : m_objectMaterials)
    {
        if (mat.tag == tag)
        {
            material = mat;
            return true;
        }
    }
    return false;
}

/***********************************************************
 * SetTransformations()
 * Builds the model matrix from scale, rotation (XYZ order),
 * and translation, then pushes it to the shader.
 * Call this before every draw call to position the mesh.
 ***********************************************************/
void SceneManager::SetTransformations(
    glm::vec3 scaleXYZ,
    float XrotationDegrees,
    float YrotationDegrees,
    float ZrotationDegrees,
    glm::vec3 positionXYZ)
{
    // TRS order: translate * rotateZ * rotateY * rotateX * scale
    glm::mat4 modelView = glm::translate(positionXYZ)
                        * glm::rotate(glm::radians(ZrotationDegrees), glm::vec3(0,0,1))
                        * glm::rotate(glm::radians(YrotationDegrees), glm::vec3(0,1,0))
                        * glm::rotate(glm::radians(XrotationDegrees), glm::vec3(1,0,0))
                        * glm::scale(scaleXYZ);

    if (m_pShaderManager != nullptr)
        m_pShaderManager->setMat4Value(g_ModelName, modelView);
}

/***********************************************************
 * SetShaderColor()
 * Switches the shader to flat-color mode and sets the RGBA
 * color for the next draw call. Disables texture sampling.
 ***********************************************************/
void SceneManager::SetShaderColor(
    float redColorValue,
    float greenColorValue,
    float blueColorValue,
    float alphaValue)
{
    glm::vec4 color(redColorValue, greenColorValue, blueColorValue, alphaValue);

    if (m_pShaderManager != nullptr)
    {
        m_pShaderManager->setIntValue(g_UseTextureName, false);
        m_pShaderManager->setVec4Value(g_ColorValueName, color);
    }
}

/***********************************************************
 * SetShaderTexture()
 * Switches the shader to texture mode and tells it which
 * texture slot to sample from, looked up by tag name.
 ***********************************************************/
void SceneManager::SetShaderTexture(std::string textureTag)
{
    if (m_pShaderManager != nullptr)
    {
        m_pShaderManager->setIntValue(g_UseTextureName, true);
        int textureID = FindTextureSlot(textureTag);
        m_pShaderManager->setSampler2DValue(g_TextureValueName, textureID);
    }
}

/***********************************************************
 * SetTextureUVScale()
 * Sets how many times the texture tiles across a surface.
 * Bump up U or V to make the texture repeat more often.
 ***********************************************************/
void SceneManager::SetTextureUVScale(float u, float v)
{
    if (m_pShaderManager != nullptr)
    {
        m_pShaderManager->setVec2Value("UVscale", glm::vec2(u,v));
    }
}

/***********************************************************
 * SetShaderMaterial()
 * Looks up a material by tag and pushes its diffuse color,
 * specular color, and shininess to the shader for Phong
 * lighting calculations.
 ***********************************************************/
void SceneManager::SetShaderMaterial(std::string materialTag)
{
    OBJECT_MATERIAL material;
    if (FindMaterial(materialTag, material))
    {
        m_pShaderManager->setVec3Value("material.diffuseColor", material.diffuseColor);
        m_pShaderManager->setVec3Value("material.specularColor", material.specularColor);
        m_pShaderManager->setFloatValue("material.shininess", material.shininess);
    }
}

/***********************************************************
 * DefineObjectMaterials()
 * Defines all the Phong materials used in the scene.
 * Each material controls how shiny or matte a surface looks
 * under the lights — tweak these to change the vibe.
 ***********************************************************/
void SceneManager::DefineObjectMaterials()
{
    // Matte gray for the flower pot — zero gloss, flat surface
    OBJECT_MATERIAL grayMatte;
    grayMatte.tag = "grayMatte";
    grayMatte.diffuseColor = glm::vec3(0.45f, 0.45f, 0.45f);
    grayMatte.specularColor = glm::vec3(0.1f, 0.1f, 0.1f);
    grayMatte.shininess = 2.0f;
    m_objectMaterials.push_back(grayMatte);

    // Off-white ceramic for the mug — glazed but not super shiny
    OBJECT_MATERIAL ceramic;
    ceramic.tag = "ceramic";
    ceramic.diffuseColor  = glm::vec3(0.95f, 0.93f, 0.90f);
    ceramic.specularColor = glm::vec3(0.20f, 0.20f, 0.19f); // soft sheen, avoids harsh glare
    ceramic.shininess     = 12.0f;                           // wide highlight = gentler look
    m_objectMaterials.push_back(ceramic);

    // Warm brown wood for the napkin holder panels
    OBJECT_MATERIAL wood;
    wood.tag = "wood";
    wood.diffuseColor = glm::vec3(0.6f, 0.4f, 0.2f);
    wood.specularColor = glm::vec3(0.15f, 0.1f, 0.05f);
    wood.shininess = 8.0f;
    m_objectMaterials.push_back(wood);

    // Slightly richer wood for the arch tops of the napkin holder
    OBJECT_MATERIAL woodie;
    woodie.tag = "woodie";
    woodie.diffuseColor  = glm::vec3(0.55f, 0.35f, 0.18f);
    woodie.specularColor = glm::vec3(0.2f, 0.15f, 0.08f);
    woodie.shininess     = 12.0f;
    m_objectMaterials.push_back(woodie);

    // Light natural wood for the coasters — pale, low sheen
    OBJECT_MATERIAL lightWood;
    lightWood.tag = "lightWood";
    lightWood.diffuseColor = glm::vec3(0.75f, 0.65f, 0.50f);
    lightWood.specularColor = glm::vec3(0.1f, 0.1f, 0.08f);
    lightWood.shininess = 4.0f;
    m_objectMaterials.push_back(lightWood);

    // Dark brushed metal for the coaster wire holder frame
    OBJECT_MATERIAL darkMetal;
    darkMetal.tag = "darkMetal";
    darkMetal.diffuseColor = glm::vec3(0.08f, 0.08f, 0.08f);
    darkMetal.specularColor = glm::vec3(0.6f, 0.6f, 0.6f); // metals reflect a lot
    darkMetal.shininess = 32.0f;
    m_objectMaterials.push_back(darkMetal);

    // Polished stone / lacquered counter surface — high gloss
    OBJECT_MATERIAL counter;
    counter.tag = "counter";
    counter.diffuseColor  = glm::vec3(0.82f, 0.78f, 0.72f);
    counter.specularColor = glm::vec3(0.92f, 0.90f, 0.88f);
    counter.shininess     = 128.0f;
    m_objectMaterials.push_back(counter);

    // Upper table top — maximum gloss, looks like a lacquered resin surface
    OBJECT_MATERIAL tableTop;
    tableTop.tag = "tableTop";
    tableTop.diffuseColor  = glm::vec3(0.80f, 0.76f, 0.70f);
    tableTop.specularColor = glm::vec3(0.98f, 0.97f, 0.95f); // near-white highlight
    tableTop.shininess     = 256.0f;                          // tightest possible specular
    m_objectMaterials.push_back(tableTop);

    // Generic metal — kept around for anything that needs a standard metallic look
    OBJECT_MATERIAL metal;
    metal.tag = "metal";
    metal.diffuseColor = glm::vec3(0.5f, 0.5f, 0.5f);
    metal.specularColor = glm::vec3(0.6f, 0.6f, 0.6f);
    metal.shininess = 24.0f;
    m_objectMaterials.push_back(metal);

    // Ficus bonsai leaves — bright medium green with a subtle waxy sheen.
    // The reference photo shows a fairly vivid, well-lit green, not dark.
    // Diffuse is the main driver of perceived colour so we push it up.
    OBJECT_MATERIAL foliage;
    foliage.tag = "foliage";
    foliage.diffuseColor  = glm::vec3(0.16f, 0.46f, 0.14f); // bright medium-green
    foliage.specularColor = glm::vec3(0.12f, 0.22f, 0.10f); // mild waxy highlight
    foliage.shininess     = 14.0f;
    m_objectMaterials.push_back(foliage);

    // Dark brown bark for the tree trunk — rough and matte
    OBJECT_MATERIAL bark;
    bark.tag = "bark";
    bark.diffuseColor = glm::vec3(0.30f, 0.24f, 0.18f);
    bark.specularColor = glm::vec3(0.05f, 0.04f, 0.03f);
    bark.shininess = 2.0f;
    m_objectMaterials.push_back(bark);

    // Dark earthy soil — no shine at all, just flat brown
    OBJECT_MATERIAL soil;
    soil.tag = "soil";
    soil.diffuseColor = glm::vec3(0.25f, 0.18f, 0.10f);
    soil.specularColor = glm::vec3(0.02f, 0.02f, 0.02f);
    soil.shininess = 1.0f;
    m_objectMaterials.push_back(soil);

    // Bright white paper napkins — just a tiny bit of sheen
    OBJECT_MATERIAL napkin;
    napkin.tag = "napkin";
    napkin.diffuseColor = glm::vec3(0.95f, 0.95f, 0.93f);
    napkin.specularColor = glm::vec3(0.10f, 0.10f, 0.10f);
    napkin.shininess = 4.0f;
    m_objectMaterials.push_back(napkin);

    // Warm cream/beige paint for the back wall — diffuse kept low so the
    // wall reads as a naturally dim background behind the lit foreground objects
    OBJECT_MATERIAL wall;
    wall.tag = "wall";
    wall.diffuseColor  = glm::vec3(0.95f, 0.95f, 0.90f); // near-white — lets texture show true color without yellow tint
    wall.specularColor = glm::vec3(0.02f, 0.02f, 0.02f); // nearly no specular — keep wall flat/matte
    wall.shininess     = 1.0f;
    m_objectMaterials.push_back(wall);

    // Bright white shaker-style cabinets — slight sheen from paint
    OBJECT_MATERIAL cabinetWhite;
    cabinetWhite.tag = "cabinetWhite";
    cabinetWhite.diffuseColor  = glm::vec3(0.78f, 0.78f, 0.77f); // slightly dimmed so cabinets don't blow out
    cabinetWhite.specularColor = glm::vec3(0.12f, 0.12f, 0.11f);
    cabinetWhite.shininess     = 12.0f;
    m_objectMaterials.push_back(cabinetWhite);

    // Brushed stainless steel for the fridge body
    OBJECT_MATERIAL stainless;
    stainless.tag = "stainless";
    stainless.diffuseColor  = glm::vec3(0.40f, 0.40f, 0.41f); // kept darker so fridge reads as background
    stainless.specularColor = glm::vec3(0.55f, 0.55f, 0.56f);
    stainless.shininess     = 48.0f;
    m_objectMaterials.push_back(stainless);

    // Dark brushed bar handles on the fridge doors
    OBJECT_MATERIAL fridgeHandle;
    fridgeHandle.tag = "fridgeHandle";
    fridgeHandle.diffuseColor  = glm::vec3(0.14f, 0.14f, 0.14f);
    fridgeHandle.specularColor = glm::vec3(0.35f, 0.35f, 0.35f);
    fridgeHandle.shininess     = 32.0f;
    m_objectMaterials.push_back(fridgeHandle);
}

/***********************************************************
 * SetupSceneLights()
 * Sets up a natural daylight rig that matches the reference
 * photo — warm sunlight from the front-left window plus
 * a few fill lights so shadows don't go pure black.
 *
 * Light breakdown:
 *   Directional — main sun/window from front-left (~5500 K)
 *   Point 0     — key light reinforcing the window glow
 *   Point 1     — cool blue sky fill lifting shadow areas
 *   Point 2     — warm bounce off the counter surface
 *   Point 3     — soft overhead fill so tops aren't pitch dark
 ***********************************************************/
void SceneManager::SetupSceneLights()
{
    // Turn on Phong shading in the fragment shader
    m_pShaderManager->setBoolValue(g_UseLightingName, true);

    // --- Directional light (sun through front-left window) ---
    // Ray travels: right (+X), slightly down (-Y), slightly into scene (-Z).
    // Left-facing surfaces get bright, right-facing surfaces get shadow — matches photo.
    m_pShaderManager->setBoolValue("directionalLight.bActive", true);
    m_pShaderManager->setVec3Value("directionalLight.direction", 1.0f, -0.55f, -0.40f);
    m_pShaderManager->setVec3Value("directionalLight.ambient",  0.07f, 0.06f, 0.06f); // reduced — less ambient wash on back wall
    m_pShaderManager->setVec3Value("directionalLight.diffuse",  0.24f, 0.23f, 0.22f); // trimmed further to dim back wall
    m_pShaderManager->setVec3Value("directionalLight.specular", 0.10f, 0.10f, 0.10f); // soft highlights

    // --- Point light 0 — front-left key light (warm window glow) ---
    // Far left and in front — drives specular highlights on the table top and mug
    m_pShaderManager->setBoolValue("pointLights[0].bActive",  true);
    m_pShaderManager->setVec3Value("pointLights[0].position", -14.0f, 9.0f, 18.0f);
    m_pShaderManager->setVec3Value("pointLights[0].ambient",  0.08f, 0.07f, 0.07f); // raised — more front fill
    m_pShaderManager->setVec3Value("pointLights[0].diffuse",  0.50f, 0.48f, 0.46f); // doubled — brighter front scene
    m_pShaderManager->setVec3Value("pointLights[0].specular", 0.14f, 0.13f, 0.12f); // stronger highlights

    // --- Point light 1 — cool sky fill (~7000 K) ---
    // Simulates scattered blue-sky light lifting the shadow sides of objects
    m_pShaderManager->setBoolValue("pointLights[1].bActive",  true);
    m_pShaderManager->setVec3Value("pointLights[1].position", -8.0f, 16.0f, 6.0f);
    m_pShaderManager->setVec3Value("pointLights[1].ambient",  0.05f, 0.06f, 0.08f);
    m_pShaderManager->setVec3Value("pointLights[1].diffuse",  0.18f, 0.21f, 0.26f); // cool blue tint
    m_pShaderManager->setVec3Value("pointLights[1].specular", 0.05f, 0.06f, 0.08f);

    // --- Point light 2 — warm counter bounce (front, low) ---
    // Mimics light bouncing off the pale counter toward the camera side of objects
    m_pShaderManager->setBoolValue("pointLights[2].bActive",  true);
    m_pShaderManager->setVec3Value("pointLights[2].position", 0.0f, 3.0f, 14.0f);
    m_pShaderManager->setVec3Value("pointLights[2].ambient",  0.07f, 0.07f, 0.06f); // raised — lifts front shadows
    m_pShaderManager->setVec3Value("pointLights[2].diffuse",  0.38f, 0.37f, 0.35f); // more than doubled — fills front faces
    m_pShaderManager->setVec3Value("pointLights[2].specular", 0.12f, 0.12f, 0.11f); // brighter gloss on counter/mug

    // --- Point light 3 — soft overhead fill (ceiling bounce) ---
    // Keeps the tops of objects from going completely dark
    // Diffuse pulled way back so the overhead angle doesn't over-brighten the back wall
    m_pShaderManager->setBoolValue("pointLights[3].bActive",  true);
    m_pShaderManager->setVec3Value("pointLights[3].position", -2.0f, 18.0f, 2.0f);
    m_pShaderManager->setVec3Value("pointLights[3].ambient",  0.05f, 0.05f, 0.05f); // reduced from 0.12
    m_pShaderManager->setVec3Value("pointLights[3].diffuse",  0.18f, 0.18f, 0.18f); // reduced from 0.45
    m_pShaderManager->setVec3Value("pointLights[3].specular", 0.04f, 0.04f, 0.04f); // reduced from 0.08

    // Turn off lights we're not using
    m_pShaderManager->setBoolValue("pointLights[4].bActive", false);
    m_pShaderManager->setBoolValue("spotLight.bActive",      false);
}

/***********************************************************
 * PrepareScene()
 * One-time setup — loads materials and all mesh types we
 * need for the scene. Call this before RenderScene().
 ***********************************************************/
void SceneManager::PrepareScene()
{
    // Set up all Phong materials
    DefineObjectMaterials();

    // Pre-load every mesh shape used anywhere in the scene
    m_basicMeshes->LoadPlaneMesh();           // flat surfaces (counter, shelf)
    m_basicMeshes->LoadBoxMesh();             // table body, napkin holder panels
    m_basicMeshes->LoadTaperedCylinderMesh(); // flower pot body
    m_basicMeshes->LoadCylinderMesh();        // mug body, pot rim, wire legs
    m_basicMeshes->LoadTorusMesh();           // mug handle
    m_basicMeshes->LoadSphereMesh();          // foliage clusters, wire arch tops
}

/***********************************************************
 * LoadSceneTextures()
 * Loads all image files from the textures folder and binds
 * them to GPU texture units. Must be called before rendering.
 ***********************************************************/
void SceneManager::LoadSceneTextures()
{
    // Each call loads an image and tags it with a short name
    // we reference later when drawing objects
    CreateGLTexture("textures/pot.jpg", "pot");
    CreateGLTexture("textures/wood.jpg", "wood");
    CreateGLTexture("textures/woodie.jpg", "woodie");
    CreateGLTexture("textures/coaster.jpg", "coaster");
    CreateGLTexture("textures/toptable.jpg", "toptable");
    CreateGLTexture("textures/bottomtable.jpg", "bottomtable");
    CreateGLTexture("textures/napkin.jpg", "napkin");
    CreateGLTexture("textures/wall.jpg", "wall");

    // Activate all loaded textures on their respective GPU texture units
    BindGLTextures();
}

/***********************************************************
 * RenderScene()
 * Draws the full kitchen counter scene every frame.
 * Scene layout:
 *   Upper counter — gray flower pot with bonsai tree
 *   Lower shelf   — candle mug, coasters in wire holder,
 *                   wooden napkin holder
 ***********************************************************/
void SceneManager::RenderScene()
{
    // Set up all lights before drawing anything
    SetupSceneLights();

    // Disable backface culling for the whole scene so open-ended
    // cylinders and tapered shapes don't have missing faces
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    // Reusable transform variables — set these before every draw call
    glm::vec3 scaleXYZ;
    float XrotationDegrees = 0.0f;
    float YrotationDegrees = 0.0f;
    float ZrotationDegrees = 0.0f;
    glm::vec3 positionXYZ;

    // Y heights for the two shelf levels — everything derives from these two values
    const float upperTableY = 1.0f;   // top surface of the upper counter
    const float lowerShelfY = -0.5f;  // top surface of the lower shelf

    /******************************************************************/
    //  BACKGROUND — drawn first so everything else renders on top
    //
    //  Mirrors the reference photo:
    //    - Cream back wall spanning the full width
    //    - Tall pantry cabinet column on the left
    //    - Two upper cabinet doors centered above the fridge
    //    - Whirlpool French-door fridge in the center
    //    - Plain wall section on the right with light-switch plate
    //    - Dark hardwood floor strip at the base
    /******************************************************************/
    {
        const float bgZ    = -11.0f;
        const float floorY = -4.0f;
        const float ceilY  = 16.0f;
        const float wallH  = ceilY - floorY;
        const float wallW  = 60.0f;
        const float cabZ   = bgZ + 0.35f;

        // Back wall
        scaleXYZ    = glm::vec3(wallW, wallH, 0.3f);
        positionXYZ = glm::vec3(0.0f, floorY + wallH * 0.5f, bgZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderTexture("wall");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wall");
        m_basicMeshes->DrawBoxMesh();

        // Dark hardwood floor strip
        scaleXYZ    = glm::vec3(wallW, 0.3f, 6.0f);
        positionXYZ = glm::vec3(0.0f, floorY + 0.15f, bgZ + 3.0f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.16f, 0.11f, 0.07f, 1.0f);
        SetShaderMaterial("bark");
        m_basicMeshes->DrawBoxMesh();

        // Ceiling strip
        scaleXYZ    = glm::vec3(wallW, 1.5f, 4.0f);
        positionXYZ = glm::vec3(0.0f, ceilY - 0.75f, bgZ + 2.0f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderTexture("wall");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wall");
        m_basicMeshes->DrawBoxMesh();

        // ── LEFT PANTRY CABINET COLUMN ────────────────────────────────
        const float cabW = 5.5f;
        const float cabX = -9.5f;

        // Upper pantry body
        scaleXYZ    = glm::vec3(cabW, 7.5f, 0.7f);
        positionXYZ = glm::vec3(cabX, 8.5f, cabZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Lower pantry body
        scaleXYZ    = glm::vec3(cabW, 7.0f, 0.7f);
        positionXYZ = glm::vec3(cabX, 0.5f, cabZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Upper door inset panel
        scaleXYZ    = glm::vec3(cabW * 0.80f, 6.8f, 0.12f);
        positionXYZ = glm::vec3(cabX, 8.5f, cabZ + 0.41f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.72f, 0.72f, 0.71f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Lower door inset panel
        scaleXYZ    = glm::vec3(cabW * 0.80f, 6.3f, 0.12f);
        positionXYZ = glm::vec3(cabX, 0.5f, cabZ + 0.41f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.72f, 0.72f, 0.71f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Mid-rail between upper/lower pantry doors
        scaleXYZ    = glm::vec3(cabW, 0.25f, 0.75f);
        positionXYZ = glm::vec3(cabX, 4.2f, cabZ + 0.05f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Handles — upper and lower pantry doors
        for (int d = 0; d < 2; d++)
        {
            float handleY = (d == 0) ? 8.5f : 0.5f;
            scaleXYZ    = glm::vec3(0.12f, 1.2f, 0.12f);
            positionXYZ = glm::vec3(cabX + 1.8f, handleY, cabZ + 0.54f);
            SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
            SetShaderColor(0.55f, 0.55f, 0.55f, 1.0f);
            SetShaderMaterial("metal");
            m_basicMeshes->DrawCylinderMesh(false, false, true);
        }

        // ── FRIDGE SURROUND + UPPER CABINET ───────────────────────────
        const float fridgeW    = 5.2f;
        const float fridgeX    = -1.0f;
        const float fridgeBotY = floorY + 0.3f;
        const float fridgeH    = 13.5f;  // taller fridge (was 10.5)
        const float fridgeTopY = fridgeBotY + fridgeH;

        // Left surround pilaster
        scaleXYZ    = glm::vec3(1.2f, fridgeH + 2.3f, 0.9f);  // taller to match fridge + bigger upper cab
        positionXYZ = glm::vec3(fridgeX - fridgeW * 0.5f - 0.6f,
                                fridgeBotY + (fridgeH + 2.3f) * 0.5f, cabZ - 0.05f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Right surround pilaster
        scaleXYZ    = glm::vec3(1.2f, fridgeH + 2.3f, 0.9f);  // taller to match
        positionXYZ = glm::vec3(fridgeX + fridgeW * 0.5f + 0.6f,
                                fridgeBotY + (fridgeH + 2.3f) * 0.5f, cabZ - 0.05f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Upper cabinet box above fridge
        const float upCabH = 2.3f;  // matched to reference — ~17% of fridge height (was 5.5)
        const float upCabY = fridgeTopY + upCabH * 0.5f;
        scaleXYZ    = glm::vec3(fridgeW + 2.4f, upCabH, 0.85f);
        positionXYZ = glm::vec3(fridgeX, upCabY, cabZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.78f, 0.78f, 0.77f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Left upper cabinet door inset
        scaleXYZ    = glm::vec3((fridgeW + 2.4f) * 0.46f, upCabH * 0.82f, 0.12f);
        positionXYZ = glm::vec3(fridgeX - (fridgeW + 2.4f) * 0.25f, upCabY, cabZ + 0.48f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.72f, 0.72f, 0.71f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Right upper cabinet door inset
        scaleXYZ    = glm::vec3((fridgeW + 2.4f) * 0.46f, upCabH * 0.82f, 0.12f);
        positionXYZ = glm::vec3(fridgeX + (fridgeW + 2.4f) * 0.25f, upCabY, cabZ + 0.48f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.72f, 0.72f, 0.71f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();

        // Upper cabinet handles
        for (int d = -1; d <= 1; d += 2)
        {
            scaleXYZ    = glm::vec3(0.1f, 0.9f, 0.1f);
            positionXYZ = glm::vec3(fridgeX + d * 0.3f, upCabY -.5f , cabZ + 0.61f);
            SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
            SetShaderColor(0.55f, 0.55f, 0.55f, 1.0f);
            SetShaderMaterial("metal");
            m_basicMeshes->DrawCylinderMesh(false, false, true);
        }

        // ── FRIDGE BODY ───────────────────────────────────────────────
        const float fridgeFaceZ = cabZ + 0.55f;
        const float fridgeDepth = 1.2f;

        // Main stainless body
        scaleXYZ    = glm::vec3(fridgeW, fridgeH, fridgeDepth);
        positionXYZ = glm::vec3(fridgeX, fridgeBotY + fridgeH * 0.5f,
                                fridgeFaceZ - fridgeDepth * 0.5f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.40f, 0.40f, 0.41f, 1.0f);
        SetShaderMaterial("stainless");
        m_basicMeshes->DrawBoxMesh();

        // Vertical door seam
        scaleXYZ    = glm::vec3(0.06f, fridgeH * 0.72f, fridgeDepth + 0.02f);
        positionXYZ = glm::vec3(fridgeX,
                                fridgeBotY + fridgeH * 0.26f + fridgeH * 0.72f * 0.5f,
                                fridgeFaceZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.22f, 0.22f, 0.23f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawBoxMesh();

        // Horizontal seam (upper doors / freezer drawer)
        scaleXYZ    = glm::vec3(fridgeW + 0.05f, 0.08f, fridgeDepth + 0.02f);
        positionXYZ = glm::vec3(fridgeX, fridgeBotY + fridgeH * 0.26f, fridgeFaceZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.20f, 0.20f, 0.21f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawBoxMesh();

        // Left door handle
        scaleXYZ    = glm::vec3(0.14f, 3.8f, 0.14f);
        positionXYZ = glm::vec3(fridgeX - 0.55f, fridgeBotY + fridgeH * 0.62f,
                                fridgeFaceZ + 0.22f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.14f, 0.14f, 0.14f, 1.0f);
        SetShaderMaterial("fridgeHandle");
        m_basicMeshes->DrawBoxMesh();

        // Right door handle
        scaleXYZ    = glm::vec3(0.14f, 3.8f, 0.14f);
        positionXYZ = glm::vec3(fridgeX + 0.55f, fridgeBotY + fridgeH * 0.62f,
                                fridgeFaceZ + 0.22f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.14f, 0.14f, 0.14f, 1.0f);
        SetShaderMaterial("fridgeHandle");
        m_basicMeshes->DrawBoxMesh();

        // Freezer drawer handle (wide horizontal bar)
        scaleXYZ    = glm::vec3(fridgeW * 0.65f, 0.18f, 0.18f);
        positionXYZ = glm::vec3(fridgeX, fridgeBotY + fridgeH * 0.13f,
                                fridgeFaceZ + 0.22f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.14f, 0.14f, 0.14f, 1.0f);
        SetShaderMaterial("fridgeHandle");
        m_basicMeshes->DrawBoxMesh();

        // Fridge feet
        for (int f = -1; f <= 1; f += 2)
        {
            scaleXYZ    = glm::vec3(0.25f, 0.28f, 0.25f);
            positionXYZ = glm::vec3(fridgeX + f * (fridgeW * 0.38f),
                                    fridgeBotY - 0.14f, fridgeFaceZ - 0.4f);
            SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
            SetShaderColor(0.10f, 0.10f, 0.10f, 1.0f);
            SetShaderMaterial("darkMetal");
            m_basicMeshes->DrawCylinderMesh(true, true, true);
        }

        // ── RIGHT WALL SECTION ────────────────────────────────────────
        scaleXYZ    = glm::vec3(8.0f, wallH, 0.3f);
        positionXYZ = glm::vec3(8.0f, floorY + wallH * 0.5f, bgZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderTexture("wall");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wall");
        m_basicMeshes->DrawBoxMesh();

        // Light-switch plate on right wall
        scaleXYZ    = glm::vec3(0.55f, 0.85f, 0.12f);
        positionXYZ = glm::vec3(6.8f, 2.8f, bgZ + 0.22f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.80f, 0.80f, 0.79f, 1.0f);
        SetShaderMaterial("cabinetWhite");
        m_basicMeshes->DrawBoxMesh();
    }
    // ── End of background ──────────────────────────────────────────────

    /******************************************************************/
    //  UPPER COUNTER SLAB
    //  The raised surface that the flower pot sits on.
    /******************************************************************/
    float topThickness = 1.0f; // slab height going upward

    // Top face of the counter slab
    scaleXYZ    = glm::vec3(20.0f, topThickness, 8.0f);
    positionXYZ = glm::vec3(0.0f, upperTableY + topThickness * 0.5f, -3.0f);
    SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
    SetShaderTexture("toptable");
    SetTextureUVScale(1.0f, 1.0f);
    SetShaderMaterial("tableTop"); // maximum gloss lacquer look
    m_basicMeshes->DrawBoxMesh();

    // Vertical front face panel between the upper and lower shelf levels
    scaleXYZ    = glm::vec3(20.0f, upperTableY - lowerShelfY, 0.8f);
    positionXYZ = glm::vec3(0.0f, (upperTableY + lowerShelfY) / 2.0f, 0.0f);
    SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
    SetShaderColor(0.72f, 0.72f, 0.70f, 1.0f);
    SetShaderMaterial("counter");
    m_basicMeshes->DrawBoxMesh();

    /******************************************************************/
    //  LOWER SHELF
    //  Flat plane where the mug, coasters, and napkin holder sit.
    /******************************************************************/
    scaleXYZ    = glm::vec3(20.0f, 1.0f, 6.0f);
    positionXYZ = glm::vec3(0.0f, lowerShelfY, 3.0f);
    SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
    SetShaderColor(0.55f, 0.53f, 0.50f, 1.0f);
    SetShaderTexture("bottomtable");
    SetTextureUVScale(1.0f, 1.0f);
    SetShaderMaterial("counter");
    m_basicMeshes->DrawPlaneMesh();

    /******************************************************************/
    //  FLOWER POT + BONSAI TREE
    //  Two-cylinder pot (narrow base + wider upper section) on the
    //  upper counter. Soil disk on top, then a trunk with branches
    //  and overlapping sphere clusters for the foliage canopy.
    /******************************************************************/
    {
        float potRadius  = 1.6f;
        float potCenterX = 0.0f;
        float potCenterZ = -3.0f;
        float potBaseY   = upperTableY + topThickness; // pot sits on top of the counter slab

        // --- Bottom cylinder (slightly narrower and shorter) ---
        float baseH = 1.0f;
        float baseR = potRadius * 0.92f; // a little narrower than the upper section
        scaleXYZ    = glm::vec3(baseR, baseH, baseR);
        positionXYZ = glm::vec3(potCenterX, potBaseY, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderTexture("pot");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("grayMatte");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // --- Upper cylinder (full width, taller) ---
        float upperH = 1.8f;
        scaleXYZ    = glm::vec3(potRadius, upperH, potRadius);
        positionXYZ = glm::vec3(potCenterX, potBaseY + baseH, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderTexture("pot");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("grayMatte");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        float potTopY = potBaseY + baseH + upperH; // top rim of the pot

        // --- Soil disk visible inside the pot rim ---
        scaleXYZ    = glm::vec3(potRadius * 0.90f, 0.05f, potRadius * 0.90f);
        positionXYZ = glm::vec3(potCenterX, potTopY , potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.25f, 0.18f, 0.10f, 1.0f); // dark earthy brown
        SetShaderMaterial("soil");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // ═══════════════════════════════════════════════════════════
        //  S-CURVE TRUNK  — seg1 halved (0.45), segs 2-4 unchanged (0.75)
        //
        //  Tip formula: tip = base + h·(-sin Z°, cos Z°)
        //
        //  Computed joint positions (all at potCenterZ):
        //    s0 = ( 0.00, potTopY+0.05)  ← pot rim
        //    s1 = (+0.15, potTopY+0.47)  h=0.45, Z=-20°  [halved]
        //    s2 = (+0.34, potTopY+1.19)  h=0.75, Z=-15°  ← LEFT  branch
        //    s3 = (+0.24, potTopY+1.93)  h=0.75, Z=+8°   ← RIGHT branch
        //    s4 = (-0.04, potTopY+2.63)  h=0.75, Z=+22°  ← apex / crown
        //
        //  Branch tips:
        //    LEFT : s2, Z=+55°, h=1.4 → (-0.81, potTopY+1.99)  [LOWER]
        //    RIGHT: s3, Z=-50°, h=1.2 → (+1.16, potTopY+2.70)  [HIGHER]
        //    CROWN: above s4          → (-0.04, potTopY+3.12)   [TOP]
        // ═══════════════════════════════════════════════════════════

        SetShaderColor(0.20f, 0.17f, 0.14f, 1.0f);
        SetShaderMaterial("bark");

        // ── Segment 1  Z=-20°  h=0.45  (halved) ──────────────────────
        scaleXYZ    = glm::vec3(0.22f, 0.45f, 0.22f);
        positionXYZ = glm::vec3(potCenterX + 0.00f, potTopY + 0.05f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, -20, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // Joint at s1  (+0.15, potTopY+0.47)
        scaleXYZ    = glm::vec3(0.21f, 0.21f, 0.21f);
        positionXYZ = glm::vec3(potCenterX + 0.15f, potTopY + 0.47f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // ── Segment 2  Z=-15°  h=0.75 ────────────────────────────────
        scaleXYZ    = glm::vec3(0.19f, 0.75f, 0.19f);
        positionXYZ = glm::vec3(potCenterX + 0.15f, potTopY + 0.47f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, -15, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // Joint at s2  (+0.34, potTopY+1.19)  ← LEFT branch exits here
        scaleXYZ    = glm::vec3(0.19f, 0.19f, 0.19f);
        positionXYZ = glm::vec3(potCenterX + 0.34f, potTopY + 1.19f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // ── Segment 3  Z=+8°  h=0.75 ─────────────────────────────────
        scaleXYZ    = glm::vec3(0.16f, 0.75f, 0.16f);
        positionXYZ = glm::vec3(potCenterX + 0.34f, potTopY + 1.19f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, +8, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // Joint at s3  (+0.24, potTopY+1.93)  ← RIGHT branch exits here
        scaleXYZ    = glm::vec3(0.16f, 0.16f, 0.16f);
        positionXYZ = glm::vec3(potCenterX + 0.24f, potTopY + 1.93f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // ── Segment 4  Z=+22°  h=0.75  (tapers thin) ─────────────────
        scaleXYZ    = glm::vec3(0.12f, 0.75f, 0.12f);
        positionXYZ = glm::vec3(potCenterX + 0.24f, potTopY + 1.93f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, +22, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // ── LEFT BRANCH  s1=(+0.15, potTopY+0.47)  Z=+55°  h=1.4 ────
        // tip = (0.15-1.4·sin55°, potTopY+0.47+1.4·cos55°) = (-1.00, potTopY+1.27)
        scaleXYZ    = glm::vec3(0.11f, 1.40f, 0.11f);
        positionXYZ = glm::vec3(potCenterX + 0.15f, potTopY + 0.47f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, +55, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // Left sub-twig — forks at t≈0.6  base=(0.15-0.84·sin55°, potTopY+0.47+0.84·cos55°)=(-0.54, potTopY+0.95)
        scaleXYZ    = glm::vec3(0.07f, 0.65f, 0.07f);
        positionXYZ = glm::vec3(potCenterX - 0.54f, potTopY + 0.95f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, +42, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // ── RIGHT BRANCH  s2=(+0.34, potTopY+1.19)  Z=-50°  h=1.2 ───
        // tip = (0.34+1.2·sin50°, potTopY+1.19+1.2·cos50°) = (+1.26, potTopY+1.96)
        scaleXYZ    = glm::vec3(0.10f, 1.20f, 0.10f);
        positionXYZ = glm::vec3(potCenterX + 0.34f, potTopY + 1.19f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, -50, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // Right sub-twig — forks at t≈0.6  base=(0.34+0.72·sin50°, potTopY+1.19+0.72·cos50°)=(+0.89, potTopY+1.65)
        scaleXYZ    = glm::vec3(0.06f, 0.60f, 0.06f);
        positionXYZ = glm::vec3(potCenterX + 0.89f, potTopY + 1.65f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, -35, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);

        // ═══════════════════════════════════════════════════════════
        //  LEAF CLUSTERS — branches moved lower
        //
        //    CROWN : (-0.04, potTopY+3.12)  [unchanged]
        //    LEFT  : (-1.00, potTopY+1.27)  [left tip at s1 exit]
        //    RIGHT : (+1.26, potTopY+1.96)  [right tip at s2 exit]
        // ═══════════════════════════════════════════════════════════
        SetShaderMaterial("foliage");
        float cHr=0.19f, cHg=0.50f, cHb=0.15f;  // highlight
        float cMr=0.14f, cMg=0.40f, cMb=0.11f;  // mid-tone
        float cSr=0.09f, cSg=0.28f, cSb=0.08f;  // shadow/inner

        // ──────────────────────────────────────────────────────────
        //  TOP CROWN  —  8 spheres, noticeably the largest cluster
        // ──────────────────────────────────────────────────────────
        float tcX = potCenterX - 0.04f;
        float tcY = potTopY + 3.12f;
        float tcZ = potCenterZ;

        // Core (large)
        scaleXYZ    = glm::vec3(0.65f, 0.55f, 0.62f);
        positionXYZ = glm::vec3(tcX, tcY, tcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cMr, cMg, cMb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Top lobe
        scaleXYZ    = glm::vec3(0.48f, 0.42f, 0.46f);
        positionXYZ = glm::vec3(tcX - 0.08f, tcY + 0.52f, tcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Right lobe
        scaleXYZ    = glm::vec3(0.52f, 0.44f, 0.48f);
        positionXYZ = glm::vec3(tcX + 0.58f, tcY + 0.12f, tcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg + 0.02f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Left lobe
        scaleXYZ    = glm::vec3(0.50f, 0.42f, 0.46f);
        positionXYZ = glm::vec3(tcX - 0.55f, tcY + 0.08f, tcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cMr, cMg + 0.02f, cMb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Front lobe (toward viewer)
        scaleXYZ    = glm::vec3(0.46f, 0.40f, 0.44f);
        positionXYZ = glm::vec3(tcX + 0.10f, tcY - 0.06f, tcZ + 0.50f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.01f, cHg + 0.03f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Back lobe
        scaleXYZ    = glm::vec3(0.44f, 0.38f, 0.42f);
        positionXYZ = glm::vec3(tcX + 0.05f, tcY + 0.04f, tcZ - 0.48f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Lower-inner shadow mass
        scaleXYZ    = glm::vec3(0.55f, 0.38f, 0.52f);
        positionXYZ = glm::vec3(tcX + 0.06f, tcY - 0.42f, tcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg + 0.02f, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Upper-right accent
        scaleXYZ    = glm::vec3(0.38f, 0.33f, 0.36f);
        positionXYZ = glm::vec3(tcX + 0.44f, tcY + 0.48f, tcZ + 0.16f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.02f, cHg + 0.04f, cHb + 0.01f, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // ──────────────────────────────────────────────────────────
        //  LEFT CLUSTER  —  7 spheres  (LOWER, smaller than crown)
        //  Branch tip: (-1.00, potTopY+1.27)
        // ──────────────────────────────────────────────────────────
        float lcX = potCenterX - 1.00f;
        float lcY = potTopY + 1.27f;
        float lcZ = potCenterZ;

        // Core
        scaleXYZ    = glm::vec3(0.40f, 0.34f, 0.38f);
        positionXYZ = glm::vec3(lcX, lcY, lcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cMr, cMg, cMb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Top
        scaleXYZ    = glm::vec3(0.30f, 0.26f, 0.28f);
        positionXYZ = glm::vec3(lcX - 0.05f, lcY + 0.36f, lcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.01f, cHg + 0.03f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Left tip
        scaleXYZ    = glm::vec3(0.28f, 0.24f, 0.26f);
        positionXYZ = glm::vec3(lcX - 0.40f, lcY + 0.05f, lcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg + 0.02f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Right (toward trunk)
        scaleXYZ    = glm::vec3(0.26f, 0.22f, 0.24f);
        positionXYZ = glm::vec3(lcX + 0.36f, lcY + 0.08f, lcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg + 0.02f, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Front
        scaleXYZ    = glm::vec3(0.30f, 0.25f, 0.28f);
        positionXYZ = glm::vec3(lcX - 0.08f, lcY + 0.02f, lcZ + 0.36f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg + 0.01f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Lower shadow
        scaleXYZ    = glm::vec3(0.34f, 0.24f, 0.32f);
        positionXYZ = glm::vec3(lcX - 0.04f, lcY - 0.28f, lcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Sub-twig cluster (left fork tip ≈ (-0.96, potTopY+1.45))
        scaleXYZ    = glm::vec3(0.24f, 0.20f, 0.22f);
        positionXYZ = glm::vec3(potCenterX - 0.96f, potTopY + 1.45f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.01f, cHg + 0.04f, cHb + 0.01f, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // ──────────────────────────────────────────────────────────
        //  RIGHT CLUSTER  —  7 spheres  (HIGHER, smaller than crown)
        //  Branch tip: (+1.26, potTopY+1.96)
        // ──────────────────────────────────────────────────────────
        float rcX = potCenterX + 1.26f;
        float rcY = potTopY + 1.96f;
        float rcZ = potCenterZ;

        // Core
        scaleXYZ    = glm::vec3(0.40f, 0.34f, 0.38f);
        positionXYZ = glm::vec3(rcX, rcY, rcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cMr, cMg, cMb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Top
        scaleXYZ    = glm::vec3(0.30f, 0.26f, 0.28f);
        positionXYZ = glm::vec3(rcX + 0.04f, rcY + 0.36f, rcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.01f, cHg + 0.03f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Right tip (furthest right)
        scaleXYZ    = glm::vec3(0.28f, 0.24f, 0.26f);
        positionXYZ = glm::vec3(rcX + 0.40f, rcY + 0.05f, rcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg + 0.02f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Left (toward trunk)
        scaleXYZ    = glm::vec3(0.26f, 0.22f, 0.24f);
        positionXYZ = glm::vec3(rcX - 0.36f, rcY + 0.08f, rcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg + 0.02f, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Front
        scaleXYZ    = glm::vec3(0.30f, 0.25f, 0.28f);
        positionXYZ = glm::vec3(rcX + 0.06f, rcY + 0.02f, rcZ + 0.36f);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr, cHg + 0.01f, cHb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Lower shadow
        scaleXYZ    = glm::vec3(0.34f, 0.24f, 0.32f);
        positionXYZ = glm::vec3(rcX + 0.04f, rcY - 0.28f, rcZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cSr, cSg, cSb, 1.0f);
        m_basicMeshes->DrawSphereMesh();

        // Sub-twig cluster (right fork tip ≈ (+1.22, potTopY+2.14))
        scaleXYZ    = glm::vec3(0.24f, 0.20f, 0.22f);
        positionXYZ = glm::vec3(potCenterX + 1.22f, potTopY + 2.14f, potCenterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(cHr + 0.01f, cHg + 0.04f, cHb + 0.01f, 1.0f);
        m_basicMeshes->DrawSphereMesh();
    }

    /******************************************************************/
    //  CANDLE MUG  (lower left shelf)
    //  Off-white ceramic mug holding a "snowy day" scented candle.
    //  Built from: cylinder body, torus handle, wax top disk,
    //  and a thin label band around the lower section.
    /******************************************************************/
    {
        float mugRadius = 0.65f;
        float mugHeight = 1.275f;
        float mugX      = -4.0f;
        float mugZ      = 4.0f;
        float mugBaseY  = lowerShelfY;

        // Mug body — main cylinder
        scaleXYZ    = glm::vec3(mugRadius, mugHeight, mugRadius);
        positionXYZ = glm::vec3(mugX, mugBaseY, mugZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.95f, 0.93f, 0.90f, 1.0f); // off-white ceramic
        SetShaderMaterial("ceramic");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // Candle wax surface — thin flat disk just inside the rim
        scaleXYZ    = glm::vec3(mugRadius * 0.88f, 0.055f, mugRadius * 0.88f);
        positionXYZ = glm::vec3(mugX, mugBaseY + mugHeight - 0.05f, mugZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.88f, 0.84f, 0.72f, 1.0f); // deeper cream/wax color
        SetShaderMaterial("ceramic");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // Handle — torus centered on the mug wall so only the outer half
        // is visible, giving a clean D-shaped handle silhouette
        scaleXYZ    = glm::vec3(0.42f, 0.3f, 0.42f);
        positionXYZ = glm::vec3(
            mugX - mugRadius,             // flush against the mug wall
            mugBaseY + mugHeight * 0.50f, // vertically centered
            mugZ
        );
        SetTransformations(scaleXYZ, 90.0f, 0.0f, 0.0f, positionXYZ);
        SetShaderColor(0.95f, 0.93f, 0.90f, 1.0f);
        SetShaderMaterial("ceramic");
        m_basicMeshes->DrawTorusMesh();

        // Label band — thin cylinder wrapping the lower portion of the mug
        scaleXYZ    = glm::vec3(mugRadius + 0.01f, mugHeight * 0.25f, mugRadius + 0.01f);
        positionXYZ = glm::vec3(mugX, mugBaseY + mugHeight * 0.15f, mugZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.88f, 0.86f, 0.82f, 1.0f); // slight tan to hint at a paper label
        SetShaderMaterial("ceramic");
        m_basicMeshes->DrawCylinderMesh(false, false, true);
    }

    /******************************************************************/
    //  COASTERS + WIRE HOLDER  (center of lower shelf)
    //  A stack of round wood coasters inside a minimal wire frame.
    //  The holder has 4 U-shaped arches (front, back, left, right),
    //  each built from two thin cylinder legs, horizontal foot bars,
    //  and a stretched sphere capping the top of the U-curve.
    /******************************************************************/
    {
        float coasterRadius    = 1.1f;
        float coasterThickness = 0.15f;
        float coasterGap       = 0.06f; // small visible gap between each coaster
        int   numCoasters      = 8;
        float coasterX         = -1.5f;
        float coasterZ         = 4.5f;
        float baseY            = lowerShelfY;

        // Pre-calculate stack dimensions so the wire height matches the coaster stack
        float stackBaseY  = baseY + 0.10f;
        float stackHeight = numCoasters * (coasterThickness + coasterGap);
        float stackTopY   = stackBaseY + stackHeight;

        // Wire frame dimensions
        float wireR      = 0.045f;               // wire thickness (very thin)
        float wireH      = stackHeight;           // wire legs match the stack height
        float legSpacing = 0.30f;                 // half-distance between the two legs of a U
        float edgeDist   = coasterRadius + 0.03f; // just outside the coaster edge
        float footY      = baseY + wireR;         // ground level for the horizontal foot bars

        // --- Front arch (+Z side) — legs spread left/right along X ---
        // Left leg
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX - legSpacing, baseY, coasterZ + edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.08f, 0.08f, 0.08f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Left foot bar — runs inward toward coaster center (-Z direction)
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX - legSpacing, footY, coasterZ + edgeDist);
        SetTransformations(scaleXYZ, -90, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Right leg
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX + legSpacing, baseY, coasterZ + edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Right foot bar
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX + legSpacing, footY, coasterZ + edgeDist);
        SetTransformations(scaleXYZ, -90, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Top U-curve — stretched sphere bridging the two legs
        scaleXYZ    = glm::vec3(legSpacing + wireR, wireR * 1.5f, wireR);
        positionXYZ = glm::vec3(coasterX, baseY + wireH, coasterZ + edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // --- Back arch (-Z side) ---
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX - legSpacing, baseY, coasterZ - edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.08f, 0.08f, 0.08f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Left foot (+Z toward center)
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX - legSpacing, footY, coasterZ - edgeDist);
        SetTransformations(scaleXYZ, 90, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Right leg
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX + legSpacing, baseY, coasterZ - edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Right foot
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX + legSpacing, footY, coasterZ - edgeDist);
        SetTransformations(scaleXYZ, 90, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        scaleXYZ    = glm::vec3(legSpacing + wireR, wireR * 1.5f, wireR);
        positionXYZ = glm::vec3(coasterX, baseY + wireH, coasterZ - edgeDist);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // --- Left arch (-X side) — legs spread along Z axis ---
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX - edgeDist, baseY, coasterZ - legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.08f, 0.08f, 0.08f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Foot toward center (+X)
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX - edgeDist, footY, coasterZ - legSpacing);
        SetTransformations(scaleXYZ, 0, 0, -90, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Other leg
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX - edgeDist, baseY, coasterZ + legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Other foot
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX - edgeDist, footY, coasterZ + legSpacing);
        SetTransformations(scaleXYZ, 0, 0, -90, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        scaleXYZ    = glm::vec3(wireR, wireR * 1.5f, legSpacing + wireR);
        positionXYZ = glm::vec3(coasterX - edgeDist, baseY + wireH, coasterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // --- Right arch (+X side) ---
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX + edgeDist, baseY, coasterZ - legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.08f, 0.08f, 0.08f, 1.0f);
        SetShaderMaterial("darkMetal");
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Foot toward center (-X)
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX + edgeDist, footY, coasterZ - legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 90, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Other leg
        scaleXYZ    = glm::vec3(wireR, wireH, wireR);
        positionXYZ = glm::vec3(coasterX + edgeDist, baseY, coasterZ + legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        // Other foot
        scaleXYZ    = glm::vec3(wireR, edgeDist, wireR);
        positionXYZ = glm::vec3(coasterX + edgeDist, footY, coasterZ + legSpacing);
        SetTransformations(scaleXYZ, 0, 0, 90, positionXYZ);
        m_basicMeshes->DrawCylinderMesh(false, false, true);
        scaleXYZ    = glm::vec3(wireR, wireR * 1.5f, legSpacing + wireR);
        positionXYZ = glm::vec3(coasterX + edgeDist, baseY + wireH, coasterZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        m_basicMeshes->DrawSphereMesh();

        // --- Coaster stack — 8 round disks with small visible gaps ---
        for (int i = 0; i < numCoasters; i++)
        {
            scaleXYZ    = glm::vec3(coasterRadius, coasterThickness, coasterRadius);
            positionXYZ = glm::vec3(coasterX,
                                    stackBaseY + i * (coasterThickness + coasterGap),
                                    coasterZ);
            SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
            // Alternate shade slightly so each coaster reads as a separate piece
            float shade = (i % 2 == 0) ? 0.90f : 0.87f;
            SetShaderColor(shade, shade - 0.01f, shade - 0.04f, 1.0f);
            SetShaderTexture("coaster");
            SetTextureUVScale(1.0f, 1.0f);
            SetShaderMaterial("lightWood");
            m_basicMeshes->DrawCylinderMesh(true, true, true);
        }
    }

    /******************************************************************/
    //  NAPKIN HOLDER  (lower right shelf)
    //  Two thin wooden boards with arch-shaped tops, facing each
    //  other and connected at the base. Napkins stand vertically in
    //  the slot between the two panels.
    //
    //  Arch trick: DrawCylinderMesh draws a full circle, so we
    //  position it at the top edge of the rectangular box panel —
    //  the lower half hides inside the box, leaving only the arch.
    /***************************************
     * ***************************/
    {
        float nhX        = 2.0f;   // center X on the lower shelf
        float nhZ        = 4.5f;   // center Z (moved back from front edge)
        float nhBaseY    = lowerShelfY;
        float panelWidth = 3.4f;   // width of each wooden panel (X direction)
        float panelRectH = 2.0f;   // height of the rectangular lower portion of each panel
        float archRadius = panelWidth / 2.0f; // arch radius = half panel width
        float panelThk   = 0.2f; // board thickness — reduce to make panels thinner
        float slotGap    = 0.75f;  // gap between front and back panels where napkins sit

        // --- Front panel (+Z side) ---
        float frontZ = nhZ + slotGap / 2.0f + panelThk / 2.0f;

        // Rectangular lower portion of the front panel
        scaleXYZ    = glm::vec3(panelWidth, panelRectH, panelThk);
        positionXYZ = glm::vec3(nhX, nhBaseY + panelRectH / 2.0f, frontZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.55f, 0.35f, 0.15f, 1.0f);
        SetShaderTexture("wood");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wood");
        m_basicMeshes->DrawBoxMesh();

        // Arch top — cylinder rotated -90X so its local Y axis points inward (-Z)
        // Placed at the outer face so the arch aligns with the box edge exactly
        scaleXYZ    = glm::vec3(archRadius, panelThk, archRadius);
        positionXYZ = glm::vec3(nhX, nhBaseY + panelRectH, frontZ + panelThk / 2.0f);
        SetTransformations(scaleXYZ, -90, 0, 0, positionXYZ);
        SetShaderColor(0.55f, 0.35f, 0.15f, 1.0f);
        SetShaderTexture("woodie");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("woodie");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // --- Back panel (-Z side) ---
        float backZ = nhZ - slotGap / 2.0f - panelThk / 2.0f;

        // Rectangular lower portion of the back panel
        scaleXYZ    = glm::vec3(panelWidth, panelRectH, panelThk);
        positionXYZ = glm::vec3(nhX, nhBaseY + panelRectH / 2.0f, backZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.55f, 0.35f, 0.15f, 1.0f);
        SetShaderTexture("wood");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wood");
        m_basicMeshes->DrawBoxMesh();

        // Arch top — rotated +90X so local Y points inward (+Z)
        scaleXYZ    = glm::vec3(archRadius, panelThk, archRadius);
        positionXYZ = glm::vec3(nhX, nhBaseY + panelRectH, backZ - panelThk / 2.0f);
        SetTransformations(scaleXYZ, 90, 0, 0, positionXYZ);
        SetShaderColor(0.55f, 0.35f, 0.15f, 1.0f);
        SetShaderTexture("wood");
        SetTextureUVScale(1.0f, 1.0f);
        SetShaderMaterial("wood");
        m_basicMeshes->DrawCylinderMesh(true, true, true);

        // --- Base slab connecting front and back panels at the bottom ---
        float totalDepth = slotGap + panelThk * 2.0f; // full depth of the whole holder
        scaleXYZ    = glm::vec3(panelWidth, panelThk, totalDepth);
        positionXYZ = glm::vec3(nhX, nhBaseY + panelThk / 2.0f, nhZ);
        SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
        SetShaderColor(0.52f, 0.32f, 0.13f, 1.0f); // slightly darker than the panels
        SetShaderMaterial("wood");
        m_basicMeshes->DrawBoxMesh();

        // --- Napkins — 12 thin boxes packed into the slot ---
        float totalWoodH  = panelRectH + archRadius; // full visible height of each panel
        float napkinH     = totalWoodH * 1.0f;       // napkins match panel height
        int   napkinCount = 12;
        float totalSlotZ  = slotGap * 0.88f;         // napkins fill 88% of the slot depth
        float napkinThk   = totalSlotZ / (float)napkinCount;
        float startZ      = nhZ - totalSlotZ / 2.0f + napkinThk / 2.0f;

        for (int i = 0; i < napkinCount; i++)
        {
            // Vary height and shade a little so they look like individual napkins
            float heightVar = (i % 2 == 0) ? 1.0f : 0.97f;
            float shade     = 0.94f + (i % 3) * 0.01f;

            scaleXYZ    = glm::vec3(panelWidth * 1.17f, napkinH * heightVar, napkinThk * 0.92f);
            positionXYZ = glm::vec3(nhX, nhBaseY + (napkinH * heightVar) / 2.0f + panelThk,
                                    startZ + i * napkinThk);
            SetTransformations(scaleXYZ, 0, 0, 0, positionXYZ);
            SetShaderColor(shade, shade, shade * 0.98f, 1.0f);
            SetShaderTexture("napkin");
            SetTextureUVScale(1.0f, 1.0f);
            SetShaderMaterial("napkin");
            m_basicMeshes->DrawBoxMesh();
        }
    }

    // Restore backface culling to whatever state it was in before we started
    if (cullEnabled)
        glEnable(GL_CULL_FACE);
}