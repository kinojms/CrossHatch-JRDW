// CrossHatchEditor.cpp : Defines the entry point for the application.
//
#include "CrossHatchEditor.h"
#include "Reconstructor.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <sstream>
#include <fstream>
#include <random>
#include "InputManager.h"
#include "Camera.h"
#include "PrimitiveObjects.h"
#include "ObjLoader.h"
#include "VideoPlayer.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>

#include <map>
#include <set>
#include <algorithm> // For std::max and std::min
#endif
#include <filesystem>
namespace fs = std::filesystem;

#include <bgfx/bgfx.h>
#include <bx/uint32_t.h>
#include <bgfx/platform.h>
#include <bx/commandline.h>
#include <bx/endian.h>
#include <bx/math.h>
#include <bx/readerwriter.h>
#include <bx/string.h>

#include <algorithm>
#include <string>
#include <map>

//include embedded shaders

#include <bgfx/defines.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_dx11.h>
#include "bgfx-imgui/imgui_impl_bgfx.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>
#endif
#include <imgui_internal.h>
#include "Logger.h"

#include "Light.h"
//#include "DebugDraw.h"
#include <ImGuizmo.h>
#include <cmath> // for rad2deg, deg2rad, etc.
#include <cfloat> // For FLT_MAX and FLT_MIN

//png, jpg image support #1179
//reference: https://github.com/bkaradzic/bgfx/issues/1179
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "TextRenderer.h" // for text element
//#include "ImGuiFileDialog.h"

std::vector<Camera> cameras;
int currentCameraIndex = 0;
static bool highlightVisible = true;
static float RadToDeg(float rad) { return rad * (180.0f / 3.14159265358979f); }
static float DegToRad(float deg) { return deg * (3.14159265358979f / 180.0f); }
static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;

#define WNDW_WIDTH 1600
#define WNDW_HEIGHT 900

static bool s_showStats = false;
bgfx::UniformHandle u_lightDir;
bgfx::UniformHandle u_lightColor;
bgfx::UniformHandle u_viewPos;
//bgfx::UniformHandle u_scale;
// Global toggle: when true, render using unlit vertex colors (Attribute mode).
static bool useAttributeMode = false;
// Program for unlit vertex-color rendering (Attribute mode).
static bgfx::ProgramHandle unlitColorProgram = BGFX_INVALID_HANDLE;
// Define the picking render target dimensions.
#define PICKING_DIM 128

static bool useGlobalCrosshatchSettings = true;

// Window visibility toggles (defaults: some windows hidden)
static bool show_Inspector = true;
static bool show_ObjectList = true;
static bool show_Gallery = false;          // hidden by default
static bool show_Reconstructor = true;
static bool show_Info = false;             // hidden by default
static bool show_Controls = false;         // hidden by default
static bool show_Screenshot = false;       // hidden by default
static bool show_CameraSettings = false;   // hidden by default
static bool show_Cameras = false;          // hidden by default
static bool show_LogConsole = false;  // hidden by default

// Forward declaration so we can use Instance* in globals before its full definition.
struct Instance;

// Pointer to the current editor instances vector used by UI helpers (e.g. right sidebar).
// Set once in main after creating the local instances vector.
static std::vector<Instance*>* g_Instances = nullptr;

// Set in main() for use by RenderInspectorBody (right sidebar).
static bgfx::VertexBufferHandle g_vbh_sphere = BGFX_INVALID_HANDLE;
static bgfx::IndexBufferHandle g_ibh_sphere = BGFX_INVALID_HANDLE;
static bgfx::VertexBufferHandle g_vbh_cone = BGFX_INVALID_HANDLE;
static bgfx::IndexBufferHandle g_ibh_cone = BGFX_INVALID_HANDLE;

// (Define TAU in C++ too)
const float TAU = 6.28318530718f;
// Declare static variables to hold our crosshatch parameters:
static float inkColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Typically black ink.
static float epsilonValue = 0.02f;              // Outer Line Smoothness or Epsilon
static float strokeMultiplier = 1.0f;           // Outer Hatch Density or Stroke Multiplier
static float lineAngle1 = TAU / 8.0f;           // Outer Hatch Angle or Line Angle 1
static float lineAngle2 = TAU / 16.0f;          // Line Angle 2

// These static variables will hold the extra parameter values.
static float patternScale = 0.4f;               // Outer Hatch Scale or Pattern Scale
static float lineThickness = 0.3f;              // Outer Hatch Weight or Line Thickness

// You can leave the remaining components as 0 (or later repurpose them)
static float transparencyValue = 1.0f;          // Hatch Opacity or Transparency
static int crosshatchMode = 4;                  // 0 = hatch ver 1.0, 1 = hatch ver 1.1, 2 = hatch ver 1.2, 3 = hatch ver 1.3, 4 = basic unlit-like shader

// These static variables will hold the values for u_paramsLayer
static float layerPatternScale = 0.5f;          // Inner Hatch Scale or Layer Pattern Scale
static float layerStrokeMult = 0.250f;           // Inner Hatch Density or Layer Stroke Multiplier
static float layerAngle = 2.983f;               // Inner Hatch Angle or Layer Angle
static float layerLineThickness = 10.0f;        // Inner Hatch Weight or Layer Line Thickness

// Persistence helpers for window visibility
static void LoadWindowVisibilityConfig(const std::string& filename)
{
    std::ifstream in(filename);
    if (!in.is_open())
        return;

    auto trim = [](std::string &s) {
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    };

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        trim(key); trim(val);
        bool v = (val == "1" || val == "true" || val == "True");

        if (key == "Inspector") show_Inspector = v;
        else if (key == "ObjectList") show_ObjectList = v;
        else if (key == "Gallery") show_Gallery = v;
        else if (key == "Reconstructor") show_Reconstructor = v;
        else if (key == "Info") show_Info = v;
        else if (key == "Controls") show_Controls = v;
        else if (key == "Screenshot") show_Screenshot = v;
        else if (key == "CameraSettings") show_CameraSettings = v;
        else if (key == "Cameras") show_Cameras = v;
        else if (key == "LogConsole") show_LogConsole = v;
    }
}

static void SaveWindowVisibilityConfig(const std::string& filename)
{
    std::ofstream out(filename, std::ios::trunc);
    if (!out.is_open())
        return;

    out << "Inspector=" << (show_Inspector ? "1" : "0") << "\n";
    out << "ObjectList=" << (show_ObjectList ? "1" : "0") << "\n";
    out << "Gallery=" << (show_Gallery ? "1" : "0") << "\n";
    out << "Reconstructor=" << (show_Reconstructor ? "1" : "0") << "\n";
    out << "Info=" << (show_Info ? "1" : "0") << "\n";
    out << "Controls=" << (show_Controls ? "1" : "0") << "\n";
    out << "Screenshot=" << (show_Screenshot ? "1" : "0") << "\n";
    out << "CameraSettings=" << (show_CameraSettings ? "1" : "0") << "\n";
    out << "Cameras=" << (show_Cameras ? "1" : "0") << "\n";
    out << "LogConsole=" << (show_LogConsole ? "1" : "0") << "\n";
}

bgfx::TextureHandle noiseTexture = BGFX_INVALID_HANDLE;

static bgfx::UniformHandle u_uvTransform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle u_albedoFactor = BGFX_INVALID_HANDLE;

// Global uniform for the diffuse texture (put this at file scope or as a static variable)
static bgfx::UniformHandle u_diffuseTex = BGFX_INVALID_HANDLE;

static bgfx::TextureHandle s_pickingRT = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_pickingRTDepth = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle s_pickingFB = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_pickingReadTex = BGFX_INVALID_HANDLE;
static uint8_t s_pickingBlitData[PICKING_DIM * PICKING_DIM * 4] = { 0 };

// Uniform for the picking shader that outputs the object ID as color.
static bgfx::UniformHandle u_id = BGFX_INVALID_HANDLE;
// Program handle for the picking pass.
static bgfx::ProgramHandle pickingProgram = BGFX_INVALID_HANDLE;

// ------------------------------------------------------------
// 3D viewport: world axes + "infinite" XZ grid
// ------------------------------------------------------------
struct LineVertex
{
    float x, y, z;
    float nx, ny, nz;
    uint32_t abgr;
    float u, v;
};

static const bgfx::VertexLayout& GetLineVertexLayout()
{
    // Match the project's common mesh layout (Position/Normal/Color0/TexCoord0),
    // so we can reuse `unlitColorProgram` which reads vertex color.
    static bgfx::VertexLayout s_layout;
    static bool s_inited = false;
    if (!s_inited)
    {
        s_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        s_inited = true;
    }
    return s_layout;
}

static inline uint32_t PackAbgr(uint8_t a, uint8_t b, uint8_t g, uint8_t r)
{
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

static void SubmitLineList(uint16_t viewId, bgfx::ProgramHandle program, const LineVertex* verts, uint32_t numVerts, uint64_t state)
{
    if (!bgfx::isValid(program) || verts == nullptr || numVerts < 2)
        return;
    if (!bgfx::getAvailTransientVertexBuffer(numVerts, GetLineVertexLayout()))
        return;

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, numVerts, GetLineVertexLayout());
    std::memcpy(tvb.data, verts, sizeof(LineVertex) * numVerts);

    float id[16];
    bx::mtxIdentity(id);
    bgfx::setTransform(id);
    bgfx::setVertexBuffer(0, &tvb, 0, numVerts);
    bgfx::setState(state);
    bgfx::submit(viewId, program);
}

static void DrawWorldAxesAndGrid(uint16_t viewId, const Camera& cam, bgfx::ProgramHandle program)
{
    if (!bgfx::isValid(program))
        return;

    // "Infinite" look: rebuild grid around camera each frame.
    // Keep the grid size tied to the camera far clip to avoid visible edges.
    const float farClip = std::max(10.0f, cam.farClip);
    const float gridRadius = std::min(5000.0f, farClip * 0.90f);

    // Grid settings (units).
    const float minorStep = 3.5f;
    const int   halfLines = std::max(10, int(gridRadius / minorStep));

    const float originX = std::floor(cam.position.x / minorStep) * minorStep;
    const float originZ = std::floor(cam.position.z / minorStep) * minorStep;

    const float zMin = originZ - halfLines * minorStep;
    const float zMax = originZ + halfLines * minorStep;
    const float xMin = originX - halfLines * minorStep;
    const float xMax = originX + halfLines * minorStep;

    // Colors (ABGR): X=red, Y=green, Z=blue.
    // Make them bright and mostly independent of scene darkening.
    const uint32_t gridColor = PackAbgr(0x88, 0xe0, 0xe0, 0xe0); // bright grid, semi-opaque
    const uint32_t xColor    = PackAbgr(0xff, 0x20, 0x20, 0xff); // bright red (X)
    const uint32_t yColor    = PackAbgr(0xff, 0x20, 0xff, 0x20); // bright green (Y)
    const uint32_t zColor    = PackAbgr(0xff, 0xff, 0x20, 0x20); // bright blue (Z)

    // Build vertices (two vertices per segment; line list).
    const uint32_t gridLines = uint32_t(2 * (2 * halfLines + 1));
    const uint32_t axisLines = 3;
    const uint32_t totalVerts = (gridLines + axisLines) * 2;

    std::vector<LineVertex> v;
    v.reserve(totalVerts);

    auto pushLine = [&](float x0, float y0, float z0, float x1, float y1, float z1, uint32_t abgr)
    {
        LineVertex a{};
        a.x = x0; a.y = y0; a.z = z0;
        a.nx = 0.0f; a.ny = 1.0f; a.nz = 0.0f;
        a.abgr = abgr;
        a.u = 0.0f; a.v = 0.0f;

        LineVertex b{};
        b.x = x1; b.y = y1; b.z = z1;
        b.nx = 0.0f; b.ny = 1.0f; b.nz = 0.0f;
        b.abgr = abgr;
        b.u = 0.0f; b.v = 0.0f;

        v.push_back(a);
        v.push_back(b);
    };

    // Grid lines parallel to Z (vary X).
    for (int i = -halfLines; i <= halfLines; ++i)
    {
        const float x = originX + float(i) * minorStep;
        pushLine(x, 0.0f, zMin, x, 0.0f, zMax, gridColor);
    }
    // Grid lines parallel to X (vary Z).
    for (int i = -halfLines; i <= halfLines; ++i)
    {
        const float z = originZ + float(i) * minorStep;
        pushLine(xMin, 0.0f, z, xMax, 0.0f, z, gridColor);
    }

    // World axes through origin, stretched very far.
    const float axisLen = std::min(200000.0f, farClip * 10.0f);
    pushLine(-axisLen, 0.0f, 0.0f, +axisLen, 0.0f, 0.0f, xColor);
    pushLine(0.0f, -axisLen, 0.0f, 0.0f, +axisLen, 0.0f, yColor);
    pushLine(0.0f, 0.0f, -axisLen, 0.0f, 0.0f, +axisLen, zColor);

    const uint64_t state =
        BGFX_STATE_WRITE_RGB |
        BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_PT_LINES |
        BGFX_STATE_BLEND_ALPHA;

    SubmitLineList(viewId, program, v.data(), uint32_t(v.size()), state);
}

struct TextureOption {
    std::string name;
    bgfx::TextureHandle handle;
};

// Set in main() for use by RenderInspectorBody (right sidebar).
static std::vector<TextureOption>* g_availableTextures = nullptr;

std::vector<TextureOption> availableNoiseTextures;
int currentNoiseIndex = 0; // which noise texture is selected by the user
int globalCurrentNoiseIndex = 0; // which noise texture is selected by the user

struct MaterialParams
{
    float tiling[2];     // (tilingU, tilingV)
    float offset[2];     // (offsetU, offsetV)
    float albedo[4];     // (r, g, b, a)
};

// Define a struct to hold animation parameters for a light.
struct LightAnimation {
    bool enabled = false;                       // Toggle animation on/off.
    float amplitude[3] = { 6.0f, 0.0f, 0.0f };  // Amplitude for x, y, z motion.
    float frequency[3] = { 1.0f, 1.0f, 1.0f };  // Frequency (Hz) for each axis.
    float phase[3] = { 0.0f, 0.0f, 0.0f };  // Phase offset for each axis.
};

struct Instance
{
    int id;
    std::string name;
    std::string type;
    int meshNumber = 0; // For multi-mesh objects
    float position[3];
    float rotation[3]; // Euler angles in radians (for X, Y, Z)
    float scale[3];    // Non-uniform scale for each axis
    float worldPosition[3];
    bgfx::VertexBufferHandle vertexBuffer;
    bgfx::IndexBufferHandle indexBuffer;
    bool selected = false;

    // Add an override object color (RGBA)
    float objectColor[4];
    // NEW: optional diffuse texture for the object.
    bgfx::TextureHandle diffuseTexture = BGFX_INVALID_HANDLE;

    // NEW: noise texture
    bgfx::TextureHandle noiseTexture = BGFX_INVALID_HANDLE;

    MaterialParams material;

    // --- New for lights ---
    bool isLight = false;
    LightProperties lightProps; // Valid if isLight == true.

    // NEW: Store the base (original) position for animation.
    float basePosition[3];

    // NEW: Animation parameters for lights.
    LightAnimation lightAnim;

    // NEW: For light objects only â€“ determines if the debug visual (the sphere)
    // is drawn. (Default true.)
    bool showDebugVisual = true;

    std::vector<Instance*> children; // Hierarchy: child instances
    Instance* parent = nullptr;      // pointer to parent

    float inkColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    // Declare variables to hold our crosshatch parameters:
    float epsilonValue = 0.02f;             // Outer Line Smoothness or Epsilon
    float strokeMultiplier = 1.0f;          // Outer Hatch Density or Stroke Multiplier
    float lineAngle1 = TAU / 8.0f;          // Outer Hatch Angle or Line Angle 1
    float lineAngle2 = TAU / 16.0f;         // Line Angle 2

    // These variables will hold the extra parameter values.
    float patternScale = 0.15f;              // Outer Hatch Scale or Pattern Scale
    float lineThickness = 0.3f;             // Outer Hatch Weight or Line Thickness

    // You can leave the remaining components as 0 (or later repurpose them)
    float transparencyValue = 1.0f;         // Hatch Opacity or Transparency
    int crosshatchMode = 4;                 // 0 = hatch ver 1.0, 1 = hatch ver 1.1, 2 = hatch ver 1.2, 3 = hatch ver 1.3, 4 = basic shader

    // These variables will hold the values for u_paramsLayer
    float layerPatternScale = 0.15f;         // Inner Hatch Scale or Layer Pattern Scale
    float layerStrokeMult = 0.250f;         // Inner Hatch Density or Layer Stroke Multiplier
    float layerAngle = 2.983f;              // Inner Hatch Angle or Layer Angle
    float layerLineThickness = 10.0f;       // Inner Hatch Weight or Layer Line Thickness

    //variables for rotating spot light
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float radius = 5.0f;
    float rotationSpeed = 0.5f;
    float instanceAngle = 0.0f;

    // for comic bubble text
    std::string textContent;

    Instance(int instanceId, const std::string& instanceName, const std::string& instanceType, float x, float y, float z, bgfx::VertexBufferHandle vbh, bgfx::IndexBufferHandle ibh)
        : id(instanceId), name(instanceName), type(instanceType), vertexBuffer(vbh), indexBuffer(ibh), textContent("A")
    {
        position[0] = x;
        position[1] = y;
        position[2] = z;
        // Initialize with no rotation and uniform scale of 1
        rotation[0] = rotation[1] = rotation[2] = 0.0f;
        scale[0] = scale[1] = scale[2] = 1.0f;
        // Initialize the object color to white (no override)
        objectColor[0] = 1.0f; objectColor[1] = 1.0f; objectColor[2] = 1.0f; objectColor[3] = 1.0f;
        // For light objects, default to drawing the debug visual.
        showDebugVisual = true;

        material.tiling[0] = 1.0f;
        material.tiling[1] = 1.0f;
        material.offset[0] = 0.0f;
        material.offset[1] = 0.0f;
        material.albedo[0] = 1.0f; // r
        material.albedo[1] = 1.0f; // g
        material.albedo[2] = 1.0f; // b
        material.albedo[3] = 1.0f; // a

        noiseTexture = availableNoiseTextures[0].handle;
        // Store base position for animation.
        basePosition[0] = x; basePosition[1] = y; basePosition[2] = z;
    }
    void addChild(Instance* child) {
        children.push_back(child);
        child->parent = this;
    }
};
static Instance* selectedInstance = nullptr;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
};

class CommandManager {
    std::vector<std::unique_ptr<ICommand>> undoStack, redoStack;
public:
    void executeCommand(std::unique_ptr<ICommand> cmd) {
        cmd->execute();
        undoStack.push_back(std::move(cmd));
        redoStack.clear();
    }
    void undo() {
        if (undoStack.empty()) return;
        auto cmd = std::move(undoStack.back());
        undoStack.pop_back();
        cmd->undo();
        redoStack.push_back(std::move(cmd));
    }
    void redo() {
        if (redoStack.empty()) return;
        auto cmd = std::move(redoStack.back());
        redoStack.pop_back();
        cmd->execute();
        undoStack.push_back(std::move(cmd));
    }
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
};

class MoveCommand : public ICommand {
    Instance* inst;
    float oldPos[3];
    float newPos[3];
public:
    MoveCommand(Instance* i,
        const float oldPosX,
        const float oldPosY,
        const float oldPosZ,
        const float newPosX,
        const float newPosY,
        const float newPosZ) {
        inst = i;
        oldPos[0] = oldPosX;
        oldPos[1] = oldPosY;
        oldPos[2] = oldPosZ;
        newPos[0] = newPosX;
        newPos[1] = newPosY;
        newPos[2] = newPosZ;
    }
    void execute() override {
        inst->position[0] = newPos[0];
        inst->position[1] = newPos[1];
        inst->position[2] = newPos[2];
        /*std::cout << "MoveCommand executed: " << inst->name << " to ("
            << newPos[0] << ", " << newPos[1] << ", " << newPos[2] << ")\n";*/
    }
    void undo() override {
        inst->position[0] = oldPos[0];
        inst->position[1] = oldPos[1];
        inst->position[2] = oldPos[2];
        /*std::cout << "MoveCommand undone: " << inst->name << " to ("
            << oldPos[0] << ", " << oldPos[1] << ", " << oldPos[2] << ")\n";*/
    }
};

class RotateCommand : public ICommand {
    Instance* inst;
    float oldRot[3];
    float newRot[3];
public:
    RotateCommand(Instance* i,
        const float oldRotX,
        const float oldRotY,
        const float oldRotZ,
        const float newRotX,
        const float newRotY,
        const float newRotZ) {
        inst = i;
        oldRot[0] = oldRotX;
        oldRot[1] = oldRotY;
        oldRot[2] = oldRotZ;
        newRot[0] = newRotX;
        newRot[1] = newRotY;
        newRot[2] = newRotZ;
    }
    void execute() override {
        inst->rotation[0] = newRot[0];
        inst->rotation[1] = newRot[1];
        inst->rotation[2] = newRot[2];
        /*std::cout << "RotateCommand executed: " << inst->name << " to ("
            << newRot[0] << ", " << newRot[1] << ", " << newRot[2] << ")\n";*/
    }
    void undo() override {
        inst->rotation[0] = oldRot[0];
        inst->rotation[1] = oldRot[1];
        inst->rotation[2] = oldRot[2];
        /*std::cout << "RotateCommand undone: " << inst->name << " to ("
            << oldRot[0] << ", " << oldRot[1] << ", " << oldRot[2] << ")\n";*/
    }
};

class ScaleCommand : public ICommand {
    Instance* inst;
    float oldScale[3];
    float newScale[3];
public:
    ScaleCommand(Instance* i,
        const float oldScaleX,
        const float oldScaleY,
        const float oldScaleZ,
        const float newScaleX,
        const float newScaleY,
        const float newScaleZ) {
        inst = i;
        oldScale[0] = oldScaleX;
        oldScale[1] = oldScaleY;
        oldScale[2] = oldScaleZ;
        newScale[0] = newScaleX;
        newScale[1] = newScaleY;
        newScale[2] = newScaleZ;
    }
    void execute() override {
        inst->scale[0] = newScale[0];
        inst->scale[1] = newScale[1];
        inst->scale[2] = newScale[2];
        /*std::cout << "ScaleCommand executed: " << inst->name << " to ("
            << newScale[0] << ", " << newScale[1] << ", " << newScale[2] << ")\n";*/
    }
    void undo() override {
        inst->scale[0] = oldScale[0];
        inst->scale[1] = oldScale[1];
        inst->scale[2] = oldScale[2];
        /*std::cout << "ScaleCommand undone: " << inst->name << " to ("
            << oldScale[0] << ", " << oldScale[1] << ", " << oldScale[2] << ")\n";*/
    }
};

class AddInstanceCommand : public ICommand {
    Instance* inst;
    std::vector<Instance*>* instancesList;
    bool isAdded = false;
public:
    AddInstanceCommand(Instance* i, std::vector<Instance*>* list) 
        : inst(i), instancesList(list), isAdded(false) {}
    void execute() override {
        instancesList->push_back(inst);
        isAdded = true;
        std::cout << "[Undo/Redo] Instance added: " << inst->name << std::endl;
    }
    void undo() override {
        if (isAdded && !instancesList->empty() && instancesList->back() == inst) {
            instancesList->pop_back();
            std::cout << "[Undo/Redo] Instance removed (undo): " << inst->name << std::endl;
        }
    }
};

class DeleteInstanceCommand : public ICommand {
    Instance* inst;
    // If top-level, instancesList points to global instances vector. If child, parent points to parent instance.
    std::vector<Instance*>* instancesList = nullptr;
    Instance* parent = nullptr;
    size_t originalIndex = 0;
    bool wasTopLevel = false;
public:
    // Top-level constructor
    DeleteInstanceCommand(Instance* i, std::vector<Instance*>* list, size_t idx)
        : inst(i), instancesList(list), parent(nullptr), originalIndex(idx), wasTopLevel(true) {}

    // Child constructor
    DeleteInstanceCommand(Instance* i, Instance* parentInst, size_t idx)
        : inst(i), instancesList(nullptr), parent(parentInst), originalIndex(idx), wasTopLevel(false) {}

    void execute() override {
        if (wasTopLevel && instancesList) {
            auto it = std::find(instancesList->begin(), instancesList->end(), inst);
            if (it != instancesList->end()) {
                instancesList->erase(it);
                inst->parent = nullptr;
                std::cout << "[Undo/Redo] Instance deleted (top-level): " << inst->name << std::endl;
            }
        }
        else if (parent) {
            auto it = std::find(parent->children.begin(), parent->children.end(), inst);
            if (it != parent->children.end()) {
                parent->children.erase(it);
                inst->parent = nullptr;
                std::cout << "[Undo/Redo] Instance deleted (child): " << inst->name << std::endl;
            }
        }
    }

    void undo() override {
        if (wasTopLevel && instancesList) {
            if (originalIndex <= instancesList->size()) {
                instancesList->insert(instancesList->begin() + originalIndex, inst);
                inst->parent = nullptr;
                std::cout << "[Undo/Redo] Instance restored (top-level): " << inst->name << std::endl;
            }
        }
        else if (parent) {
            if (originalIndex <= parent->children.size()) {
                parent->children.insert(parent->children.begin() + originalIndex, inst);
                inst->parent = parent;
                std::cout << "[Undo/Redo] Instance restored (child): " << inst->name << std::endl;
            }
        }
    }
};

class ClearInstancesCommand : public ICommand {
    std::vector<Instance*> savedInstances;
    std::vector<Instance*>* instancesList;
public:
    ClearInstancesCommand(std::vector<Instance*>* list) 
        : instancesList(list) {}
    void execute() override {
        // Save all instances
        savedInstances = *instancesList;
        instancesList->clear();
        std::cout << "[Undo/Redo] All instances cleared (" << savedInstances.size() << " saved for undo)" << std::endl;
    }
    void undo() override {
        // Restore instances
        *instancesList = savedInstances;
        std::cout << "[Undo/Redo] Instances restored (undo): " << savedInstances.size() << " instance(s)" << std::endl;
    }
};

CommandManager gCmdManager;

struct MeshData {
    std::vector<PosColorVertex> vertices;
    std::vector<uint32_t> indices;
};

// CPU-side editable mesh data for instances that support geometry operations.
// Keyed by Instance::id.
static std::unordered_map<int, MeshData> g_InstanceMeshData;
// Base mesh templates keyed by instance type (e.g. "cube", "plane", "mesh").
// These are cloned into g_InstanceMeshData when instances are created.
static std::unordered_map<std::string, MeshData> g_BaseMeshData;

struct Vec3 {
    float x, y, z;
};

void BuildWorldMatrix(const Instance* inst, float* outMatrix) {
    float local[16];
    float translation[3] = { inst->position[0], inst->position[1], inst->position[2] };
    float rotationDeg[3] = { RadToDeg(inst->rotation[0]), RadToDeg(inst->rotation[1]), RadToDeg(inst->rotation[2]) };
    float scale[3] = { inst->scale[0], inst->scale[1], inst->scale[2] };
    // Build the local transform matrix.
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotationDeg, scale, local);

    if (inst->parent)
    {
        float parentWorld[16];
        // Recursively compute the parent's world matrix.
        BuildWorldMatrix(inst->parent, parentWorld);
        // Multiply parent's world matrix with the local transform.
        bx::mtxMul(outMatrix, local, parentWorld);
    }
    else
    {
        memcpy(outMatrix, local, sizeof(float) * 16);
    }
}
void BuildMatrixFromInstance_ImGuizmo(const Instance* inst, float* outMatrix)
{
    // 1) Copy your instanceâ€™s data into the arrays ImGuizmo expects:
    float translation[3] = { inst->position[0], inst->position[1], inst->position[2] };
    float rotationDeg[3] = {
        RadToDeg(inst->rotation[0]),
        RadToDeg(inst->rotation[1]),
        RadToDeg(inst->rotation[2])
    };
    float scl[3] = { inst->scale[0], inst->scale[1], inst->scale[2] };

    // 2) Build the matrix:
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotationDeg, scl, outMatrix);
}

void DecomposeMatrixToInstance_ImGuizmo(const float* matrix, Instance* inst)
{
    float translation[3];
    float rotationDeg[3];
    float scl[3];

    ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotationDeg, scl);

    // Copy them back:
    inst->position[0] = translation[0];
    inst->position[1] = translation[1];
    inst->position[2] = translation[2];

    // Convert degrees to radians if your system is in radians:
    inst->rotation[0] = DegToRad(rotationDeg[0]);
    inst->rotation[1] = DegToRad(rotationDeg[1]);
    inst->rotation[2] = DegToRad(rotationDeg[2]);

    inst->scale[0] = scl[0];
    inst->scale[1] = scl[1];
    inst->scale[2] = scl[2];
}

// Sphere-based orientation gizmo (replaces cube-based ViewManipulate)
// Renders 6 colored spheres at each axis direction for camera orientation control
// The spheres are positioned based on how the world axes appear in the camera's view
// They stay in a circle in the gizmo area at the bottom-right, never disappearing
void DrawOrientationSphereGizmo(float* view, float* proj, ImVec2 position, ImVec2 size, float rectW, float rectH, Camera& cam)
{
    const float sphereRadius = 12.0f;
    const float centerX = position.x + size.x * 0.5f;
    const float centerY = position.y + size.y * 0.5f;
    const ImVec2 gizmoCenter(centerX, centerY);
    const float orbitRadius = std::min(size.x, size.y) * 0.35f;

    // Axis colors (matching the world axes: Red=X, Green=Y, Blue=Z)
    const ImVec4 colorX(1.0f, 0.2f, 0.2f, 0.8f);  // Red for X axis
    const ImVec4 colorY(0.2f, 1.0f, 0.2f, 0.8f);  // Green for Y axis
    const ImVec4 colorZ(0.2f, 0.2f, 1.0f, 0.8f);  // Blue for Z axis
    const ImVec4 colorBg(0.15f, 0.15f, 0.15f, 0.5f);  // Background

    // Get the draw list for rendering to the gizmo area
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Normalize camera basis vectors to ensure consistent projection
    bx::Vec3 camRight = bx::normalize(cam.right);
    bx::Vec3 camUp = bx::normalize(cam.up);

    // Helper function to project a world direction to a 2D position within the gizmo area
    // The spheres move freely within the circular region based on their 3D orientation
    auto directionToGizmoPos = [&](bx::Vec3 worldDir) -> ImVec2 {
        // Project the world direction onto the camera's view plane
        // using the camera's normalized right and up vectors
        float x = bx::dot(worldDir, camRight);
        float y = bx::dot(worldDir, camUp);
        
        // Use raw projections scaled to fill the circular region
        // This gives a true 3D visualization where the distance from center
        // indicates how much the axis is pointing toward/away from camera
        float screenX = gizmoCenter.x + x * orbitRadius;
        float screenY = gizmoCenter.y + y * orbitRadius;
        
        return ImVec2(screenX, screenY);
    };

    // Define the 6 sphere positions (axis directions)
    struct SphereAxis {
        ImVec2 screenPos;
        bx::Vec3 viewDirection;  // Direction to look along this axis
        ImVec4 color;
        const char* label;
        SphereAxis() : screenPos(0,0), viewDirection(0,0,0), color(0,0,0,0), label("") {}
        SphereAxis(ImVec2 sp, bx::Vec3 vd, ImVec4 c, const char* l) 
            : screenPos(sp), viewDirection(vd), color(c), label(l) {}
    };

    std::vector<SphereAxis> spheres;
    
    // Create 6 spheres for each axis direction
    // Positive/Negative X (Red)
    bx::Vec3 dirX(1.0f, 0.0f, 0.0f);
    bx::Vec3 dirNegX(-1.0f, 0.0f, 0.0f);
    spheres.push_back(SphereAxis(directionToGizmoPos(dirX), dirX, colorX, "+X"));
    spheres.push_back(SphereAxis(directionToGizmoPos(dirNegX), dirNegX, colorX, "-X"));
    
    // Positive/Negative Y (Green)
    bx::Vec3 dirY(0.0f, 1.0f, 0.0f);
    bx::Vec3 dirNegY(0.0f, -1.0f, 0.0f);
    spheres.push_back(SphereAxis(directionToGizmoPos(dirY), dirY, colorY, "+Y"));
    spheres.push_back(SphereAxis(directionToGizmoPos(dirNegY), dirNegY, colorY, "-Y"));
    
    // Positive/Negative Z (Blue)
    bx::Vec3 dirZ(0.0f, 0.0f, 1.0f);
    bx::Vec3 dirNegZ(0.0f, 0.0f, -1.0f);
    spheres.push_back(SphereAxis(directionToGizmoPos(dirZ), dirZ, colorZ, "+Z"));
    spheres.push_back(SphereAxis(directionToGizmoPos(dirNegZ), dirNegZ, colorZ, "-Z"));

    // Draw background circle
    drawList->AddCircleFilled(gizmoCenter, orbitRadius + sphereRadius + 4.0f, ImGui::GetColorU32(colorBg), 32);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    int hoveredSphere = -1;
    bool mousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // Draw spheres and check for interaction
    for (size_t i = 0; i < spheres.size(); ++i)
    {
        const SphereAxis& sphere = spheres[i];
        ImVec2 delta = ImVec2(mousePos.x - sphere.screenPos.x, mousePos.y - sphere.screenPos.y);
        float distSq = delta.x * delta.x + delta.y * delta.y;
        bool isHovered = distSq < (sphereRadius * sphereRadius);

        if (isHovered) {
            hoveredSphere = static_cast<int>(i);
        }

        // Draw sphere (as a filled circle with border)
        ImU32 sphereColor = ImGui::GetColorU32(isHovered ? 
            ImVec4(sphere.color.x * 1.3f, sphere.color.y * 1.3f, sphere.color.z * 1.3f, 1.0f) :
            sphere.color);
        
        drawList->AddCircleFilled(sphere.screenPos, sphereRadius, sphereColor, 32);
        drawList->AddCircle(sphere.screenPos, sphereRadius, 
                           ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.6f)), 32, 1.5f);

        // If clicked, snap camera to this axis direction looking at world origin
        if (isHovered && mousePressed)
        {
            // Position camera along the axis direction at a reasonable distance from origin
            float viewDistance = 20.0f;
            bx::Vec3 worldOrigin(0.0f, 0.0f, 0.0f);
            cam.position = bx::mul(sphere.viewDirection, viewDistance);
            
            // Make camera look at the world origin
            float viewTemp[16];
            bx::mtxLookAt(viewTemp, cam.position, worldOrigin, bx::Vec3(0.0f, 1.0f, 0.0f));
            
            // Extract new camera orientation from the view matrix
            float invView[16];
            bx::mtxInverse(invView, viewTemp);
            
            // Extract basis vectors from inverse view matrix (camera transform)
            // Column-major: first 3 columns are basis, last column is translation
            cam.right = bx::normalize(bx::Vec3(invView[0], invView[4], invView[8]));
            cam.up = bx::normalize(bx::Vec3(invView[1], invView[5], invView[9]));
            cam.front = bx::normalize(bx::sub(worldOrigin, cam.position));
            
            // Set FOV to a lower value for flat appearance
            cam.fov = 25.0f;
        }
    }

    // Handle dragging for smooth rotation
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && hoveredSphere >= 0)
    {
        // Calculate rotation based on mouse movement
        ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
        if (std::abs(mouseDelta.x) > 0.1f || std::abs(mouseDelta.y) > 0.1f)
        {
            // Simple trackball-like rotation around view center
            float rotSpeed = 0.01f;
            
            // Rotate around up axis (yaw)
            float yawAngle = mouseDelta.x * rotSpeed;
            
            // Create rotation matrix for yaw (around Y axis)
            float cosy = std::cos(yawAngle);
            float siny = std::sin(yawAngle);
            
            // Rotate cam.front and cam.right around world Y axis
            bx::Vec3 newFront(
                cam.front.x * cosy - cam.front.z * siny,
                cam.front.y,
                cam.front.x * siny + cam.front.z * cosy
            );
            cam.front = bx::normalize(newFront);
            
            bx::Vec3 newRight(
                cam.right.x * cosy - cam.right.z * siny,
                cam.right.y,
                cam.right.x * siny + cam.right.z * cosy
            );
            cam.right = bx::normalize(newRight);
            
            // Pitch rotation
            float pitchAngle = mouseDelta.y * rotSpeed;
            float cosp = std::cos(pitchAngle);
            float sinp = std::sin(pitchAngle);
            
            // Rotate cam.front and cam.up around the right axis
            newFront = bx::Vec3(
                cam.front.x * cosp + cam.up.x * sinp,
                cam.front.y * cosp + cam.up.y * sinp,
                cam.front.z * cosp + cam.up.z * sinp
            );
            cam.front = bx::normalize(newFront);
            
            bx::Vec3 newUp(
                -cam.front.x * sinp + cam.up.x * cosp,
                -cam.front.y * sinp + cam.up.y * cosp,
                -cam.front.z * sinp + cam.up.z * cosp
            );
            cam.up = bx::normalize(newUp);
        }
    }
}

void DrawGizmoForSelected(Instance* selectedInstance, float originX, float originY, const float* view, const float* proj, float rectWidth, float rectHeight)
{
    // Static state for this function:
    static bool wasUsing = false;
    static float oldPos[3] = { 0.0f, 0.0f, 0.0f };
    static float oldRot[3] = { 0.0f, 0.0f, 0.0f };
    static float oldScale[3] = { 0.0f, 0.0f, 0.0f };
    if (!selectedInstance)
        return;

    // 1) Build world matrix from the selected instance (correctly accounting for parent hierarchy)
    float matrix[16];
    BuildWorldMatrix(selectedInstance, matrix);

    // 2) Draw and hit-test in the current window (viewport); required for correct input when gizmo is in its own window
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(originX, originY, rectWidth, rectHeight);

    // 3) Store the original object's local transform before manipulation
    float localMatrix[16];
    bx::mtxSRT(localMatrix,
        selectedInstance->scale[0], selectedInstance->scale[1], selectedInstance->scale[2],
        selectedInstance->rotation[0], selectedInstance->rotation[1], selectedInstance->rotation[2],
        selectedInstance->position[0], selectedInstance->position[1], selectedInstance->position[2]);

    ImGuiIO& io = ImGui::GetIO();
    // Check if ALT is held down to enable snapping
    bool useSnap = io.KeyAlt;

    // Define snap settings for different operations (x, y, z for each operation)
    float snapTranslation[3] = { 0.5f, 0.5f, 0.5f };  // Snap all axes to 0.5 units
    float snapRotation[3] = { 90.0f, 90.0f, 90.0f };  // Snap all axes to 90 degrees
    float snapScale[3] = { 0.5f, 0.5f, 0.5f };        // Snap all axes to 0.5 scale incr

    // 4) Manipulate the world matrix
    bool changed = ImGuizmo::Manipulate(
        view, proj,
        currentGizmoOperation,
        currentGizmoMode,
        matrix,
        nullptr,
        useSnap ? (currentGizmoOperation == ImGuizmo::TRANSLATE ? snapTranslation :
            currentGizmoOperation == ImGuizmo::ROTATE ? snapRotation :
            snapScale) : nullptr
    );
    bool isUsing = ImGuizmo::IsUsing();

    if (isUsing && !wasUsing) {
        if (currentGizmoOperation == ImGuizmo::TRANSLATE) {
            // Store the old position before any changes
            oldPos[0] = selectedInstance->position[0];
            oldPos[1] = selectedInstance->position[1];
            oldPos[2] = selectedInstance->position[2];
        }
        else if (currentGizmoOperation == ImGuizmo::ROTATE) {
            // Store the old rotation before any changes
            oldRot[0] = selectedInstance->rotation[0];
            oldRot[1] = selectedInstance->rotation[1];
            oldRot[2] = selectedInstance->rotation[2];
        }
        else {
            // Store the old scale before any changes
            oldScale[0] = selectedInstance->scale[0];
            oldScale[1] = selectedInstance->scale[1];
            oldScale[2] = selectedInstance->scale[2];
        }
    }

    // 4) Drag end?
    if (!isUsing && wasUsing) {
        bool moved = false;
        bool rotated = false;
        bool scaled = false;
        if (currentGizmoOperation == ImGuizmo::TRANSLATE) {
            if (oldPos[0] != selectedInstance->position[0] ||
                oldPos[1] != selectedInstance->position[1] ||
                oldPos[2] != selectedInstance->position[2]) {
                moved = true;
            }
        }
        if (currentGizmoOperation == ImGuizmo::ROTATE) {
            if (oldRot[0] != selectedInstance->rotation[0] ||
                oldRot[1] != selectedInstance->rotation[1] ||
                oldRot[2] != selectedInstance->rotation[2]) {
                rotated = true;
            }
        }
        if (currentGizmoOperation == ImGuizmo::SCALE) {
            if (oldScale[0] != selectedInstance->scale[0] ||
                oldScale[1] != selectedInstance->scale[1] ||
                oldScale[2] != selectedInstance->scale[2]) {
                scaled = true;
            }
        }
        // Only push a command if something actually moved
        if (moved) {
            gCmdManager.executeCommand(
                std::make_unique<MoveCommand>(selectedInstance, oldPos[0], oldPos[1], oldPos[2], selectedInstance->position[0], selectedInstance->position[1], selectedInstance->position[2])
            );
        }

        if (rotated) {
            gCmdManager.executeCommand(
                std::make_unique<RotateCommand>(selectedInstance, oldRot[0], oldRot[1], oldRot[2], selectedInstance->rotation[0], selectedInstance->rotation[1], selectedInstance->rotation[2])
            );
        }

        if (scaled) {
            gCmdManager.executeCommand(
                std::make_unique<ScaleCommand>(selectedInstance, oldScale[0], oldScale[1], oldScale[2], selectedInstance->scale[0], selectedInstance->scale[1], selectedInstance->scale[2])
            );
        }
    }

    // 5) Update for next frame
    wasUsing = isUsing;
    // 5) If changed, convert world matrix back to local space
    if (changed)
    {
        if (selectedInstance->parent)
        {
            // Get parent's world matrix
            float parentWorld[16];
            BuildWorldMatrix(selectedInstance->parent, parentWorld);

            // Calculate parent's inverse matrix
            float parentInverse[16];
            bx::mtxInverse(parentInverse, parentWorld);

            // Convert world-space result back to local space
            float newLocalMatrix[16];
            bx::mtxMul(newLocalMatrix, matrix, parentInverse);

            // Extract the local transform values
            float translation[3];
            float rotationDeg[3];
            float scale[3];
            ImGuizmo::DecomposeMatrixToComponents(newLocalMatrix, translation, rotationDeg, scale);

            selectedInstance->position[0] = translation[0];
            selectedInstance->position[1] = translation[1];
            selectedInstance->position[2] = translation[2];

            // Convert degrees to radians and apply snapping if CTRL is held
            for (int i = 0; i < 3; i++) {
                float rotRad = DegToRad(rotationDeg[i]);

                // Apply additional rotation snapping if CTRL is held
                if (useSnap && currentGizmoOperation == ImGuizmo::ROTATE) {
                    // Convert to degrees, snap, and back to radians
                    float degrees = RadToDeg(rotRad);
                    float snapped = round(degrees / 90.0f) * 90.0f;
                    rotRad = DegToRad(snapped);
                }

                selectedInstance->rotation[i] = rotRad;
            }

            selectedInstance->scale[0] = scale[0];
            selectedInstance->scale[1] = scale[1];
            selectedInstance->scale[2] = scale[2];
        }
        else
        {
            // For root objects (no parent), decompose directly
            float translation[3];
            float rotationDeg[3];
            float scale[3];
            ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotationDeg, scale);

            selectedInstance->position[0] = translation[0];
            selectedInstance->position[1] = translation[1];
            selectedInstance->position[2] = translation[2];

            // Apply rotations with snapping if needed
            for (int i = 0; i < 3; i++) {
                float rotRad = DegToRad(rotationDeg[i]);

                // Apply rotation snapping if CTRL is held
                if (useSnap && currentGizmoOperation == ImGuizmo::ROTATE) {
                    float degrees = RadToDeg(rotRad);
                    float snapped = round(degrees / 90.0f) * 90.0f;
                    rotRad = DegToRad(snapped);
                }

                selectedInstance->rotation[i] = rotRad;
            }

            selectedInstance->scale[0] = scale[0];
            selectedInstance->scale[1] = scale[1];
            selectedInstance->scale[2] = scale[2];
        }
    }
}
//transfer to ObjLoader.cpp
void computeNormals(std::vector<PosColorVertex>& vertices, const std::vector<uint32_t>& indices) {

    // Reset all normals to zero
    for (auto& vertex : vertices) {
        vertex.nx = 0.0f;
        vertex.ny = 0.0f;
        vertex.nz = 0.0f;
    }
    // Create an array to accumulate normals
    std::vector<Vec3> accumulatedNormals(vertices.size(), { 0.0f, 0.0f, 0.0f });

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        Vec3 v0 = { vertices[i0].x, vertices[i0].y, vertices[i0].z };
        Vec3 v1 = { vertices[i1].x, vertices[i1].y, vertices[i1].z };
        Vec3 v2 = { vertices[i2].x, vertices[i2].y, vertices[i2].z };

        Vec3 edge1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
        Vec3 edge2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };

        Vec3 normal = {
            edge2.y * edge1.z - edge2.z * edge1.y,
            edge2.z * edge1.x - edge2.x * edge1.z,
            edge2.x * edge1.y - edge2.y * edge1.x
        };

        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0f) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }


        // Accumulate the normal for each vertex in the face
        accumulatedNormals[i0].x += normal.x; accumulatedNormals[i0].y += normal.y; accumulatedNormals[i0].z += normal.z;
        accumulatedNormals[i1].x += normal.x; accumulatedNormals[i1].y += normal.y; accumulatedNormals[i1].z += normal.z;
        accumulatedNormals[i2].x += normal.x; accumulatedNormals[i2].y += normal.y; accumulatedNormals[i2].z += normal.z;
    }

    // Normalize the accumulated normals for each vertex
    for (size_t i = 0; i < vertices.size(); i++) {
        Vec3& normal = accumulatedNormals[i];
        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0f) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }

        // Assign the normalized normal to the vertex
        vertices[i].nx = normal.x;
        vertices[i].ny = normal.y;
        vertices[i].nz = normal.z;
    }
}

// Register base mesh data helpers --------------------------------------------

// Build a MeshData from compile-time arrays.
template<size_t NVerts, size_t NIdx, typename IndexT>
static MeshData MakeMeshDataFromArrays(const PosColorVertex(&verts)[NVerts], const IndexT(&idx)[NIdx])
{
    MeshData mesh;
    mesh.vertices.assign(verts, verts + NVerts);
    mesh.indices.reserve(NIdx);
    for (size_t i = 0; i < NIdx; ++i)
    {
        mesh.indices.push_back(static_cast<uint32_t>(idx[i]));
    }
    return mesh;
}

// Build a MeshData from runtime vectors.
template<typename IndexT>
static MeshData MakeMeshDataFromVectors(const std::vector<PosColorVertex>& verts, const std::vector<IndexT>& idx)
{
    MeshData mesh;
    mesh.vertices = verts;
    mesh.indices.reserve(idx.size());
    for (size_t i = 0; i < idx.size(); ++i)
    {
        mesh.indices.push_back(static_cast<uint32_t>(idx[i]));
    }
    return mesh;
}

// Helper to attach a base mesh template to a newly created instance, based on its type.
static void RegisterInstanceMeshFromType(Instance* inst)
{
    if (!inst) return;
    auto it = g_BaseMeshData.find(inst->type);
    if (it != g_BaseMeshData.end())
    {
        g_InstanceMeshData[inst->id] = it->second;
    }
}

// Forward declaration so geometry tools can recreate GPU buffers.
void createMeshBuffers(const MeshData& meshData, bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh);

// Helper: get editable mesh data for an instance, if available.
static MeshData* GetEditableMeshData(Instance* inst)
{
    if (!inst) return nullptr;
    auto it = g_InstanceMeshData.find(inst->id);
    if (it == g_InstanceMeshData.end()) return nullptr;
    return &it->second;
}

static bool HasEditableMeshData(const Instance* inst)
{
    if (!inst || inst->isLight) return false;
    return g_InstanceMeshData.find(inst->id) != g_InstanceMeshData.end();
}

// Rebuild GPU buffers from CPU-side mesh data for the given instance.
static void ApplyEditableMeshToInstance(Instance* inst)
{
    if (!inst) return;
    auto it = g_InstanceMeshData.find(inst->id);
    if (it == g_InstanceMeshData.end()) return;

    MeshData& mesh = it->second;

    // Recompute normals after topology/position changes.
    computeNormals(mesh.vertices, mesh.indices);

    if (bgfx::isValid(inst->vertexBuffer))
    {
        bgfx::destroy(inst->vertexBuffer);
    }
    if (bgfx::isValid(inst->indexBuffer))
    {
        bgfx::destroy(inst->indexBuffer);
    }

    createMeshBuffers(mesh, inst->vertexBuffer, inst->indexBuffer);
}

// --- Geometry modification helpers (subdivision, merging, smoothing, boundaries) ---

// Simple utility to encode an undirected edge (i,j) into a 64-bit key.
static inline uint64_t EncodeEdge(uint32_t a, uint32_t b)
{
    return (uint64_t)std::min(a, b) << 32 | (uint64_t)std::max(a, b);
}

// Color boundary vertices (vertices that belong to at least one boundary edge).
static void ColorBoundaryVertices(MeshData& mesh, uint32_t boundaryAbgr)
{
    if (mesh.indices.size() < 3 || mesh.vertices.empty())
        return;

    std::unordered_map<uint64_t, uint32_t> edgeUseCount;
    edgeUseCount.reserve(mesh.indices.size());

    // Count how many faces reference each undirected edge.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        uint32_t i0 = mesh.indices[i + 0];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        ++edgeUseCount[EncodeEdge(i0, i1)];
        ++edgeUseCount[EncodeEdge(i1, i2)];
        ++edgeUseCount[EncodeEdge(i2, i0)];
    }

    std::vector<uint8_t> isBoundary(mesh.vertices.size(), 0);

    for (const auto& kv : edgeUseCount)
    {
        if (kv.second == 1)
        {
            uint32_t a = uint32_t(kv.first >> 32);
            uint32_t b = uint32_t(kv.first & 0xffffffffu);
            if (a < isBoundary.size()) isBoundary[a] = 1;
            if (b < isBoundary.size()) isBoundary[b] = 1;
        }
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        if (isBoundary[i])
        {
            mesh.vertices[i].abgr = boundaryAbgr;
        }
    }
}

// Naive triangle subdivision: each triangle is split into 4 using midpoints.
// ============================================================================
// Catmull-Clark Subdivision (Industry Standard - Like Blender/Maya)
// ============================================================================
// Simple Loop-based subdivision (one iteration) used as a fallback.
static void SubdivideMesh_LoopOnce(MeshData& mesh)
{
    if (mesh.indices.size() < 3 || mesh.vertices.empty())
        return;

    std::unordered_map<uint64_t, uint32_t> midpointIndex;
    midpointIndex.reserve(mesh.indices.size());

    std::vector<uint32_t> newIndices;
    newIndices.reserve(mesh.indices.size() * 4);

    // Create midpoints on edges
    auto getMidpoint = [&](uint32_t i0, uint32_t i1) -> uint32_t
    {
        uint64_t key = EncodeEdge(i0, i1);
        auto it = midpointIndex.find(key);
        if (it != midpointIndex.end())
            return it->second;

        PosColorVertex v0 = mesh.vertices[i0];
        PosColorVertex v1 = mesh.vertices[i1];

        PosColorVertex m = v0;
        m.x = 0.5f * (v0.x + v1.x);
        m.y = 0.5f * (v0.y + v1.y);
        m.z = 0.5f * (v0.z + v1.z);
        m.u = 0.5f * (v0.u + v1.u);
        m.v = 0.5f * (v0.v + v1.v);
        m.abgr = v0.abgr;
        m.nx = m.ny = m.nz = 0.0f;

        uint32_t newIndex = (uint32_t)mesh.vertices.size();
        mesh.vertices.push_back(m);
        midpointIndex[key] = newIndex;
        return newIndex;
    };

    // Split each triangle into 4
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        uint32_t i0 = mesh.indices[i + 0];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        uint32_t m01 = getMidpoint(i0, i1);
        uint32_t m12 = getMidpoint(i1, i2);
        uint32_t m20 = getMidpoint(i2, i0);

        newIndices.push_back(i0);   newIndices.push_back(m01); newIndices.push_back(m20);
        newIndices.push_back(i1);   newIndices.push_back(m12); newIndices.push_back(m01);
        newIndices.push_back(i2);   newIndices.push_back(m20); newIndices.push_back(m12);
        newIndices.push_back(m01);  newIndices.push_back(m12); newIndices.push_back(m20);
    }

    mesh.indices.swap(newIndices);
}

// Forward declaration of MergeCloseVertices (defined later)
static void MergeCloseVertices(MeshData& mesh, float epsilon);

static void SubdivideMesh_CatmullClark(MeshData& mesh)
{
    if (mesh.indices.size() < 3 || mesh.vertices.empty())
        return;

    // Weld any nearly-duplicate vertices up front – many primitives and some
    // imports create separate vertices for each face which breaks adjacency
    // and leads to spike artifacts.  Use a tiny epsilon so only exact duplicates
    // collapse.
    MergeCloseVertices(mesh, 0.0001f);

    const size_t originalVertexCount = mesh.vertices.size();
    const size_t triangleCount = mesh.indices.size() / 3;

    // Build per-vertex adjacency info
    std::vector<std::vector<uint32_t>> vertexFaces(originalVertexCount);  // faces touching each vertex
    std::vector<std::vector<uint32_t>> vertexNeighbors(originalVertexCount);  // adjacent vertices

    for (size_t faceIdx = 0; faceIdx < triangleCount; ++faceIdx)
    {
        uint32_t i0 = mesh.indices[faceIdx * 3 + 0];
        uint32_t i1 = mesh.indices[faceIdx * 3 + 1];
        uint32_t i2 = mesh.indices[faceIdx * 3 + 2];

        // Record face adjacency
        vertexFaces[i0].push_back((uint32_t)faceIdx);
        vertexFaces[i1].push_back((uint32_t)faceIdx);
        vertexFaces[i2].push_back((uint32_t)faceIdx);

        // Record vertex neighbors (avoid duplicates)
        auto addNeighbor = [](std::vector<uint32_t>& neighbors, uint32_t v)
        {
            if (std::find(neighbors.begin(), neighbors.end(), v) == neighbors.end())
                neighbors.push_back(v);
        };

        addNeighbor(vertexNeighbors[i0], i1);
        addNeighbor(vertexNeighbors[i0], i2);
        addNeighbor(vertexNeighbors[i1], i0);
        addNeighbor(vertexNeighbors[i1], i2);
        addNeighbor(vertexNeighbors[i2], i0);
        addNeighbor(vertexNeighbors[i2], i1);
    }

    std::vector<PosColorVertex> newVertices = mesh.vertices;
    newVertices.reserve(originalVertexCount + mesh.indices.size() / 2);

    std::vector<uint32_t> newIndices;
    newIndices.reserve(mesh.indices.size() * 4);

    // Step 1: Create face points (one per triangle)
    std::vector<uint32_t> facePointIndices;
    facePointIndices.reserve(triangleCount);

    for (size_t faceIdx = 0; faceIdx < triangleCount; ++faceIdx)
    {
        uint32_t i0 = mesh.indices[faceIdx * 3 + 0];
        uint32_t i1 = mesh.indices[faceIdx * 3 + 1];
        uint32_t i2 = mesh.indices[faceIdx * 3 + 2];

        const PosColorVertex& v0 = mesh.vertices[i0];
        const PosColorVertex& v1 = mesh.vertices[i1];
        const PosColorVertex& v2 = mesh.vertices[i2];

        PosColorVertex facePoint;
        facePoint.x = (v0.x + v1.x + v2.x) / 3.0f;
        facePoint.y = (v0.y + v1.y + v2.y) / 3.0f;
        facePoint.z = (v0.z + v1.z + v2.z) / 3.0f;
        facePoint.u = (v0.u + v1.u + v2.u) / 3.0f;
        facePoint.v = (v0.v + v1.v + v2.v) / 3.0f;
        facePoint.nx = 0.0f;  // Recomputed after subdivision
        facePoint.ny = 0.0f;
        facePoint.nz = 0.0f;
        facePoint.abgr = v0.abgr;  // Use first vertex's color

        uint32_t facePointIdx = (uint32_t)newVertices.size();
        newVertices.push_back(facePoint);
        facePointIndices.push_back(facePointIdx);
    }

    // Step 2: Create edge points and track them
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgePointMap;

    auto getOrCreateEdgePoint = [&](uint32_t v0, uint32_t v1) -> uint32_t
    {
        if (v0 > v1) std::swap(v0, v1);
        auto key = std::make_pair(v0, v1);

        auto it = edgePointMap.find(key);
        if (it != edgePointMap.end())
            return it->second;

        // Edge point = average of edge endpoints + adjacent face points
        const PosColorVertex& pv0 = mesh.vertices[v0];
        const PosColorVertex& pv1 = mesh.vertices[v1];

        // Find faces adjacent to this edge
        std::vector<uint32_t> adjacentFaces;
        for (uint32_t f : vertexFaces[v0])
        {
            if (std::find(vertexFaces[v1].begin(), vertexFaces[v1].end(), f) != vertexFaces[v1].end())
                adjacentFaces.push_back(f);
        }

        PosColorVertex edgePoint;
        edgePoint.x = pv0.x + pv1.x;
        edgePoint.y = pv0.y + pv1.y;
        edgePoint.z = pv0.z + pv1.z;
        edgePoint.u = pv0.u + pv1.u;
        edgePoint.v = pv0.v + pv1.v;

        // Add face point contributions (averaged)
        for (uint32_t faceIdx : adjacentFaces)
        {
            const PosColorVertex& fp = newVertices[facePointIndices[faceIdx]];
            edgePoint.x += fp.x;
            edgePoint.y += fp.y;
            edgePoint.z += fp.z;
            edgePoint.u += fp.u;
            edgePoint.v += fp.v;
        }

        // Average: (v0 + v1 + f0 + f1) / 4 for internal edges
        float denom = 2.0f + (float)adjacentFaces.size();
        edgePoint.x /= denom;
        edgePoint.y /= denom;
        edgePoint.z /= denom;
        edgePoint.u /= denom;
        edgePoint.v /= denom;
        edgePoint.nx = 0.0f;  // Recomputed
        edgePoint.ny = 0.0f;
        edgePoint.nz = 0.0f;
        edgePoint.abgr = pv0.abgr;

        uint32_t edgePointIdx = (uint32_t)newVertices.size();
        newVertices.push_back(edgePoint);
        edgePointMap[key] = edgePointIdx;

        return edgePointIdx;
    };

    // Step 3: Move original vertices using Catmull-Clark formula
    std::vector<PosColorVertex> movedVertices = mesh.vertices;
    bool clampedOccurred = false;

    for (uint32_t i = 0; i < originalVertexCount; ++i)
    {
        const std::vector<uint32_t>& adjacentFaces = vertexFaces[i];
        const std::vector<uint32_t>& neighbors = vertexNeighbors[i];

        if (adjacentFaces.empty() || neighbors.empty())
            continue;

        uint32_t n = (uint32_t)adjacentFaces.size();

        // F = average of face points of faces touching this vertex
        float Fx = 0.0f, Fy = 0.0f, Fz = 0.0f;
        for (uint32_t faceIdx : adjacentFaces)
        {
            const PosColorVertex& fp = newVertices[facePointIndices[faceIdx]];
            Fx += fp.x;
            Fy += fp.y;
            Fz += fp.z;
        }
        Fx /= (float)n;
        Fy /= (float)n;
        Fz /= (float)n;

        // R = average of edge midpoints for edges of this vertex
        // each edge midpoint = (P + neighbor) / 2
        float Rx = 0.0f, Ry = 0.0f, Rz = 0.0f;
        const PosColorVertex& P = mesh.vertices[i];
        for (uint32_t neighbor : neighbors)
        {
            const PosColorVertex& nv = mesh.vertices[neighbor];
            Rx += (P.x + nv.x) * 0.5f;
            Ry += (P.y + nv.y) * 0.5f;
            Rz += (P.z + nv.z) * 0.5f;
        }
        Rx /= (float)neighbors.size();
        Ry /= (float)neighbors.size();
        Rz /= (float)neighbors.size();

        // Catmull-Clark formula: (F + 2*R + (n-3)*P) / n
        float nf = (float)n;
        PosColorVertex newPos;
        newPos.x = (Fx + 2.0f * Rx + (nf - 3.0f) * P.x) / nf;
        newPos.y = (Fy + 2.0f * Ry + (nf - 3.0f) * P.y) / nf;
        newPos.z = (Fz + 2.0f * Rz + (nf - 3.0f) * P.z) / nf;
        
        // Clamp movement to avoid spikes (limit distance from original to some fraction of average edge length)
        {
            float avgEdgeLen = 0.0f;
            for (uint32_t neighbor : neighbors)
            {
                const PosColorVertex& nv = mesh.vertices[neighbor];
                float dx = nv.x - P.x;
                float dy = nv.y - P.y;
                float dz = nv.z - P.z;
                avgEdgeLen += sqrtf(dx*dx + dy*dy + dz*dz);
            }
            if (!neighbors.empty())
                avgEdgeLen /= (float)neighbors.size();

            float maxMove = avgEdgeLen * 0.5f; // half the average edge length
            float dx = newPos.x - P.x;
            float dy = newPos.y - P.y;
            float dz = newPos.z - P.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist > maxMove && dist > 0.0f)
            {
                clampedOccurred = true;
                float scale = maxMove / dist;
                newPos.x = P.x + dx * scale;
                newPos.y = P.y + dy * scale;
                newPos.z = P.z + dz * scale;
#ifdef _DEBUG
                // debug print a warning for problematic vertices
                printf("[Subdivision] clamped vertex %u move (orig %.3f %.3f %.3f -> new %.3f %.3f %.3f)\n",
                       i, P.x, P.y, P.z, newPos.x, newPos.y, newPos.z);
#endif
            }
        }

        movedVertices[i] = newPos;
        // UVs and color stay the same for moved vertices
    }

    // Replace original vertices with moved ones in the new vertex list
    for (uint32_t i = 0; i < originalVertexCount; ++i)
    {
        newVertices[i] = movedVertices[i];
    }

    // Log clamping occurrences for diagnostic purposes.  We no longer perform
    // a hard fallback; clamping by itself guarantees there will be no extreme
    // spikes, and keeping the Catmull-Clark topology yields smoother results
    // than switching back to Loop.
    if (clampedOccurred)
    {
        printf("[Subdivision] some vertex movements were clamped (mesh may have had poor topology)\n");
    }

    // Step 4: Rebuild indices connecting face points, edge points, and moved vertices
    for (size_t faceIdx = 0; faceIdx < triangleCount; ++faceIdx)
    {
        uint32_t i0 = mesh.indices[faceIdx * 3 + 0];
        uint32_t i1 = mesh.indices[faceIdx * 3 + 1];
        uint32_t i2 = mesh.indices[faceIdx * 3 + 2];

        uint32_t fp = facePointIndices[faceIdx];  // Central face point

        // Edge points for this triangle
        uint32_t e01 = getOrCreateEdgePoint(i0, i1);
        uint32_t e12 = getOrCreateEdgePoint(i1, i2);
        uint32_t e20 = getOrCreateEdgePoint(i2, i0);

        // Create 6 smaller triangles from the original triangle
        // Each original vertex gets a quad that includes the face point
        
        // Quad around vertex i0: (i0, e01, fp, e20)
        newIndices.push_back(i0);   newIndices.push_back(e01);  newIndices.push_back(fp);
        newIndices.push_back(i0);   newIndices.push_back(fp);   newIndices.push_back(e20);

        // Quad around vertex i1: (i1, e12, fp, e01)
        newIndices.push_back(i1);   newIndices.push_back(e12);  newIndices.push_back(fp);
        newIndices.push_back(i1);   newIndices.push_back(fp);   newIndices.push_back(e01);

        // Quad around vertex i2: (i2, e20, fp, e12)
        newIndices.push_back(i2);   newIndices.push_back(e20);  newIndices.push_back(fp);
        newIndices.push_back(i2);   newIndices.push_back(fp);   newIndices.push_back(e12);
    }

    mesh.vertices = std::move(newVertices);
    mesh.indices = std::move(newIndices);
}

static void SubdivideMesh(MeshData& mesh, int levels)
{
    levels = std::max(0, std::min(levels, 3));  // Cap at 3 levels for performance
    for (int level = 0; level < levels; ++level)
    {
        if (mesh.indices.size() < 3) break;
        SubdivideMesh_CatmullClark(mesh);
    }
}

// Merge vertices that are closer than epsilon in object space.
static void MergeCloseVertices(MeshData& mesh, float epsilon)
{
    if (mesh.vertices.empty() || mesh.indices.empty() || epsilon <= 0.0f)
        return;

    const float eps2 = epsilon * epsilon;
    const size_t oldCount = mesh.vertices.size();

    std::vector<uint32_t> remap(oldCount, UINT32_MAX);
    std::vector<PosColorVertex> newVerts;
    newVerts.reserve(oldCount);

    for (uint32_t i = 0; i < oldCount; ++i)
    {
        if (remap[i] != UINT32_MAX)
            continue;

        PosColorVertex base = mesh.vertices[i];
        float accumX = base.x, accumY = base.y, accumZ = base.z;
        uint32_t mergedCount = 1;

        remap[i] = (uint32_t)newVerts.size();

        for (uint32_t j = i + 1; j < oldCount; ++j)
        {
            if (remap[j] != UINT32_MAX)
                continue;

            const PosColorVertex& v = mesh.vertices[j];
            float dx = v.x - base.x;
            float dy = v.y - base.y;
            float dz = v.z - base.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= eps2)
            {
                remap[j] = remap[i];
                accumX += v.x;
                accumY += v.y;
                accumZ += v.z;
                ++mergedCount;
            }
        }

        // Average position; keep other attributes from the base vertex.
        base.x = accumX / float(mergedCount);
        base.y = accumY / float(mergedCount);
        base.z = accumZ / float(mergedCount);

        newVerts.push_back(base);
    }

    // Remap indices.
    for (uint32_t& idx : mesh.indices)
    {
        if (idx < remap.size() && remap[idx] != UINT32_MAX)
        {
            idx = remap[idx];
        }
    }

    mesh.vertices.swap(newVerts);
}

//// Simple Laplacian smoothing.
//static void SmoothMesh(MeshData& mesh, int iterations, float factor)
//{
//    if (mesh.vertices.empty() || mesh.indices.empty())
//        return;
//
//    iterations = std::max(0, iterations);
//    factor = std::max(0.0f, std::min(factor, 1.0f));
//    if (iterations == 0 || factor <= 0.0f)
//        return;
//
//    std::vector<std::vector<uint32_t>> adjacency(mesh.vertices.size());
//    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
//    {
//        uint32_t i0 = mesh.indices[i + 0];
//        uint32_t i1 = mesh.indices[i + 1];
//        uint32_t i2 = mesh.indices[i + 2];
//
//        adjacency[i0].push_back(i1);
//        adjacency[i0].push_back(i2);
//        adjacency[i1].push_back(i0);
//        adjacency[i1].push_back(i2);
//        adjacency[i2].push_back(i0);
//        adjacency[i2].push_back(i1);
//    }
//
//    std::vector<PosColorVertex> temp = mesh.vertices;
//
//    for (int it = 0; it < iterations; ++it)
//    {
//        temp = mesh.vertices;
//
//        for (size_t i = 0; i < mesh.vertices.size(); ++i)
//        {
//            const auto& nbrs = adjacency[i];
//            if (nbrs.empty())
//                continue;
//
//            float ax = 0.0f, ay = 0.0f, az = 0.0f;
//            for (uint32_t n : nbrs)
//            {
//                ax += temp[n].x;
//                ay += temp[n].y;
//                az += temp[n].z;
//            }
//            float inv = 1.0f / float(nbrs.size());
//            ax *= inv; ay *= inv; az *= inv;
//
//            PosColorVertex& v = mesh.vertices[i];
//            v.x = v.x + factor * (ax - v.x);
//            v.y = v.y + factor * (ay - v.y);
//            v.z = v.z + factor * (az - v.z);
//        }
//    }
//}
static void SubdivideOnce(MeshData& mesh)
{
    std::vector<PosColorVertex> newVerts = mesh.vertices;
    std::vector<uint32_t> newIndices;

    std::map<std::pair<uint16_t, uint16_t>, uint16_t> midpointCache;

    auto getMidpoint = [&](uint16_t a, uint16_t b) -> uint16_t
        {
            if (a > b) std::swap(a, b);

            auto key = std::make_pair(a, b);
            auto it = midpointCache.find(key);
            if (it != midpointCache.end())
                return it->second;

            PosColorVertex va = mesh.vertices[a];
            PosColorVertex vb = mesh.vertices[b];

            PosColorVertex mid;
            mid.x = (va.x + vb.x) * 0.5f;
            mid.y = (va.y + vb.y) * 0.5f;
            mid.z = (va.z + vb.z) * 0.5f;

            uint16_t index = (uint16_t)newVerts.size();
            newVerts.push_back(mid);
            midpointCache[key] = index;

            return index;
        };

    for (size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        uint16_t i0 = mesh.indices[i];
        uint16_t i1 = mesh.indices[i + 1];
        uint16_t i2 = mesh.indices[i + 2];

        uint16_t m0 = getMidpoint(i0, i1);
        uint16_t m1 = getMidpoint(i1, i2);
        uint16_t m2 = getMidpoint(i2, i0);

        // Split triangle into 4
        newIndices.insert(newIndices.end(), {
            i0, m0, m2,
            i1, m1, m0,
            i2, m2, m1,
            m0, m1, m2
            });
    }

    mesh.vertices = newVerts;
    mesh.indices = newIndices;
}


struct Vec3Key {
    int32_t x, y, z; // Use scaled integers for robust matching
    bool operator<(const Vec3Key& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

void computeNormals(std::vector<PosColorVertex>& vertices, const std::vector<uint16_t>& indices) {
    // 1. Zero out all normals
    for (auto& v : vertices) { v.nx = v.ny = v.nz = 0.0f; }

    // 2. Accumulate triangle normals
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto& v0 = vertices[indices[i]];
        auto& v1 = vertices[indices[i + 1]];
        auto& v2 = vertices[indices[i + 2]];

        float dx1 = v1.x - v0.x; float dy1 = v1.y - v0.y; float dz1 = v1.z - v0.z;
        float dx2 = v2.x - v0.x; float dy2 = v2.y - v0.y; float dz2 = v2.z - v0.z;

        // Cross product
        float nx = dy1 * dz2 - dz1 * dy2;
        float ny = dz1 * dx2 - dx1 * dz2;
        float nz = dx1 * dy2 - dy1 * dx2;

        v0.nx += nx; v0.ny += ny; v0.nz += nz;
        v1.nx += nx; v1.ny += ny; v1.nz += nz;
        v2.nx += nx; v2.ny += ny; v2.nz += nz;
    }

    // 3. Normalize
    for (auto& v : vertices) {
        float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (len > 0.0f) { v.nx /= len; v.ny /= len; v.nz /= len; }
    }
}

static void SmoothMesh(MeshData& mesh, int iterations, float factor)
{
    if (mesh.vertices.empty() || mesh.indices.empty()) return;

    size_t vertexCount = mesh.vertices.size();

    // ------------------------------------------------------------
    // 1. Weld duplicate vertices (by position)
    // ------------------------------------------------------------
    std::map<Vec3Key, uint32_t> posToUniqueId;
    std::vector<uint32_t> vertexToUniqueId(vertexCount);

    struct Vec3 { float x, y, z; };
    std::vector<Vec3> uniquePositions;

    auto toKey = [](float f) { return (int32_t)(f * 1000.0f); };

    for (size_t i = 0; i < vertexCount; ++i)
    {
        Vec3Key key = {
            toKey(mesh.vertices[i].x),
            toKey(mesh.vertices[i].y),
            toKey(mesh.vertices[i].z)
        };

        auto it = posToUniqueId.find(key);
        if (it == posToUniqueId.end())
        {
            uint32_t newId = (uint32_t)uniquePositions.size();
            posToUniqueId[key] = newId;
            vertexToUniqueId[i] = newId;

            uniquePositions.push_back({
                mesh.vertices[i].x,
                mesh.vertices[i].y,
                mesh.vertices[i].z
                });
        }
        else
        {
            vertexToUniqueId[i] = it->second;
        }
    }

    // ------------------------------------------------------------
    // 2. Build adjacency (unique IDs only)
    // ------------------------------------------------------------
    std::vector<std::set<uint32_t>> adj(uniquePositions.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        uint32_t u0 = vertexToUniqueId[mesh.indices[i]];
        uint32_t u1 = vertexToUniqueId[mesh.indices[i + 1]];
        uint32_t u2 = vertexToUniqueId[mesh.indices[i + 2]];

        auto addEdge = [&](uint32_t a, uint32_t b)
            {
                if (a != b) adj[a].insert(b);
            };

        addEdge(u0, u1); addEdge(u0, u2);
        addEdge(u1, u0); addEdge(u1, u2);
        addEdge(u2, u0); addEdge(u2, u1);
    }

    // ------------------------------------------------------------
    // 3. Laplacian smoothing (FLOAT math!)
    // ------------------------------------------------------------
 /*   for (int it = 0; it < iterations; ++it)
    {
        std::vector<Vec3> next = uniquePositions;

        for (size_t i = 0; i < uniquePositions.size(); ++i)
        {
            if (adj[i].empty()) continue;

            float ax = 0, ay = 0, az = 0;

            for (uint32_t n : adj[i])
            {
                ax += uniquePositions[n].x;
                ay += uniquePositions[n].y;
                az += uniquePositions[n].z;
            }

            float inv = 1.0f / adj[i].size();

            ax *= inv;
            ay *= inv;
            az *= inv;

            next[i].x = uniquePositions[i].x + factor * (ax - uniquePositions[i].x);
            next[i].y = uniquePositions[i].y + factor * (ay - uniquePositions[i].y);
            next[i].z = uniquePositions[i].z + factor * (az - uniquePositions[i].z);
        }

        uniquePositions = next;
    }*/
    // ------------------------------------------------------------
// 3. Proper Taubin smoothing with volume preservation
// ------------------------------------------------------------

    float lambda = factor;        // 0.3f recommended
    float mu = -lambda * 0.5f;    // shrink cancel

    for (int it = 0; it < iterations; ++it)
    {
        std::vector<Vec3> lap(uniquePositions.size());

        // Compute Laplacian
        for (size_t i = 0; i < uniquePositions.size(); ++i)
        {
            if (adj[i].empty()) continue;

            float ax = 0, ay = 0, az = 0;
            for (uint32_t n : adj[i])
            {
                ax += uniquePositions[n].x;
                ay += uniquePositions[n].y;
                az += uniquePositions[n].z;
            }

            float inv = 1.0f / adj[i].size();
            ax *= inv; ay *= inv; az *= inv;

            lap[i].x = ax - uniquePositions[i].x;
            lap[i].y = ay - uniquePositions[i].y;
            lap[i].z = az - uniquePositions[i].z;
        }

        // Lambda pass
        for (size_t i = 0; i < uniquePositions.size(); ++i)
        {
            uniquePositions[i].x += lambda * lap[i].x;
            uniquePositions[i].y += lambda * lap[i].y;
            uniquePositions[i].z += lambda * lap[i].z;
        }

        // Recompute Laplacian after lambda
        for (size_t i = 0; i < uniquePositions.size(); ++i)
        {
            if (adj[i].empty()) continue;

            float ax = 0, ay = 0, az = 0;
            for (uint32_t n : adj[i])
            {
                ax += uniquePositions[n].x;
                ay += uniquePositions[n].y;
                az += uniquePositions[n].z;
            }

            float inv = 1.0f / adj[i].size();
            ax *= inv; ay *= inv; az *= inv;

            lap[i].x = ax - uniquePositions[i].x;
            lap[i].y = ay - uniquePositions[i].y;
            lap[i].z = az - uniquePositions[i].z;
        }

        // Mu pass
        for (size_t i = 0; i < uniquePositions.size(); ++i)
        {
            uniquePositions[i].x += mu * lap[i].x;
            uniquePositions[i].y += mu * lap[i].y;
            uniquePositions[i].z += mu * lap[i].z;
        }
    }


    // ------------------------------------------------------------
    // 4. Write back to original mesh
    // ------------------------------------------------------------
    for (size_t i = 0; i < vertexCount; ++i)
    {
        uint32_t uid = vertexToUniqueId[i];

        mesh.vertices[i].x = uniquePositions[uid].x;
        mesh.vertices[i].y = uniquePositions[uid].y;
        mesh.vertices[i].z = uniquePositions[uid].z;
    }

    // ------------------------------------------------------------
    // 5. Recompute normals
    // ------------------------------------------------------------
    computeNormals(mesh.vertices, mesh.indices);
}


// Transform a position by a 4x4 matrix.
static void TransformPosition(const float* m, float x, float y, float z, float& outX, float& outY, float& outZ)
{
    outX = m[0] * x + m[4] * y + m[8]  * z + m[12];
    outY = m[1] * x + m[5] * y + m[9]  * z + m[13];
    outZ = m[2] * x + m[6] * y + m[10] * z + m[14];
}

// Draw selected instance's mesh as green vertices (small crosses) and green edges.
static void DrawSelectedMeshOverlay(const Instance* inst, const float* worldMatrix, uint16_t viewId)
{
    if (!inst) return;

    auto it = g_InstanceMeshData.find(inst->id);
    if (it == g_InstanceMeshData.end())
        return;

    const MeshData& mesh = it->second;
    if (mesh.vertices.empty() || mesh.indices.empty())
        return;

    const uint32_t edgeColor = PackAbgr(0xff, 0x20, 0xff, 0x20); // bright green
    const uint32_t vertColor = PackAbgr(0xff, 0x40, 0xff, 0x40); // slightly brighter green

    std::vector<LineVertex> verts;
    verts.reserve(mesh.indices.size() * 2 + mesh.vertices.size() * 6);

    auto pushLine = [&](float x0, float y0, float z0,
                        float x1, float y1, float z1,
                        uint32_t abgr)
    {
        LineVertex a{};
        a.x = x0; a.y = y0; a.z = z0;
        a.nx = 0.0f; a.ny = 1.0f; a.nz = 0.0f;
        a.abgr = abgr;
        a.u = 0.0f; a.v = 0.0f;

        LineVertex b{};
        b.x = x1; b.y = y1; b.z = z1;
        b.nx = 0.0f; b.ny = 1.0f; b.nz = 0.0f;
        b.abgr = abgr;
        b.u = 0.0f; b.v = 0.0f;

        verts.push_back(a);
        verts.push_back(b);
    };

    // Edges: draw each triangle edge as a green segment.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        uint32_t i0 = mesh.indices[i + 0];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            continue;

        const PosColorVertex& v0 = mesh.vertices[i0];
        const PosColorVertex& v1 = mesh.vertices[i1];
        const PosColorVertex& v2 = mesh.vertices[i2];

        float x0, y0, z0;
        float x1, y1, z1;
        float x2, y2, z2;
        TransformPosition(worldMatrix, v0.x, v0.y, v0.z, x0, y0, z0);
        TransformPosition(worldMatrix, v1.x, v1.y, v1.z, x1, y1, z1);
        TransformPosition(worldMatrix, v2.x, v2.y, v2.z, x2, y2, z2);

        pushLine(x0, y0, z0, x1, y1, z1, edgeColor);
        pushLine(x1, y1, z1, x2, y2, z2, edgeColor);
        pushLine(x2, y2, z2, x0, y0, z0, edgeColor);
    }

    // Vertices: small crosses around each vertex position.
    const float r = 0.01f; // cross half-size in world units
    for (const auto& v : mesh.vertices)
    {
        float cx, cy, cz;
        TransformPosition(worldMatrix, v.x, v.y, v.z, cx, cy, cz);

        pushLine(cx - r, cy, cz, cx + r, cy, cz, vertColor);
        pushLine(cx, cy - r, cz, cx, cy + r, cz, vertColor);
        pushLine(cx, cy, cz - r, cx, cy, cz + r, vertColor);
    }

    if (!verts.empty())
    {
        SubmitLineList(viewId, unlitColorProgram, verts.data(), static_cast<uint32_t>(verts.size()),
            BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS);
    }
}

std::string openFileDialog(bool save) {
#ifdef _WIN32
    char filePath[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn; // Windows File Picker Struct
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "txt";

    if (save) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        if (GetSaveFileNameA(&ofn))
            return std::string(filePath);
    }
    else {
        if (GetOpenFileNameA(&ofn))
            return std::string(filePath);
    }
#endif
    return "";
}

MeshData loadMesh(const std::string& filePath) {
    Assimp::Importer importer;
    // Ensure vertex colors are loaded - OBJ files may have vertex colors
    unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs;
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    std::cout << "[DEBUG loadMesh] Assimp import flags: Triangulate | FlipUVs" << std::endl;

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error: Assimp - " << importer.GetErrorString() << std::endl;
        return {};
    }

    std::vector<PosColorVertex> vertices;
    std::vector<uint32_t> indices;

    // used for making origin of imported objects at 0 0 0 to center gizmo
    // works for both single mesh and multi mesh objects
    // Global bounding box for the entire model.
    float globalMinX = FLT_MAX, globalMinY = FLT_MAX, globalMinZ = FLT_MAX;
    float globalMaxX = -FLT_MAX, globalMaxY = -FLT_MAX, globalMaxZ = -FLT_MAX;

    // Iterate through all meshes in the scene
    std::cout << "[DEBUG loadMesh] Loading mesh file: " << filePath << std::endl;
    std::cout << "[DEBUG loadMesh] Total meshes in scene: " << scene->mNumMeshes << std::endl;
    
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        size_t baseIndex = vertices.size();
        std::unordered_map<std::string, uint16_t> uniqueVertices;

        std::cout << "[DEBUG loadMesh] Processing mesh " << meshIndex 
                  << ": " << mesh->mNumVertices << " vertices, " 
                  << mesh->mNumFaces << " faces" << std::endl;
        std::cout << "[DEBUG loadMesh] HasVertexColors(0): " << (mesh->HasVertexColors(0) ? "YES" : "NO") << std::endl;
        std::cout << "[DEBUG loadMesh] HasNormals: " << (mesh->HasNormals() ? "YES" : "NO") << std::endl;
        std::cout << "[DEBUG loadMesh] HasTextureCoords(0): " << (mesh->HasTextureCoords(0) ? "YES" : "NO") << std::endl;

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            PosColorVertex vertex;
            vertex.x = -mesh->mVertices[i].x; //original: mesh->mVertices[i].x | flipped: -mesh->mVertices[i].x 
            vertex.y = mesh->mVertices[i].y;
            vertex.z = mesh->mVertices[i].z;

            // Update global bounding box.
            if (vertex.x < globalMinX) globalMinX = vertex.x;
            if (vertex.y < globalMinY) globalMinY = vertex.y;
            if (vertex.z < globalMinZ) globalMinZ = vertex.z;
            if (vertex.x > globalMaxX) globalMaxX = vertex.x;
            if (vertex.y > globalMaxY) globalMaxY = vertex.y;
            if (vertex.z > globalMaxZ) globalMaxZ = vertex.z;

            // Retrieve normals if available
            if (mesh->HasNormals()) {
                vertex.nx = mesh->mNormals[i].x;
                vertex.ny = mesh->mNormals[i].y;
                vertex.nz = mesh->mNormals[i].z;
            }
            else {
                vertex.nx = 0.0f;
                vertex.ny = 0.0f;
                vertex.nz = 0.0f; // Default to zero, will recompute later
            }

            // Retrieve texture coordinates (UVs) if available.
            if (mesh->HasTextureCoords(0)) {
                // Assimp stores texture coordinates in a 3D vector; we use only x and y.
                vertex.u = mesh->mTextureCoords[0][i].x;
                vertex.v = mesh->mTextureCoords[0][i].y;
            }
            else {
                vertex.u = 0.0f;
                vertex.v = 0.0f;
            }

            if (mesh->HasVertexColors(0)) {
                // Convert RGBA to ABGR format (A in bits 24-31, B in 16-23, G in 8-15, R in 0-7)
                float r = mesh->mColors[0][i].r;
                float g = mesh->mColors[0][i].g;
                float b = mesh->mColors[0][i].b;
                float a = mesh->mColors[0][i].a;
                vertex.abgr = ((uint8_t)(a * 255) << 24) |
                    ((uint8_t)(b * 255) << 16) |
                    ((uint8_t)(g * 255) << 8) |
                    (uint8_t)(r * 255);
                
                // Debug: Log first few vertices with colors
                if (i < 3) {
                    std::cout << "[DEBUG loadMesh] Vertex " << i << " has color: RGBA(" 
                              << r << ", " << g << ", " << b << ", " << a 
                              << ") -> ABGR(0x" << std::hex << vertex.abgr << std::dec << ")" << std::endl;
                }
            }
            else {
                vertex.abgr = 0xffffffff; // Default color (white)
                if (i == 0) {
                    std::cout << "[DEBUG loadMesh] Mesh has NO vertex colors, using default white (0xFFFFFFFF)" << std::endl;
                }
            }

            vertices.push_back(vertex);
        }


        /*
        // Reversed winding order
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            // Reverse the winding order
            for (int j = face.mNumIndices - 1; j >= 0; j--) {
                indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[j]));
            }
        }
        */
        // Natural winding order
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[j]));
            }
        }

        // Compute normals if the mesh does not have them
        if (!mesh->HasNormals()) {
            computeNormals(vertices, indices);
        }
    }

    // Compute the global center.
    float centerX = (globalMinX + globalMaxX) * 0.5f;
    float centerY = (globalMinY + globalMaxY) * 0.5f;
    float centerZ = (globalMinZ + globalMaxZ) * 0.5f;

    // Second pass: recenter all vertices by subtracting the global center.
    for (auto& v : vertices) {
        v.x -= centerX;
        v.y -= centerY;
        v.z -= centerZ;
    }

    return { vertices, indices };
}

MeshData loadMesh2(const std::string& filePath) {
    Assimp::Importer importer;
    // Ensure vertex colors are loaded - OBJ files may have vertex colors
    unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs;
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    std::cout << "[DEBUG loadMesh2] Assimp import flags: Triangulate | FlipUVs" << std::endl;

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error: Assimp - " << importer.GetErrorString() << std::endl;
        return {};
    }

    std::vector<PosColorVertex> vertices;
    std::vector<uint32_t> indices;

    // used for making origin of imported objects at 0 0 0 to center gizmo
    // works for both single mesh and multi mesh objects
    // Global bounding box for the entire model.
    float globalMinX = FLT_MAX, globalMinY = FLT_MAX, globalMinZ = FLT_MAX;
    float globalMaxX = -FLT_MAX, globalMaxY = -FLT_MAX, globalMaxZ = -FLT_MAX;

    // Iterate through all meshes in the scene
    std::cout << "[DEBUG loadMesh2] Loading mesh file: " << filePath << std::endl;
    std::cout << "[DEBUG loadMesh2] Total meshes in scene: " << scene->mNumMeshes << std::endl;
    
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        size_t baseIndex = vertices.size();
        std::unordered_map<std::string, uint16_t> uniqueVertices;

        std::cout << "[DEBUG loadMesh2] Processing mesh " << meshIndex 
                  << ": " << mesh->mNumVertices << " vertices, " 
                  << mesh->mNumFaces << " faces" << std::endl;
        std::cout << "[DEBUG loadMesh2] HasVertexColors(0): " << (mesh->HasVertexColors(0) ? "YES" : "NO") << std::endl;

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            PosColorVertex vertex;
            vertex.x = mesh->mVertices[i].x; //original: mesh->mVertices[i].x | flipped: -mesh->mVertices[i].x 
            vertex.y = mesh->mVertices[i].y;
            vertex.z = mesh->mVertices[i].z;

            // Update global bounding box.
            if (vertex.x < globalMinX) globalMinX = vertex.x;
            if (vertex.y < globalMinY) globalMinY = vertex.y;
            if (vertex.z < globalMinZ) globalMinZ = vertex.z;
            if (vertex.x > globalMaxX) globalMaxX = vertex.x;
            if (vertex.y > globalMaxY) globalMaxY = vertex.y;
            if (vertex.z > globalMaxZ) globalMaxZ = vertex.z;

            // Retrieve normals if available
            if (mesh->HasNormals()) {
                vertex.nx = mesh->mNormals[i].x;
                vertex.ny = mesh->mNormals[i].y;
                vertex.nz = mesh->mNormals[i].z;
            }
            else {
                vertex.nx = 0.0f;
                vertex.ny = 0.0f;
                vertex.nz = 0.0f; // Default to zero, will recompute later
            }

            // Retrieve texture coordinates (UVs) if available.
            if (mesh->HasTextureCoords(0)) {
                // Assimp stores texture coordinates in a 3D vector; we use only x and y.
                vertex.u = mesh->mTextureCoords[0][i].x;
                vertex.v = mesh->mTextureCoords[0][i].y;
            }
            else {
                vertex.u = 0.0f;
                vertex.v = 0.0f;
            }

            if (mesh->HasVertexColors(0)) {
                // Convert RGBA to ABGR format (A in bits 24-31, B in 16-23, G in 8-15, R in 0-7)
                float r = mesh->mColors[0][i].r;
                float g = mesh->mColors[0][i].g;
                float b = mesh->mColors[0][i].b;
                float a = mesh->mColors[0][i].a;
                vertex.abgr = ((uint8_t)(a * 255) << 24) |
                    ((uint8_t)(b * 255) << 16) |
                    ((uint8_t)(g * 255) << 8) |
                    (uint8_t)(r * 255);
                
                // Debug: Log first few vertices with colors
                if (i < 3) {
                    std::cout << "[DEBUG loadMesh2] Vertex " << i << " has color: RGBA(" 
                              << r << ", " << g << ", " << b << ", " << a 
                              << ") -> ABGR(0x" << std::hex << vertex.abgr << std::dec << ")" << std::endl;
                }
            }
            else {
                vertex.abgr = 0xffffffff; // Default color (white)
                if (i == 0) {
                    std::cout << "[DEBUG loadMesh2] Mesh has NO vertex colors, using default white (0xFFFFFFFF)" << std::endl;
                }
            }

            vertices.push_back(vertex);
        }


        // Reversed winding order
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            // Reverse the winding order
            for (int j = face.mNumIndices - 1; j >= 0; j--) {
                indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[j]));
            }
        }


        // Compute normals if the mesh does not have them
        if (!mesh->HasNormals()) {
            computeNormals(vertices, indices);
        }
    }

    // Compute the global center.
    float centerX = (globalMinX + globalMaxX) * 0.5f;
    float centerY = (globalMinY + globalMaxY) * 0.5f;
    float centerZ = (globalMinZ + globalMaxZ) * 0.5f;

    // Second pass: recenter all vertices by subtracting the global center.
    for (auto& v : vertices) {
        v.x -= centerX;
        v.y -= centerY;
        v.z -= centerZ;
    }

    return { vertices, indices };
}

void createMeshBuffers(const MeshData& meshData, bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh) {
    std::cout << "[DEBUG createMeshBuffers] Creating buffers for " << meshData.vertices.size() 
              << " vertices, " << meshData.indices.size() << " indices" << std::endl;
    
    // Debug: Check first few vertex colors
    if (meshData.vertices.size() > 0) {
        std::cout << "[DEBUG createMeshBuffers] First vertex color: 0x" << std::hex 
                  << meshData.vertices[0].abgr << std::dec << std::endl;
#ifdef _WIN32
        {
            char dbg[128];
            sprintf_s(dbg, "[AttributeMode] First vertex color CPU-side: 0x%08x\n",
                meshData.vertices[0].abgr);
            OutputDebugStringA(dbg);
        }
#endif
        if (meshData.vertices.size() > 1) {
            std::cout << "[DEBUG createMeshBuffers] Second vertex color: 0x" << std::hex 
                      << meshData.vertices[1].abgr << std::dec << std::endl;
        }
    }
    
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float) // NEW: UV coordinates
        .end();
    
    std::cout << "[DEBUG createMeshBuffers] Vertex layout includes Color0 attribute" << std::endl;

    vbh = bgfx::createVertexBuffer(
        bgfx::copy(meshData.vertices.data(), sizeof(PosColorVertex) * meshData.vertices.size()),
        layout
    );

    // Detect if we need 32-bit indices
    if (meshData.vertices.size() > std::numeric_limits<uint16_t>::max()) {
        std::cout << "Using 32-bit index buffer due to high poly count.\n";
        ibh = bgfx::createIndexBuffer(
            bgfx::copy(meshData.indices.data(), sizeof(uint32_t) * meshData.indices.size()),
            BGFX_BUFFER_INDEX32 // Enables 32-bit index buffer
        );
    }
    else {
        std::cout << "Using 16-bit index buffer for efficiency.\n";
        std::vector<uint16_t> indices16(meshData.indices.begin(), meshData.indices.end());
        ibh = bgfx::createIndexBuffer(
            bgfx::copy(indices16.data(), sizeof(uint16_t) * indices16.size())
        );
    }
}
static void glfw_keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_Z
        && (mods & GLFW_MOD_CONTROL)
        && action == GLFW_RELEASE) {
        gCmdManager.undo();
    }
    else if (key == GLFW_KEY_Y
        && (mods & GLFW_MOD_CONTROL)
        && action == GLFW_RELEASE) {
        gCmdManager.redo();
    }
    else if (key == GLFW_KEY_F1 && action == GLFW_RELEASE)
        s_showStats = !s_showStats;

    // Forward the event to ImGui
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

float getRandomFloat()
{
    static std::random_device rd;  // Seed for random number engine
    static std::mt19937 gen(rd()); // Mersenne Twister engine
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f); // Distribution range [0.0, 1.0]

    return dist(gen);
}

bgfx::ShaderHandle loadShader(const char* shaderPath)
{
    std::ifstream file(shaderPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to load shader: " << shaderPath << std::endl;
        return BGFX_INVALID_HANDLE;
    }

    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(static_cast<size_t>(fileSize));
    file.read(buffer.data(), fileSize);

    const bgfx::Memory* mem = bgfx::copy(buffer.data(), static_cast<uint32_t>(fileSize));
    //std::cout << "Shader loaded: " << shaderPath << std::endl;
    return bgfx::createShader(mem);
}

const bgfx::Memory* loadMem(const char* _filePath)
{
    std::ifstream file(_filePath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        fprintf(stderr, "Failed to load file: %s\n", _filePath);
        return nullptr;
    }

    // Get the file size.
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read file contents into a buffer.
    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size))
    {
        fprintf(stderr, "Failed to read file: %s\n", _filePath);
        return nullptr;
    }

    // Create a BGFX memory block from the buffer.
    return bgfx::copy(buffer.data(), static_cast<uint32_t>(size));
}

bgfx::TextureHandle loadTextureDDS(const char* _filePath)
{
    const bgfx::Memory* mem = loadMem(_filePath);
    if (mem == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }
    // Create texture from memory.
    return bgfx::createTexture(mem);
}

// The new function that checks extension and calls either your DDS loader or stb_image.
bgfx::TextureHandle loadTextureFile(const char* filePath)
{
    fs::path p(filePath);
    std::string ext = p.extension().string();
    // Convert extension to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // If DDS, use your existing function
    if (ext == ".dds")
    {
        return loadTextureDDS(filePath);
    }
    else
    {
        // Otherwise, use stb_image to load the file
        int width, height, channels;
        // Force RGBA (4 channels)
        unsigned char* data = stbi_load(filePath, &width, &height, &channels, 4);
        if (!data)
        {
            std::cerr << "Failed to load image via stb_image: " << filePath << std::endl;
            return BGFX_INVALID_HANDLE;
        }

        // Create a BGFX memory block from the raw pixels
        // We now have width * height * 4 bytes (RGBA).
        const bgfx::Memory* mem = bgfx::copy(data, width * height * 4);

        // Free stb_imageâ€™s data
        stbi_image_free(data);

        // Create a 2D texture from that memory
        // Note: If you want MIP maps, pass e.g. BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        //       then call bgfx::generateMips(...) or generate them offline.
        bgfx::TextureHandle texHandle = bgfx::createTexture2D(
            (uint16_t)width,
            (uint16_t)height,
            false,          // Has MIPs?
            1,              // Number of layers
            bgfx::TextureFormat::RGBA8, // We loaded RGBA
            0,              // Flags
            mem
        );

        if (!bgfx::isValid(texHandle))
        {
            std::cerr << "Failed to create BGFX texture from: " << filePath << std::endl;
        }
        else
        {
            std::cout << "Successfully loaded image (stb_image): " << filePath
                << "  (" << width << "x" << height << ")\n";
        }
        return texHandle;
    }
}

static int instanceCounter = 0;

static void spawnInstance(Camera camera, const std::string& instanceName, const std::string& instanceType, bgfx::VertexBufferHandle vbh, bgfx::IndexBufferHandle ibh, std::vector<Instance*>& instances)
{

    float spawnDistance = 5.0f;
    // Position for new instance, e.g., random or predefined position
    float x = camera.position.x + camera.front.x * spawnDistance;
    float y = camera.position.y + camera.front.y * spawnDistance;
    float z = camera.position.z + camera.front.z * spawnDistance;

    std::string fullName = instanceName + std::to_string(instanceCounter);

    // Create a new instance with the current vertex and index buffers
    Instance* inst = new Instance(instanceCounter++, fullName, instanceType, x, y, z, vbh, ibh);
    instances.push_back(inst);
    // Attach editable mesh data (if base template exists for this type).
    RegisterInstanceMeshFromType(inst);
    std::cout << "New instance created at (" << x << ", " << y << ", " << z << ")" << std::endl;
}

static void spawnInstanceAtCenter(const std::string& instanceName, const std::string& instanceType, bgfx::VertexBufferHandle vbh, bgfx::IndexBufferHandle ibh, std::vector<Instance*>& instances)
{
    std::string fullName = instanceName + std::to_string(instanceCounter);

    // Create a new instance with the current vertex and index buffers
    Instance* inst = new Instance(instanceCounter++, fullName, instanceType, 0.0f, 0.0f, 0.0f, vbh, ibh);
    instances.push_back(inst);
    // Attach editable mesh data (if base template exists for this type).
    RegisterInstanceMeshFromType(inst);
    std::cout << "New instance created at (" << 0 << ", " << 0 << ", " << 0 << ")" << std::endl;
}

static void spawnLight(const Camera& camera, bgfx::VertexBufferHandle vbh_sphere, bgfx::IndexBufferHandle ibh_sphere, bgfx::VertexBufferHandle vbh_cone, bgfx::IndexBufferHandle ibh_cone, std::vector<Instance*>& instances)
{
    float spawnDistance = 5.0f;
    float x = cameras[currentCameraIndex].position.x + cameras[currentCameraIndex].front.x * spawnDistance;
    float y = cameras[currentCameraIndex].position.y + cameras[currentCameraIndex].front.y * spawnDistance;
    float z = cameras[currentCameraIndex].position.z + cameras[currentCameraIndex].front.z * spawnDistance;
    std::string lightName = "light" + std::to_string(instanceCounter);
    Instance* lightInst = new Instance(instanceCounter++, lightName, "light", x, y, z, vbh_sphere, ibh_sphere);
    lightInst->isLight = true;
    // Set default light properties (point light)
    lightInst->lightProps.type = LightType::Point;
    lightInst->lightProps.intensity = 1.0f;
    lightInst->lightProps.range = 15.0f;
    lightInst->lightProps.coneAngle = 1.0f;
    lightInst->lightProps.color[0] = 1.0f; lightInst->lightProps.color[1] = 1.0f;
    lightInst->lightProps.color[2] = 1.0f; lightInst->lightProps.color[3] = 1.0f;

    // Update the debug geometry based on the light type.
    switch (lightInst->lightProps.type)
    {
    case LightType::Point:
        // Use sphere: already set
        break;
    case LightType::Spot:
        // Use cone mesh for a spot light.
        lightInst->vertexBuffer = vbh_cone;
        lightInst->indexBuffer = ibh_cone;
        break;
    case LightType::Directional:
        // use a cone for now
        lightInst->vertexBuffer = vbh_cone;
        lightInst->indexBuffer = ibh_cone;
        break;
    default:
        break;
    }

    // Make the visual representation small.
    lightInst->scale[0] = lightInst->scale[1] = lightInst->scale[2] = 0.2f;
    instances.push_back(lightInst);
    std::cout << "Spawned light " << lightName << " at (" << x << ", " << y << ", " << z << ")\n";
}

// Helper function to determine if a color is (approximately) white.
bool IsWhite(const float color[4], float epsilon = 0.001f)
{
    return std::fabs(color[0] - 1.0f) < epsilon &&
        std::fabs(color[1] - 1.0f) < epsilon &&
        std::fabs(color[2] - 1.0f) < epsilon &&
        std::fabs(color[3] - 1.0f) < epsilon;
}
// Recursive draw function for hierarchy.
void drawInstance(Instance* instance, bgfx::ProgramHandle defaultProgram, bgfx::ProgramHandle lightDebugProgram, bgfx::ProgramHandle textProgram, bgfx::ProgramHandle comicProgram, bgfx::UniformHandle u_comicColor, bgfx::UniformHandle u_noiseTex, bgfx::UniformHandle u_diffuseTex, bgfx::UniformHandle u_objectColor, bgfx::UniformHandle u_tint, bgfx::UniformHandle u_inkColor, bgfx::UniformHandle u_e, bgfx::UniformHandle u_params, bgfx::UniformHandle u_extraParams, bgfx::UniformHandle u_paramsLayer,
    bgfx::TextureHandle defaultWhiteTexture, bgfx::TextureHandle inheritedNoiseTex, bgfx::TextureHandle inheritedTexture, const float* parentColor = nullptr, const float* parentTransform = nullptr)
{
    float local[16];
    bx::mtxSRT(local,
        instance->scale[0], instance->scale[1], instance->scale[2],
        instance->rotation[0], instance->rotation[1], instance->rotation[2],
        instance->position[0], instance->position[1], instance->position[2]);

    float world[16];
    if (parentTransform)
        bx::mtxMul(world, local, parentTransform);
    else
        std::memcpy(world, local, sizeof(world));

    instance->worldPosition[0] = world[12];
    instance->worldPosition[1] = world[13];
    instance->worldPosition[2] = world[14];
    // Compute comic object color.
    float comicColor[4];

    // Compute effective object color.
    float effectiveColor[4];
    if (parentColor && !IsWhite(parentColor))
    {
        // If parent's color is not white, inherit it.
        std::memcpy(effectiveColor, parentColor, sizeof(effectiveColor));

        // Compute comic object color.
        std::memcpy(comicColor, parentColor, sizeof(comicColor));
    }
    else
    {
        // Otherwise, use this instance's objectColor.
        std::memcpy(effectiveColor, instance->objectColor, sizeof(effectiveColor));

        // Compute comic object color.
        std::memcpy(comicColor, instance->objectColor, sizeof(comicColor));
    }

    // Set uniforms only if we're actually going to draw something
    const bgfx::VertexBufferHandle invalidVbh = BGFX_INVALID_HANDLE;
    const bgfx::IndexBufferHandle invalidIbh = BGFX_INVALID_HANDLE;
    // Draw geometry if valid.
    if (instance->vertexBuffer.idx != invalidVbh.idx &&
        instance->indexBuffer.idx != invalidIbh.idx)
    {
        // Set the object override color uniform (only set right before we submit).
        bgfx::setUniform(u_objectColor, effectiveColor);
        bgfx::setUniform(u_albedoFactor, instance->material.albedo);
        const float tintBasic[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
        const float tintHighlighted[4] = { 0.3f, 0.3f, 2.0f, 0.1f };
        if (selectedInstance == instance && highlightVisible) {
            bgfx::setUniform(u_tint, tintHighlighted);
        }
        else {
            bgfx::setUniform(u_tint, tintBasic);
        }
        if (!useGlobalCrosshatchSettings) {
            bgfx::setUniform(u_inkColor, instance->inkColor);
            // Set epsilon uniform:
            float epsilonUniform[4] = { instance->epsilonValue, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(u_e, epsilonUniform);

            // Prepare an array of 4 floats.
            // Set u_params uniform:
            float paramsUniform[4] = { 0.0f, instance->strokeMultiplier, instance->lineAngle1, instance->lineAngle2 };
            bgfx::setUniform(u_params, paramsUniform);

            // Prepare an array of 4 floats.
            float extraParamsUniform[4] = { instance->patternScale, instance->lineThickness, instance->transparencyValue, float(instance->crosshatchMode) };
            // Set the uniform for extra parameters.
            bgfx::setUniform(u_extraParams, extraParamsUniform);


            // Prepare an array of 4 floats.
            float paramsLayerUniform[4] = { instance->layerPatternScale, instance->layerStrokeMult, instance->layerAngle, instance->layerLineThickness };
            // Set the uniform for extra parameters.
            bgfx::setUniform(u_paramsLayer, paramsLayerUniform);
        }

        // Attribute (unlit vertex color) mode: bypass all lighting and materials.
        if (useAttributeMode &&
            instance->type != "light" &&
            instance->type != "text" &&
            instance->type != "comicborder" &&
            instance->type != "comicbubble")
        {
#ifdef _WIN32
            char dbg[256];
            sprintf_s(dbg,
                "[AttributeMode] Drawing instance '%s' (id=%d, type=%s) with unlit vertex colors. Program valid: %s\n",
                instance->name.c_str(),
                instance->id,
                instance->type.c_str(),
                bgfx::isValid(unlitColorProgram) ? "YES" : "NO");
            OutputDebugStringA(dbg);
#endif
            bgfx::setTransform(world);
            bgfx::setVertexBuffer(0, instance->vertexBuffer);
            bgfx::setIndexBuffer(instance->indexBuffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS);

            if (bgfx::isValid(unlitColorProgram))
            {
                bgfx::submit(1, unlitColorProgram);
            }
            else
            {
#ifdef _WIN32
                OutputDebugStringA("[AttributeMode] unlitColorProgram is invalid, falling back to defaultProgram.\n");
#endif
                bgfx::setState(BGFX_STATE_DEFAULT);
                bgfx::submit(1, defaultProgram);
            }
        }
        else
        {
            bgfx::setTransform(world);
            bgfx::setVertexBuffer(0, instance->vertexBuffer);
            bgfx::setIndexBuffer(instance->indexBuffer);

            // Decide which texture to use:
            // If the inherited texture (from the parent) is valid, then use it regardless of what the instance may have set.
            // Otherwise, use the instance's own texture (if any), or fall back to the default.
            bgfx::TextureHandle textureToUse = defaultWhiteTexture;
            if (inheritedTexture.idx != bgfx::kInvalidHandle)
            {
                textureToUse = inheritedTexture;
            }
            else if (instance->diffuseTexture.idx != bgfx::kInvalidHandle)
            {
                textureToUse = instance->diffuseTexture;
            }
            else
            {
                textureToUse = defaultWhiteTexture;
            }
            bgfx::setTexture(1, u_diffuseTex, textureToUse);

            // Decide which noise tex to use
            bgfx::TextureHandle noiseTextureToUse;
            if (useGlobalCrosshatchSettings) {
                noiseTextureToUse = noiseTexture;
            }
            else if (inheritedNoiseTex.idx != bgfx::kInvalidHandle) {
                noiseTextureToUse = inheritedNoiseTex;
            }
            else if (instance->noiseTexture.idx != bgfx::kInvalidHandle)
            {
                noiseTextureToUse = instance->noiseTexture;
            }
            else
            {
                noiseTextureToUse = availableNoiseTextures[0].handle;
            }
            bgfx::setTexture(0, u_noiseTex, noiseTextureToUse);

            // If the instance is a light and its debug visual is turned off,
            // skip drawing the sphere representation.
            if (instance->type == "light" && !instance->showDebugVisual)
            {
                // Do nothing (or optionally draw a minimal indicator)
            }
            else
            {
                // Choose appropriate shader based on instance type
                if (instance->type == "text")
                {
                    // Set the comic color uniform (see below for how it's updated via ImGui).
                    bgfx::setUniform(u_comicColor, comicColor);

                    // Enable alpha blending for text rendering
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                    bgfx::submit(1, textProgram);
                }
                else if (instance->type == "comicborder" || instance->type == "comicbubble") {
                    bgfx::setState(BGFX_STATE_DEFAULT);

                    // Set the comic color uniform (see below for how it's updated via ImGui).
                    bgfx::setUniform(u_comicColor, comicColor);
                    bgfx::submit(1, comicProgram);
                }
                else
                {
                    // Use default or debug shader
                    bgfx::setState(BGFX_STATE_DEFAULT);

                    bgfx::submit(1, (instance->type == "light") ? lightDebugProgram : defaultProgram);
                }
            }
        }

        // When this instance is selected, draw its vertices as green dots and edges as green lines.
        if (selectedInstance == instance && highlightVisible)
        {
            DrawSelectedMeshOverlay(instance, world, 1);
        }
    }
    // Determine what color to pass to children.
    // If the effective color is white, then children should use their own objectColor.
    const float* childParentColor = (!IsWhite(effectiveColor)) ? effectiveColor : nullptr;
    // For children, propagate the override:
    // If the inherited texture is already valid, continue propagating that.
    // Otherwise, use the current instanceâ€™s texture as the inherited texture.
    bgfx::TextureHandle newInheritedTexture = inheritedTexture;
    if (inheritedTexture.idx == bgfx::kInvalidHandle)
    {
        newInheritedTexture = instance->diffuseTexture;
    }

    // For children
    bgfx::TextureHandle newInheritedNoiseTex = inheritedNoiseTex;
    if (inheritedTexture.idx == bgfx::kInvalidHandle)
    {
        newInheritedNoiseTex = instance->noiseTexture;
    }

    // Recursively draw children.
    for (Instance* child : instance->children)
    {
        drawInstance(child, defaultProgram, lightDebugProgram, textProgram, comicProgram, u_comicColor, u_noiseTex, u_diffuseTex, u_objectColor, u_tint, u_inkColor, u_e, u_params, u_extraParams, u_paramsLayer, defaultWhiteTexture, inheritedNoiseTex, newInheritedTexture, childParentColor, world);
    }
}
Instance* g_PendingDelete = nullptr;
// Recursive deletion for hierarchy.
void deleteInstance(Instance* instance)
{
    for (Instance* child : instance->children)
    {
        deleteInstance(child);
    }
    // Remove any cached editable mesh data associated with this instance.
    auto it = g_InstanceMeshData.find(instance->id);
    if (it != g_InstanceMeshData.end())
    {
        g_InstanceMeshData.erase(it);
    }
    delete instance;
}

void ShowInstanceTree(
    Instance* instance,
    Instance*& selectedInstance,
    std::vector<Instance*>& instances
)
{
    // ---------- Shared UI state ----------
    static Instance* renamingInstance = nullptr;
    static char renameBuffer[256] = {};

    // g_PendingDelete must be declared at file scope
     Instance* deletingInstance = nullptr;

    // ---------- Tree flags ----------
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanFullWidth;

    if (instance->children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (selectedInstance == instance)
        flags |= ImGuiTreeNodeFlags_Selected;

    // ---------- Tree node ----------
    bool nodeOpen = ImGui::TreeNodeEx(
        (void*)instance,
        flags,
        "%s",
        instance->name.c_str()
    );

    // ---------- Selection ----------
    if (ImGui::IsItemClicked())
        selectedInstance = instance;

    // ---------- Double-click â†’ focus camera ----------
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
    {
        selectedInstance = instance;

        Camera& cam = cameras[currentCameraIndex];
        const float distance = 5.0f;

        cam.position.x = instance->worldPosition[0] - cam.front.x * distance;
        cam.position.y = instance->worldPosition[1] - cam.front.y * distance;
        cam.position.z = instance->worldPosition[2] - cam.front.z * distance;

        float dx = instance->worldPosition[0] - cam.position.x;
        float dy = instance->worldPosition[1] - cam.position.y;
        float dz = instance->worldPosition[2] - cam.position.z;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (len > 0.0001f)
        {
            cam.front.x = dx / len;
            cam.front.y = dy / len;
            cam.front.z = dz / len;
        }

        cam.yaw = std::atan2(cam.front.z, cam.front.x) * 180.0f / 3.14159265f;
        cam.pitch = std::asin(cam.front.y) * 180.0f / 3.14159265f;
    }

    // ---------- Context menu ----------
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename"))
        {
            renamingInstance = instance;
            strncpy(renameBuffer, instance->name.c_str(), sizeof(renameBuffer));
            renameBuffer[sizeof(renameBuffer) - 1] = '\0';
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete"))
        {
            deletingInstance = instance;
            
            // If the selected instance has a parent, remove it from the parent's children list.
            if (deletingInstance->parent)
            {
                Instance* parent = deletingInstance->parent;
                auto it = std::find(parent->children.begin(), parent->children.end(), deletingInstance);
                if (it != parent->children.end())
                {
                    parent->children.erase(it);
                }
            }
            else
            {
                // Otherwise, it's top-level. Remove it from the global instances vector.
                auto it = std::find(instances.begin(), instances.end(), deletingInstance);
                if (it != instances.end())
                {
                    instances.erase(it);
                }
            }

            // Delete the instance (which will recursively delete its children)
            deleteInstance(deletingInstance);
            selectedInstance = nullptr;
        }

        ImGui::EndPopup();
    }

    // ---------- Inline rename ----------
    if (renamingInstance == instance)
    {
        ImGui::SameLine();

        ImGui::PushID(instance); // ðŸ”´ IMPORTANT: avoid ID collision
        ImGui::SetNextItemWidth(160);
        ImGui::SetKeyboardFocusHere();

        if (ImGui::InputText(
            "##rename",
            renameBuffer,
            sizeof(renameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll
        ))
        {
            instance->name = renameBuffer;
            renamingInstance = nullptr;
        }

        // Click outside â†’ cancel
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            renamingInstance = nullptr;

        ImGui::PopID();
    }

    // ---------- Drag source ----------
    if (ImGui::BeginDragDropSource())
    {
        Instance* payload = instance;
        ImGui::SetDragDropPayload("DND_INSTANCE", &payload, sizeof(Instance*));
        ImGui::Text("%s", instance->name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---------- Drag target ----------
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_INSTANCE"))
        {
            Instance* dropped = *(Instance**)payload->Data;
            if (dropped && dropped != instance)
            {
                if (dropped->parent)
                {
                    auto& siblings = dropped->parent->children;
                    siblings.erase(
                        std::remove(siblings.begin(), siblings.end(), dropped),
                        siblings.end()
                    );
                }
                else
                {
                    instances.erase(
                        std::remove(instances.begin(), instances.end(), dropped),
                        instances.end()
                    );
                }

                instance->addChild(dropped);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ---------- Children ----------
    if (nodeOpen && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen))
    {
        for (Instance* child : instance->children)
            ShowInstanceTree(child, selectedInstance, instances);

        ImGui::TreePop();
    }
}

// Helper function to quote strings for output
std::string quote_if_needed(const std::string& s) {
    // Simple check for spaces, or always quote strings
    // For more robust quoting, you might check for other special characters too
    // or just always quote.
    if (s.find(' ') != std::string::npos || s.find('\t') != std::string::npos || s.find('"') != std::string::npos) {
        std::string quoted_s = "\"";
        for (char c : s) {
            if (c == '"') {
                quoted_s += "\\\""; // Escape existing quotes
            }
            else if (c == '\\') {
                quoted_s += "\\\\"; // Escape backslashes
            }
            else {
                quoted_s += c;
            }
        }
        quoted_s += "\"";
        return quoted_s;
    }
    return s;
}

// Helper function to read a potentially quoted string from an istringstream
std::string read_quoted_string(std::istringstream& iss) {
    std::string token;
    iss >> token; // Read the first part

    if (!token.empty() && token.front() == '"') {
        std::string result = token.substr(1); // Remove leading quote
        bool in_escape = false;
        // If the token didn't end with a quote (and it wasn't an escaped quote)
        while (result.empty() || result.back() != '"' || (result.length() > 1 && result[result.length() - 2] == '\\' && !in_escape)) {
            if (iss.eof()) { // Reached end of stream unexpectedly
                // Handle error: unclosed quote
                throw std::runtime_error("Unclosed quote in input stream");
            }
            std::string next_part;
            iss >> next_part;
            if (!result.empty()) result += " "; // Add back the space delimiter
            result += next_part;
        }

        // Remove trailing quote if it's not escaped
        if (!result.empty() && result.back() == '"') {
            // Check if it's an escaped quote (e.g., "abc\\\"")
            int trailing_backslashes = 0;
            for (auto it = result.rbegin() + 1; it != result.rend() && *it == '\\'; ++it) {
                trailing_backslashes++;
            }
            if (trailing_backslashes % 2 == 0) { // Not an escaped quote
                result.pop_back();
            }
        }


        // Unescape characters
        std::string unescaped_result;
        unescaped_result.reserve(result.length());
        for (size_t i = 0; i < result.length(); ++i) {
            if (result[i] == '\\' && i + 1 < result.length()) {
                if (result[i + 1] == '"' || result[i + 1] == '\\') {
                    unescaped_result += result[i + 1];
                    i++; // Skip the escaped character
                }
                else {
                    unescaped_result += result[i]; // Not a recognized escape sequence, keep the backslash
                }
            }
            else {
                unescaped_result += result[i];
            }
        }
        return unescaped_result;

    }
    else {
        // Not a quoted string, or an empty string if that's how you write it
        return token;
    }
}

void saveInstance(std::ofstream& file, const Instance* instance,
    const std::vector<TextureOption>& availableTextures, int parentID = -1)
{
    std::string textureName = "none";
    for (const auto& tex : availableTextures)
    {
        if (instance->diffuseTexture.idx == tex.handle.idx)
        {
            textureName = tex.name;
            break;
        }
    }

    std::string noiseTextureName = "none";
    for (const auto& tex : availableNoiseTextures)
    {
        if (instance->noiseTexture.idx == tex.handle.idx)
        {
            noiseTextureName = tex.name;
            break;
        }
    }

    file << instance->id << " " << quote_if_needed(instance->type) << " " << quote_if_needed(instance->name) << " " << instance->meshNumber << " "
        << instance->position[0] << " " << instance->position[1] << " " << instance->position[2] << " "
        << instance->rotation[0] << " " << instance->rotation[1] << " " << instance->rotation[2] << " "
        << instance->scale[0] << " " << instance->scale[1] << " " << instance->scale[2] << " "
        << instance->objectColor[0] << " " << instance->objectColor[1] << " " << instance->objectColor[2] << " " << instance->objectColor[3] << " "
        << quote_if_needed(textureName) << " " << quote_if_needed(noiseTextureName) << " " << parentID << " " << static_cast<int>(instance->lightProps.type) << " " << instance->lightProps.direction[0] << " "
        << instance->lightProps.direction[1] << " " << instance->lightProps.direction[2] << " " << instance->lightProps.intensity << " "
        << instance->lightProps.range << " " << instance->lightProps.coneAngle << " " << instance->lightProps.color[0] << " "
        << instance->lightProps.color[1] << " " << instance->lightProps.color[2] << " " << instance->lightProps.color[3] << " "
        << instance->inkColor[0] << " " << instance->inkColor[1] << " " << instance->inkColor[2] << " " << instance->inkColor[3] << " "
        << instance->epsilonValue << " " << instance->strokeMultiplier << " " << instance->lineAngle1 << " " << instance->lineAngle2 << " "
        << instance->patternScale << " " << instance->lineThickness << " " << instance->transparencyValue << " " << instance->crosshatchMode << " "
        << instance->layerPatternScale << " " << instance->layerStrokeMult << " " << instance->layerAngle << " " << instance->layerLineThickness << " "
        << instance->centerX << " " << instance->centerZ << " " << instance->radius << " " << instance->rotationSpeed << " " << instance->instanceAngle << " "
        << instance->basePosition[0] << " " << instance->basePosition[1] << " " << instance->basePosition[2] << " "
        << instance->lightAnim.amplitude[0] << " " << instance->lightAnim.amplitude[1] << " " << instance->lightAnim.amplitude[2] << " "
        << instance->lightAnim.frequency[0] << " " << instance->lightAnim.frequency[1] << " " << instance->lightAnim.frequency[2] << " "
        << instance->lightAnim.phase[0] << " " << instance->lightAnim.phase[1] << " " << instance->lightAnim.phase[2] << " "
        << static_cast<int>(instance->lightAnim.enabled) << " " << quote_if_needed(instance->textContent) << "\n"; // Save `type` and parentID

    // Recursively save children
    for (const Instance* child : instance->children)
    {
        saveInstance(file, child, availableTextures, instance->id);
    }
}
void SaveImportedObjMap(const std::unordered_map<std::string, std::string>& map, const std::string& filePath)
{
    std::ofstream ofs(filePath);
    if (!ofs)
    {
        std::cerr << "Failed to open file for writing: " << filePath << std::endl;
        return;
    }
    // Write each mapping as: key [whitespace] value
    for (const auto& entry : map)
    {
        ofs << quote_if_needed(entry.first) << " " << quote_if_needed(entry.second) << "\n";
    }
    ofs.close();
}
std::unordered_map<std::string, std::string> LoadImportedObjMap(const std::string& filePath)
{
    std::unordered_map<std::string, std::string> map;
    std::ifstream ifs(filePath);
    if (!ifs)
    {
        std::cerr << "Failed to open file for reading: " << filePath << std::endl;
        return map;
    }
    std::string line;
    while (std::getline(ifs, line))
    {
        std::istringstream iss(line);
        std::string key, path;
        key = read_quoted_string(iss);
        path = read_quoted_string(iss);
        if (key.empty() || path.empty())
        {
            std::cerr << "Invalid line in file: " << line << std::endl;
            continue; // Skip invalid lines
        }
        map[key] = path;
    }
    ifs.close();
    return map;
}

void saveSceneToFile(std::vector<Instance*>& instances, const std::vector<TextureOption>& availableTextures, const std::unordered_map<std::string, std::string>& importedObjMap)
{
    std::string saveFilePath = openFileDialog(true); // Open save dialog
    if (saveFilePath.empty()) return; // Exit if no file was chosen

    std::ofstream file(saveFilePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to save scene!" << std::endl;
        return;
    }

    for (const Instance* instance : instances)
    {
        // Ensure top-level instances are saved first (with no parent)
        saveInstance(file, instance, availableTextures, -1);
    }

    file.close();
    std::cout << "Scene saved to " << saveFilePath << std::endl;

    std::filesystem::path scenePath = saveFilePath;
    std::filesystem::path sceneDirectory = scenePath.parent_path();
    std::filesystem::path importedObjMapPath = sceneDirectory / (scenePath.stem().string() + "_imp_obj_map.txt");

    SaveImportedObjMap(importedObjMap, importedObjMapPath.string());
    std::cout << "Imported obj paths saved to " << importedObjMapPath << std::endl;
    //std::string importedObjMapPath = fs::path(saveFilePath).stem().string() + "_imp_obj_map.txt";
    //SaveImportedObjMap(importedObjMap, importedObjMapPath);
    //std::cout << "Imported obj paths saved to " << importedObjMapPath << std::endl;
}

std::string ConvertBackslashesToForward(const std::string& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

// ========================
// UPDATED IMPORTER CODE WITH TEXTURE LOADING
// ========================

// Structure to hold mesh data, its transform, and its diffuse texture.
struct ImportedMesh {
    MeshData meshData;
    aiMatrix4x4 transform; // Global transform (accumulated from the scene hierarchy)
    bgfx::TextureHandle diffuseTexture; // Diffuse texture for this mesh, if available.
    float diffuseColor[4]; // To store Kd from MTL
    bool hasDiffuseColor;  // Flag to indicate if diffuseColor was loaded

    ImportedMesh() : diffuseTexture(BGFX_INVALID_HANDLE), hasDiffuseColor(false) {
        // Initialize diffuseColor to white (or any default)
        diffuseColor[0] = 1.0f; diffuseColor[1] = 1.0f; diffuseColor[2] = 1.0f; diffuseColor[3] = 1.0f;
    }
};

// Recursive function to traverse the scene graph.
// The additional 'baseDir' parameter lets us resolve relative texture paths.
void processNode(const aiScene* scene, aiNode* node, const aiMatrix4x4& parentTransform,
    const std::string& baseDir, std::vector<ImportedMesh>& importedMeshes)
{
    aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;

    // Process each mesh referenced by this node.
    std::cout << "[DEBUG processNode] Processing node with " << node->mNumMeshes << " meshes" << std::endl;
    
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        unsigned int meshIndex = node->mMeshes[i];
        aiMesh* mesh = scene->mMeshes[meshIndex];
        MeshData meshData;
        size_t baseIndex = meshData.vertices.size();
        
        std::cout << "[DEBUG processNode] Mesh " << meshIndex 
                  << ": " << mesh->mNumVertices << " vertices, " 
                  << mesh->mNumFaces << " faces" << std::endl;
        std::cout << "[DEBUG processNode] HasVertexColors(0): " << (mesh->HasVertexColors(0) ? "YES" : "NO") << std::endl;

        // Process vertices.
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            PosColorVertex vertex;
            vertex.x = -mesh->mVertices[j].x; //original: mesh->mVertices[j].x | flipped: -mesh->mVertices[j].x 
            vertex.y = mesh->mVertices[j].y;
            vertex.z = mesh->mVertices[j].z;
            if (mesh->HasNormals()) {
                vertex.nx = -mesh->mNormals[j].x;
                vertex.ny = mesh->mNormals[j].y;
                vertex.nz = mesh->mNormals[j].z;
            }
            else {
                vertex.nx = vertex.ny = vertex.nz = 0.0f;
            }
            if (mesh->HasTextureCoords(0)) {
                vertex.u = mesh->mTextureCoords[0][j].x;
                vertex.v = mesh->mTextureCoords[0][j].y;
            }
            else {
                vertex.u = vertex.v = 0.0f;
            }
            if (mesh->HasVertexColors(0)) {
                // Convert RGBA to ABGR format (A in bits 24-31, B in 16-23, G in 8-15, R in 0-7)
                float r = mesh->mColors[0][j].r;
                float g = mesh->mColors[0][j].g;
                float b = mesh->mColors[0][j].b;
                float a = mesh->mColors[0][j].a;
                vertex.abgr = ((uint8_t)(a * 255) << 24) |
                    ((uint8_t)(b * 255) << 16) |
                    ((uint8_t)(g * 255) << 8) |
                    (uint8_t)(r * 255);
                
                // Debug: Log first few vertices with colors
                if (j < 3) {
                    std::cout << "[DEBUG processNode] Vertex " << j << " has color: RGBA(" 
                              << r << ", " << g << ", " << b << ", " << a 
                              << ") -> ABGR(0x" << std::hex << vertex.abgr << std::dec << ")" << std::endl;
                }
            }
            else {
                vertex.abgr = 0xffffffff; // Default color (white)
                if (j == 0) {
                    std::cout << "[DEBUG processNode] Mesh has NO vertex colors, using default white (0xFFFFFFFF)" << std::endl;
                }
            }
            meshData.vertices.push_back(vertex);
        }

        // Process indices (reversed winding order).
        /*
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (int k = face.mNumIndices - 1; k >= 0; k--) {
                meshData.indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[k]));
            }
        }
        */
        // Process indices in natural order (preserving winding order)
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (int k = 0; k < face.mNumIndices; k++) {
                meshData.indices.push_back(static_cast<uint32_t>(baseIndex + face.mIndices[k]));
            }
        }

        // Recompute normals if missing.
        if (!mesh->HasNormals()) {
            computeNormals(meshData.vertices, meshData.indices);
        }

        // Create an ImportedMesh to store this meshâ€™s data.
        ImportedMesh impMesh;
        impMesh.meshData = meshData;
        impMesh.transform = globalTransform;
        impMesh.diffuseTexture = BGFX_INVALID_HANDLE;
        impMesh.hasDiffuseColor = false;          // Initialize

        // --- New: Retrieve diffuse texture from the material ---
        // Attempt to load a texture from the material.
        if (scene->HasMaterials()) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texPath;
            // For glTF, check for the base color texture.
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
                std::string textureFile = texPath.C_Str();
                fs::path fullTexPath = fs::path(baseDir) / textureFile;
                std::string normalizedTexPath = ConvertBackslashesToForward(fullTexPath.string());
                std::cout << "[DEBUG] Found baseColor texture: " << normalizedTexPath << std::endl;
                bgfx::TextureHandle texHandle = loadTextureFile(normalizedTexPath.c_str());
                if (bgfx::isValid(texHandle)) {
                    std::cout << "[DEBUG] Successfully loaded texture: " << normalizedTexPath << std::endl;
                    impMesh.diffuseTexture = texHandle;
                }
                else {
                    std::cout << "[DEBUG] FAILED to load texture: " << normalizedTexPath << std::endl;
                }
            }
            // Fallback: if no baseColor texture, try diffuse.
            else if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string textureFile = texPath.C_Str();
                fs::path fullTexPath = fs::path(baseDir) / textureFile;
                std::string normalizedTexPath = ConvertBackslashesToForward(fullTexPath.string());
                std::cout << "[DEBUG] Found diffuse texture (fallback): " << normalizedTexPath << std::endl;
                bgfx::TextureHandle texHandle = loadTextureFile(normalizedTexPath.c_str());
                if (bgfx::isValid(texHandle)) {
                    std::cout << "[DEBUG] Successfully loaded texture: " << normalizedTexPath << std::endl;
                    impMesh.diffuseTexture = texHandle;
                }
                else {
                    std::cout << "[DEBUG] FAILED to load texture: " << normalizedTexPath << std::endl;
                }
            }
            // If no diffuse texture was loaded, try to get the diffuse color (Kd from MTL)
            if (!bgfx::isValid(impMesh.diffuseTexture)) {
                aiColor4D diffuse; // Use aiColor4D to potentially get alpha.
                if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
                    impMesh.diffuseColor[0] = diffuse.r;
                    impMesh.diffuseColor[1] = diffuse.g;
                    impMesh.diffuseColor[2] = diffuse.b;

                    // Try to get opacity (d or Tr from MTL, Assimp provides it via AI_MATKEY_OPACITY)
                    float opacity = 1.0f;
                    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
                        impMesh.diffuseColor[3] = opacity;
                    }
                    else {
                        impMesh.diffuseColor[3] = diffuse.a; // Fallback to alpha from aiColor4D, often 1.0 for Kd
                    }
                    impMesh.hasDiffuseColor = true;
                    std::cout << "[DEBUG] Found diffuse color for material index " << mesh->mMaterialIndex
                        << ": (" << diffuse.r << ", " << diffuse.g << ", " << diffuse.b << ", " << impMesh.diffuseColor[3] << ")" << std::endl;
                }
                else {
                    std::cout << "[DEBUG] No diffuse texture or diffuse color found for material index "
                        << mesh->mMaterialIndex << std::endl;
                }
            }
        }
        else {
            std::cout << "[DEBUG] No materials in scene or mesh material index is invalid." << std::endl;
        }

        importedMeshes.push_back(impMesh);
    }

    // Recursively process child nodes.
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(scene, node->mChildren[i], globalTransform, baseDir, importedMeshes);
    }
}

// Load the file and extract all meshes, their transforms, and diffuse textures.
// The baseDir is computed from the model file path.
std::vector<ImportedMesh> loadImportedMeshes(const std::string& filePath) {
    std::cout << "[DEBUG loadImportedMeshes] Loading file: " << filePath << std::endl;
    
    Assimp::Importer importer;
    // Ensure vertex colors are loaded - OBJ files may have vertex colors
    unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices;
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    std::cout << "[DEBUG loadImportedMeshes] Assimp import flags: Triangulate | FlipUVs | PreTransformVertices" << std::endl;
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error: Assimp - " << importer.GetErrorString() << std::endl;
        return {};
    }
    
    std::cout << "[DEBUG loadImportedMeshes] Scene loaded successfully. Total meshes: " << scene->mNumMeshes << std::endl;

    std::vector<ImportedMesh> importedMeshes;
    aiMatrix4x4 identity; // Identity matrix
    // Determine the base directory from the model file path.
    fs::path modelPath(filePath);
    std::string baseDir = modelPath.parent_path().string();
    processNode(scene, scene->mRootNode, identity, baseDir, importedMeshes);

    // ---- Perâ€“Mesh Recentering (as before) ----
    for (auto& impMesh : importedMeshes) {
        aiVector3D localMin(FLT_MAX, FLT_MAX, FLT_MAX);
        aiVector3D localMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const auto& vertex : impMesh.meshData.vertices) {
            aiVector3D pos(vertex.x, vertex.y, vertex.z);
            localMin.x = std::min(localMin.x, pos.x);
            localMin.y = std::min(localMin.y, pos.y);
            localMin.z = std::min(localMin.z, pos.z);
            localMax.x = std::max(localMax.x, pos.x);
            localMax.y = std::max(localMax.y, pos.y);
            localMax.z = std::max(localMax.z, pos.z);
        }
        aiVector3D meshCenter = (localMin + localMax) * 0.5f;
        // Shift vertices so the mesh geometry is centered at (0,0,0)
        for (auto& vertex : impMesh.meshData.vertices) {
            vertex.x -= meshCenter.x;
            vertex.y -= meshCenter.y;
            vertex.z -= meshCenter.z;
        }
        // Update the mesh's transform to account for the shift.
        aiMatrix4x4 translationMat;
        aiMatrix4x4::Translation(meshCenter, translationMat);
        impMesh.transform = translationMat * impMesh.transform;
    }

    return importedMeshes;
}

// Find the highest instance ID in the hierarchy
void findMaxInstanceId(const Instance* instance, int& maxId) {
    // Check this instance's ID
    maxId = std::max(maxId, instance->id);

    // Recursively check all children
    for (const Instance* child : instance->children) {
        findMaxInstanceId(child, maxId);
    }
}

// for comic bubble text
void updateTextTexture(Instance* textInst) {
    bgfx::TextureHandle newTex = createTextTexture(textInst->textContent);
    if (bgfx::isValid(newTex)) {
        // If an old texture exists, destroy it.
        if (bgfx::isValid(textInst->diffuseTexture)) {
            bgfx::destroy(textInst->diffuseTexture);
        }
        textInst->diffuseTexture = newTex;
    }
}

std::unordered_map<std::string, std::string> loadSceneFromFile(std::vector<Instance*>& instances,
    const std::vector<TextureOption>& availableTextures,
    const std::unordered_map<std::string, std::pair<bgfx::VertexBufferHandle, bgfx::IndexBufferHandle>>& bufferMap)
{
    selectedInstance = nullptr;
    std::string loadFilePath = openFileDialog(false);
    std::string importedObjMapPath = fs::path(loadFilePath).parent_path().string() + "\\" + (fs::path(loadFilePath).stem().string() + "_imp_obj_map.txt");
    std::unordered_map<std::string, std::string> importedObjMap = LoadImportedObjMap(importedObjMapPath);
    if (loadFilePath.empty()) return importedObjMap;

    std::ifstream file(loadFilePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to load scene!" << std::endl;
        return importedObjMap;
    }

    // Clear existing instances
    for (Instance* inst : instances)
    {
        deleteInstance(inst);
    }
    instances.clear();

    std::unordered_map<int, Instance*> instanceMap; // Stores instances by their IDs
    std::vector<std::pair<int, int>> parentAssignments; // Stores parent-child assignments

    std::vector<ImportedMesh> importedMeshes;
    std::string importedMeshesName = "";

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        bgfx::TextureHandle diffuseTexture = BGFX_INVALID_HANDLE;
        int id, meshNo, parentID, lightType, crosshatchMode, animationEnabled;
        std::string type, name, textureName, noiseTextureName, textContent;
        float pos[3], rot[3], scale[3], color[4], lightDirection[3], intensity, range, coneAngle, lightColor[4], inkColor[4], epsilonValue, strokeMultiplier, lineAngle1, lineAngle2, patternScale, lineThickness, transparencyValue, layerPatternScale, layerStrokeMult, layerAngle, layerLineThickness, centerX, centerZ, radius, rotationSpeed, instanceAngle, basePosition[3], amplitude[3], frequency[3], phase[3];

        iss >> id;
        type = read_quoted_string(iss);
        name = read_quoted_string(iss);
        iss >> meshNo
            >> pos[0] >> pos[1] >> pos[2]
            >> rot[0] >> rot[1] >> rot[2]
            >> scale[0] >> scale[1] >> scale[2]
            >> color[0] >> color[1] >> color[2] >> color[3];
        textureName = read_quoted_string(iss);
        noiseTextureName = read_quoted_string(iss);
        iss >> parentID >> lightType
            >> lightDirection[0] >> lightDirection[1] >> lightDirection[2]
            >> intensity >> range >> coneAngle
            >> lightColor[0] >> lightColor[1] >> lightColor[2] >> lightColor[3]
            >> inkColor[0] >> inkColor[1] >> inkColor[2] >> inkColor[3]
            >> epsilonValue >> strokeMultiplier >> lineAngle1 >> lineAngle2
            >> patternScale >> lineThickness >> transparencyValue >> crosshatchMode
            >> layerPatternScale >> layerStrokeMult >> layerAngle >> layerLineThickness
            >> centerX >> centerZ >> radius >> rotationSpeed >> instanceAngle
            >> basePosition[0] >> basePosition[1] >> basePosition[2]
            >> amplitude[0] >> amplitude[1] >> amplitude[2]
            >> frequency[0] >> frequency[1] >> frequency[2]
            >> phase[0] >> phase[1] >> phase[2]
            >> animationEnabled;
        textContent = read_quoted_string(iss);

        // Fetch correct buffers using `type`
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;

        auto it = bufferMap.find(type);
        if (it != bufferMap.end())
        {
            vbh = it->second.first;
            ibh = it->second.second;
        }
        else {
            std::string meshType = type;
            int meshNumber = meshNo;
            auto i = importedObjMap.find(meshType);
            if (i != importedObjMap.end())
            {
                if (importedMeshesName != meshType)
                {
                    importedMeshes.clear();
                    importedMeshes = loadImportedMeshes(i->second);
                    importedMeshesName = meshType;
                }
                createMeshBuffers(importedMeshes[meshNumber].meshData, vbh, ibh);
                diffuseTexture = importedMeshes[meshNumber].diffuseTexture;
            }
        }

        // Create instance
        Instance* instance = new Instance(id, name, type, pos[0], pos[1], pos[2], vbh, ibh);
        instance->meshNumber = meshNo;
        // If this instance was created from an imported mesh, cache its editable mesh data.
        if (!importedMeshes.empty() && meshNo >= 0 && meshNo < (int)importedMeshes.size())
        {
            g_InstanceMeshData[instance->id] = importedMeshes[meshNo].meshData;
        }
        // Otherwise, try to attach a base mesh template for this type.
        if (g_InstanceMeshData.find(instance->id) == g_InstanceMeshData.end())
        {
            RegisterInstanceMeshFromType(instance);
        }
        instance->rotation[0] = rot[0]; instance->rotation[1] = rot[1]; instance->rotation[2] = rot[2];
        instance->scale[0] = scale[0]; instance->scale[1] = scale[1]; instance->scale[2] = scale[2];
        instance->objectColor[0] = color[0]; instance->objectColor[1] = color[1];
        instance->objectColor[2] = color[2]; instance->objectColor[3] = color[3];
        instance->lightProps.type = static_cast<LightType>(lightType);
        instance->lightProps.direction[0] = lightDirection[0]; instance->lightProps.direction[1] = lightDirection[1]; instance->lightProps.direction[2] = lightDirection[2];
        instance->lightProps.intensity = intensity;
        instance->lightProps.range = range;
        instance->lightProps.coneAngle = coneAngle;
        instance->lightProps.color[0] = lightColor[0]; instance->lightProps.color[1] = lightColor[1]; instance->lightProps.color[2] = lightColor[2]; instance->lightProps.color[3] = lightColor[3];
        if (instance->type == "light") {
            instance->isLight = true;
            if (instance->lightProps.type == LightType::Spot || instance->lightProps.type == LightType::Directional) {
                auto it = bufferMap.find("cone");
                if (it != bufferMap.end())
                {
                    instance->vertexBuffer = it->second.first;
                    instance->indexBuffer = it->second.second;
                }
            }
        }
        instance->inkColor[0] = inkColor[0]; instance->inkColor[1] = inkColor[1]; instance->inkColor[2] = inkColor[2]; instance->inkColor[3] = inkColor[3];
        instance->epsilonValue = epsilonValue;
        instance->strokeMultiplier = strokeMultiplier;
        instance->lineAngle1 = lineAngle1;
        instance->lineAngle2 = lineAngle2;
        instance->patternScale = patternScale;
        instance->lineThickness = lineThickness;
        instance->transparencyValue = transparencyValue;
        instance->crosshatchMode = crosshatchMode;
        instance->layerPatternScale = layerPatternScale;
        instance->layerStrokeMult = layerStrokeMult;
        instance->layerAngle = layerAngle;
        instance->layerLineThickness = layerLineThickness;
        instance->centerX = centerX;
        instance->centerZ = centerZ;
        instance->radius = radius;
        instance->rotationSpeed = rotationSpeed;
        instance->instanceAngle = instanceAngle;
        instance->basePosition[0] = basePosition[0]; instance->basePosition[1] = basePosition[1]; instance->basePosition[2] = basePosition[2];
        instance->lightAnim.amplitude[0] = amplitude[0]; instance->lightAnim.amplitude[1] = amplitude[1]; instance->lightAnim.amplitude[2] = amplitude[2];
        instance->lightAnim.frequency[0] = frequency[0]; instance->lightAnim.frequency[1] = frequency[1]; instance->lightAnim.frequency[2] = frequency[2];
        instance->lightAnim.phase[0] = phase[0]; instance->lightAnim.phase[1] = phase[1]; instance->lightAnim.phase[2] = phase[2];
        instance->lightAnim.enabled = static_cast<bool>(animationEnabled);
        instance->textContent = textContent;

        // Assign texture
        if (textureName != "none")
        {
            for (const auto& tex : availableTextures)
            {
                if (tex.name == textureName)
                {
                    instance->diffuseTexture = tex.handle;
                    break;
                }
            }
        }
        else {
            instance->diffuseTexture = diffuseTexture;
        }

        if (instance->type == "text") {
            // Immediately generate its text texture.
            updateTextTexture(instance);
        }

        // Assign noise texture
        if (noiseTextureName != "none")
        {
            for (const auto& tex : availableNoiseTextures)
            {
                if (tex.name == noiseTextureName)
                {
                    instance->noiseTexture = tex.handle;
                    break;
                }
            }
        }

        instanceMap[id] = instance;

        // If it has a parent, store the relationship
        if (parentID != -1)
        {
            parentAssignments.push_back({ id, parentID });
        }
        else
        {
            // If it has no parent, it's a top-level instance
            instances.push_back(instance);
        }
    }

    // Restore parent-child relationships
    for (const auto& [childID, parentID] : parentAssignments)
    {
        auto childIt = instanceMap.find(childID);
        auto parentIt = instanceMap.find(parentID);
        if (childIt != instanceMap.end() && parentIt != instanceMap.end())
        {
            parentIt->second->addChild(childIt->second);
        }
    }

    file.close();
    std::cout << "Scene loaded from " << loadFilePath << std::endl;
    // Replace the existing code with this
    int maxId = 0;
    for (const Instance* inst : instances) {
        findMaxInstanceId(inst, maxId);
    }
    instanceCounter = maxId + 1;
    return importedObjMap;
}


void ShowTopLevelDropTarget(std::vector<Instance*>& instances)
{
    // Reserve a region across the available width (adjust the height as needed)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(avail.x, 50)); // 50 pixels tall drop area
    ImGui::Text("Drop here to make top-level");

    // Get the region's rectangle
    ImVec2 dropPos = ImGui::GetItemRectMin();
    ImVec2 dropSize = ImGui::GetItemRectSize();
    ImRect dropRect(dropPos, ImVec2(dropPos.x + dropSize.x, dropPos.y + dropSize.y));
    // Use a custom drag-drop target on that dummy widget.
    if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("TopLevelDropTarget")))
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_INSTANCE"))
        {
            Instance* dropped = *(Instance**)payload->Data;
            // Remove the dropped node from its current parent (if any)
            if (dropped->parent)
            {
                auto it = std::find(dropped->parent->children.begin(), dropped->parent->children.end(), dropped);
                if (it != dropped->parent->children.end())
                    dropped->parent->children.erase(it);
                dropped->parent = nullptr;
            }
            // If the node isn't already top-level, add it to the global list.
            auto it = std::find(instances.begin(), instances.end(), dropped);
            if (it == instances.end())
            {
                instances.push_back(dropped);
            }
        }
        ImGui::EndDragDropTarget();
    }
}
std::string OpenFileDialog(HWND owner, const char* filter)
{
#ifdef _WIN32
    OPENFILENAME ofn;
    char fileName[MAX_PATH] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner; // Use the passed HWND
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "";

    if (GetOpenFileName(&ofn))
    {
        return std::string(fileName);
    }
#endif
    return std::string();
}

std::string GetRelativePath(const std::string& absolutePath, const std::string& base = fs::current_path().string())
{
    // Use the error_code overload to avoid throwing filesystem_error if paths
    // are on different roots or other issues occur. Fall back to the absolute
    // path on failure.
    fs::path absPath(absolutePath);
    fs::path basePath(base);
    std::error_code ec;
    fs::path relPath = fs::relative(absPath, basePath, ec);
    if (ec)
    {
        std::cerr << "[GetRelativePath] Warning: failed to compute relative path from '"
                  << absPath.string() << "' to base '" << basePath.string()
                  << "': " << ec.message() << ". Using absolute path instead.\n";
        return absolutePath;
    }
    return relPath.string();
}

Instance* findInstanceById(const std::vector<Instance*>& instances, int id)
{
    for (Instance* inst : instances)
    {
        if (inst->id == id)
            return inst;
        // Recursively check children.
        Instance* childResult = findInstanceById(inst->children, id);
        if (childResult != nullptr)
            return childResult;
    }
    return nullptr;
}

void renderInstancePickingRecursive(const Instance* instance, const float* parentTransform, uint32_t viewID)
{
    // Compute the local transform.
    float local[16];
    bx::mtxSRT(local,
        instance->scale[0], instance->scale[1], instance->scale[2],
        instance->rotation[0], instance->rotation[1], instance->rotation[2],
        instance->position[0], instance->position[1], instance->position[2]
    );

    // Compute the world transform by combining the parent's transform (if any) with the local.
    float world[16];
    if (parentTransform)
    {
        bx::mtxMul(world, local, parentTransform);
    }
    else
    {
        std::memcpy(world, local, sizeof(world));
    }

    bgfx::setTransform(world);

    // Submit the geometry if valid.
    const bgfx::VertexBufferHandle invalidVbh = BGFX_INVALID_HANDLE;
    const bgfx::IndexBufferHandle invalidIbh = BGFX_INVALID_HANDLE;
    // Draw geometry if valid.
    if (instance->vertexBuffer.idx != invalidVbh.idx &&
        instance->indexBuffer.idx != invalidIbh.idx)
    {
        // Encode the instance's unique ID into a color and set the picking uniform
        // only when we're actually going to submit geometry. This avoids setting
        // u_id multiple times without an intervening submit, which BGFX asserts on.
        uint32_t id = instance->id;
        float idColor[4] = {
            ((id >> 16) & 0xFF) / 255.0f,
            ((id >> 8) & 0xFF) / 255.0f,
            (id & 0xFF) / 255.0f,
            1.0f
        };
        bgfx::setUniform(u_id, idColor);

        bgfx::setVertexBuffer(0, instance->vertexBuffer);
        bgfx::setIndexBuffer(instance->indexBuffer);
        bgfx::submit(viewID, pickingProgram);
    }

    // Recursively render children with the new world transform.
    for (const Instance* child : instance->children)
    {
        renderInstancePickingRecursive(child, world, viewID);
    }
}

//-----------------------------------------------------------------------------
// Uniforms for light data (for shader)
const int MAX_LIGHTS = 16;
static bgfx::UniformHandle u_lights;   // array of vec4's (MAX_LIGHTS*4)
static bgfx::UniformHandle u_numLights;  // vec4 (x holds number of lights)

void collectLights(const Instance* inst, float* lightsData, int& numLights, const float* parentTransform = nullptr)
{
    if (!inst)
        return;

    // Use local matrix for this instance
    float local[16];
    bx::mtxSRT(local,
        inst->scale[0], inst->scale[1], inst->scale[2],
        inst->rotation[0], inst->rotation[1], inst->rotation[2],
        inst->position[0], inst->position[1], inst->position[2]);

    // Compute world matrix by combining with parent transform (if any)
    float world[16];
    if (parentTransform) {
        bx::mtxMul(world, local, parentTransform);
    }
    else {
        std::memcpy(world, local, sizeof(world));
    }

    if (inst->isLight)
    {
        if (numLights >= MAX_LIGHTS)
            return;

        int base = numLights * 16;

        // Type and intensity remain unchanged
        lightsData[base + 0] = static_cast<float>(inst->lightProps.type); // type
        lightsData[base + 1] = inst->lightProps.intensity;
        lightsData[base + 2] = 0.0f;
        lightsData[base + 3] = 0.0f;

        // Transform position from local to world space
        float worldPos[3] = { 0.0f, 0.0f, 0.0f };
        worldPos[0] = world[12]; // Translation is stored in elements 12, 13, 14
        worldPos[1] = world[13];
        worldPos[2] = world[14];

        lightsData[base + 4] = worldPos[0];
        lightsData[base + 5] = worldPos[1];
        lightsData[base + 6] = worldPos[2];
        lightsData[base + 7] = 1.0f;

        // For direction, we need to transform by the rotation part of the matrix only
        // (without translation), and then normalize the result
        float direction[3] = {
            inst->lightProps.direction[0],
            inst->lightProps.direction[1],
            inst->lightProps.direction[2]
        };

        // Transform direction vector using the 3x3 rotation part of the world matrix
        float worldDir[3] = { 0.0f, 0.0f, 0.0f };
        worldDir[0] = world[0] * direction[0] + world[4] * direction[1] + world[8] * direction[2];
        worldDir[1] = world[1] * direction[0] + world[5] * direction[1] + world[9] * direction[2];
        worldDir[2] = world[2] * direction[0] + world[6] * direction[1] + world[10] * direction[2];

        // Normalize the direction
        float length = std::sqrt(worldDir[0] * worldDir[0] + worldDir[1] * worldDir[1] + worldDir[2] * worldDir[2]);
        if (length > 0.0001f) {
            worldDir[0] /= length;
            worldDir[1] /= length;
            worldDir[2] /= length;
        }

        lightsData[base + 8] = worldDir[0];
        lightsData[base + 9] = worldDir[1];
        lightsData[base + 10] = worldDir[2];
        lightsData[base + 11] = inst->lightProps.coneAngle;

        // Light color remains unchanged
        lightsData[base + 12] = inst->lightProps.color[0];
        lightsData[base + 13] = inst->lightProps.color[1];
        lightsData[base + 14] = inst->lightProps.color[2];
        lightsData[base + 15] = inst->lightProps.range;

        numLights++;
    }

    // Process children with the current world transform
    for (const Instance* child : inst->children)
    {
        if (numLights >= MAX_LIGHTS)
            break;
        collectLights(child, lightsData, numLights, world);
    }
}

// Add this to your code - preferably right before main() or in a utility file
void updateRotatingLights(std::vector<Instance*>& instances, float deltaTime) {
    for (Instance* inst : instances) {
        if (inst->isLight && inst->name.find("rotating_light") != std::string::npos) {
            // Update angle by speed factor 
            inst->instanceAngle -= deltaTime * inst->rotationSpeed;

            if (inst->instanceAngle < 0) {
                inst->instanceAngle += TAU;  // Keep the angle within [0, TAU]
            }
            else if (inst->instanceAngle > TAU) {
                inst->instanceAngle -= TAU;  // Keep the angle within [0, TAU]
            }
            // Update position
            inst->position[0] = inst->centerX + inst->radius * cos(inst->instanceAngle);
            inst->position[2] = inst->centerZ + inst->radius * sin(inst->instanceAngle);

            // Calculate direction vector to center point (0,0,0)
            float dirX = inst->centerX - inst->position[0];
            float dirZ = inst->centerZ - inst->position[2];

            // Normalize the direction
            float length = sqrt(dirX * dirX + dirZ * dirZ);
            if (length > 0.001f) {
                dirX /= length;
                dirZ /= length;

                // Convert direction to rotation angles
                // Yaw (Y-axis rotation) - determines the horizontal orientation
                inst->rotation[1] = atan2(dirZ, dirX) + 3.14159f / 2.0f;

                // Keep roll (Z-axis rotation) at 0
                inst->rotation[2] = 0.0f;
            }
        }
    }
}

void createNewCamera() {
    Camera newCam;
    newCam = cameras[currentCameraIndex];
    cameras.push_back(newCam);
}

namespace Gallery {
    static bool galleryOpen = false;
    static bool fullscreenOpen = false;
    static int selectedImage = -1;
    static std::vector<bgfx::TextureHandle> textures;
    static std::vector<ImVec2> imgSizes;

    // Call once at startup
    void LoadGallery(const std::string& folderPath) {
        textures.clear();
        imgSizes.clear();
        try
        {
            if (!std::filesystem::exists(folderPath))
            {
                std::cerr << "[Gallery::LoadGallery] Folder does not exist: " << folderPath << std::endl;
                return;
            }
            if (!std::filesystem::is_directory(folderPath))
            {
                std::cerr << "[Gallery::LoadGallery] Path is not a directory: " << folderPath << std::endl;
                return;
            }

            for (auto& entry : std::filesystem::directory_iterator(folderPath)) {
                if (!entry.is_regular_file()) continue;
                auto path = entry.path().string();
                int w, h, channels;
                unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
                if (!data) continue;
                const bgfx::Memory* mem = bgfx::copy(data, w * h * 4);
                stbi_image_free(data);
                auto tex = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1,
                    bgfx::TextureFormat::RGBA8, 0, mem);
                if (bgfx::isValid(tex)) {
                    textures.push_back(tex);
                    imgSizes.push_back(ImVec2((float)w, (float)h));
                }
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::cerr << "[Gallery::LoadGallery] filesystem_error: " << e.what() << std::endl;
        }
    }
}
void takeScreenshotAsPng(bgfx::FrameBufferHandle fb, const std::string& baseName) {
    // Use non-throwing overload to avoid terminating if directory can't be created.
    std::error_code dirEc;
    std::filesystem::create_directory("screenshots", dirEc);
    if (dirEc)
    {
        std::cerr << "[Screenshot] Warning: failed to create 'screenshots' directory: "
                  << dirEc.message() << std::endl;
    }

    std::string normalPath = "screenshots/" + baseName;
    std::string tgaPath = "screenshots/" + baseName + ".tga";
    std::string pngPath = "screenshots/" + baseName + ".png";


    // Tell BGFX to save the framebuffer contents to a file
    bgfx::requestScreenShot(fb, normalPath.c_str());
    std::cout << "[Screenshot] Requested .tga: " << tgaPath << std::endl;

    std::thread([tgaPath, pngPath]() {
        namespace fs = std::filesystem;

        const int maxWaitMs = 10000;
        const int pollIntervalMs = 100;
        int waitedMs = 0;

        while (!fs::exists(tgaPath) && waitedMs < maxWaitMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            waitedMs += pollIntervalMs;
        }

        if (!fs::exists(tgaPath)) {
            std::cerr << "[Screenshot] TGA file not found after waiting." << std::endl;
            return;
        }

        int width, height, channels;
        unsigned char* data = stbi_load(tgaPath.c_str(), &width, &height, &channels, 4); // Force RGBA

        if (!data) {
            std::cerr << "[Screenshot] Failed to load .tga: " << tgaPath << std::endl;
            return;
        }

        if (stbi_write_png(pngPath.c_str(), width, height, 4, data, width * 4)) {
            std::cout << "[Screenshot] Converted to .png: " << pngPath << std::endl;
            fs::remove(tgaPath); // Clean up
        }
        else {
            std::cerr << "[Screenshot] Failed to save .png: " << pngPath << std::endl;
        }

        stbi_image_free(data);
        Gallery::LoadGallery("./screenshots");
        }).detach(); // Detach the thread so it runs independently
}

void ResetCrosshatchSettings()
{
    if (crosshatchMode == 0 || crosshatchMode == 1 || crosshatchMode == 3) {
        // default color (RGBA)
        inkColor[0] = 0.0f; inkColor[1] = 0.0f;
        inkColor[2] = 0.0f; inkColor[3] = 1.0f;

        // default floats
        epsilonValue = 0.02f;
        strokeMultiplier = 1.0f;
        lineAngle1 = TAU / 8.0f;
        lineAngle2 = TAU / 16.0f;
        patternScale = 0.4f;
        lineThickness = 0.3f;
        transparencyValue = 1.0f;

        // inner-layer defaults
        layerPatternScale = 0.5f;
        layerStrokeMult = 0.250f;
        layerAngle = 2.983f;
        layerLineThickness = 10.0f;
    }
    else if (crosshatchMode == 2) {
        // default color (RGBA)
        inkColor[0] = 0.0f; inkColor[1] = 0.0f;
        inkColor[2] = 0.0f; inkColor[3] = 1.0f;

        // default floats
        epsilonValue = 0.02f;
        strokeMultiplier = 1.0f;
        lineAngle1 = TAU / 8.0f;
        lineAngle2 = TAU / 16.0f;
        patternScale = 3.0f;
        lineThickness = 0.3f;
        transparencyValue = 1.0f;

        // inner-layer defaults
        layerPatternScale = 1.0f;
        layerStrokeMult = 0.250f;
        layerAngle = 2.983f;
        layerLineThickness = 10.0f;
    }

}

static void SetupLichtFeldLikeStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // General look â€“ slightly rounded, compact, editor-like.
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(10.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);

    ImVec4* colors = style.Colors;

    // Base palette: dark neutral background, subtle panel separation.
    colors[ImGuiCol_WindowBg]        = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_ChildBg]         = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_PopupBg]         = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);

    colors[ImGuiCol_Border]          = ImVec4(0.19f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_Separator]       = ImVec4(0.20f, 0.24f, 0.28f, 1.00f);

    colors[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.19f, 0.23f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]   = ImVec4(0.21f, 0.27f, 0.30f, 1.00f);

    colors[ImGuiCol_TitleBg]         = ImVec4(0.06f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive]   = ImVec4(0.07f, 0.13f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]= colors[ImGuiCol_TitleBg];
    colors[ImGuiCol_MenuBarBg]       = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);

    // Accent color â€“ keep your green accent.
    ImVec4 accent      = ImVec4(0.173f, 0.796f, 0.435f, 1.00f);
    ImVec4 accent_dark = ImVec4(0.078f, 0.361f, 0.282f, 1.00f);

    colors[ImGuiCol_CheckMark]       = accent;
    colors[ImGuiCol_SliderGrab]      = accent;
    colors[ImGuiCol_SliderGrabActive]= ImVec4(0.200f, 1.000f, 0.500f, 1.00f);

    colors[ImGuiCol_Button]          = accent_dark;
    colors[ImGuiCol_ButtonHovered]   = accent;
    colors[ImGuiCol_ButtonActive]    = accent;

    colors[ImGuiCol_Tab]             = accent_dark;
    colors[ImGuiCol_TabHovered]      = accent;
    colors[ImGuiCol_TabActive]       = accent;
    colors[ImGuiCol_TabUnfocused]    = accent_dark;
    colors[ImGuiCol_TabUnfocusedActive] = accent;

    colors[ImGuiCol_Header]          = accent_dark;
    colors[ImGuiCol_HeaderHovered]   = accent;
    colors[ImGuiCol_HeaderActive]    = accent;

    colors[ImGuiCol_ResizeGrip]      = ImVec4(0.16f, 0.24f, 0.22f, 0.80f);
    colors[ImGuiCol_ResizeGripHovered]=accent;
    colors[ImGuiCol_ResizeGripActive]= accent;
}

// Shared layout state for fixed-right LichtFeld-like sidebar
static float g_MainMenuHeight   = 32.0f;   // Updated each frame from the menu window height
static float g_LeftPanelWidth   = 64.0f;   // Left toolbar width in pixels
static float g_RightPanelWidth  = 420.0f;  // Sidebar width in pixels
static float g_ObjPanelRatio    = 0.33f;   // Fraction of sidebar height for Object List
static float g_InspectorRatio   = 0.34f;   // Fraction for Inspector (rest goes to Reconstructor)

// 3D Viewport window rect (LichtFeld-style: its own window with rounded corners), set each frame when the window is built
static float g_ViewportRectX = 0.0f, g_ViewportRectY = 0.0f, g_ViewportRectW = 800.0f, g_ViewportRectH = 600.0f;

// Application background: dark so the 3D viewport window shape (rounded corners) is visible (0xRRGGBBAA)
static const uint32_t g_AppBackgroundColor = 0x0d0d0dff;
static const uint32_t g_ViewportClearColor  = 0x303030ff;  // 3D world clear (unchanged)

// Full inspector UI body (transform, light, material, delete, etc.). Used by the right sidebar only.
static void RenderInspectorBody(Instance* selectedInstance, std::vector<Instance*>& instances);

// Fixed-layout left sidebar: operation toolbar (Translate/Rotate/Scale) with icon buttons.
static void RenderLeftSidebar()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    const float panel_h = vp->WorkSize.y - g_MainMenuHeight;
    if (panel_h <= 0.0f)
        return;

    const float panel_x = vp->WorkPos.x;
    const float panel_y = vp->WorkPos.y + g_MainMenuHeight;

    // Clamp width to something sensible so it never eats the whole screen.
    const float min_w = 44.0f;
    const float max_w = ImMax(44.0f, vp->WorkSize.x * 0.25f);
    g_LeftPanelWidth = ImClamp(g_LeftPanelWidth, min_w, max_w);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(g_LeftPanelWidth, panel_h), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.08f, 0.95f));

    if (ImGui::Begin("##LeftSidebar", nullptr, flags))
    {
        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);

        const float button_sz = ImClamp(g_LeftPanelWidth - 16.0f, 28.0f, 48.0f);
        const float icon_pad = 7.0f;
        const float circle_r = 4.0f;

        auto draw_translate_icon = [](ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col)
        {
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float w = (p1.x - p0.x);
            const float h = (p1.y - p0.y);
            const float len = ImMin(w, h) * 0.28f;
            const float ah = ImMin(w, h) * 0.10f;
            const float thick = 2.0f;

            // Horizontal line + arrows
            dl->AddLine(ImVec2(c.x - len, c.y), ImVec2(c.x + len, c.y), col, thick);
            dl->AddTriangleFilled(ImVec2(c.x + len, c.y), ImVec2(c.x + len - ah, c.y - ah), ImVec2(c.x + len - ah, c.y + ah), col);
            dl->AddTriangleFilled(ImVec2(c.x - len, c.y), ImVec2(c.x - len + ah, c.y - ah), ImVec2(c.x - len + ah, c.y + ah), col);

            // Vertical line + arrows
            dl->AddLine(ImVec2(c.x, c.y - len), ImVec2(c.x, c.y + len), col, thick);
            dl->AddTriangleFilled(ImVec2(c.x, c.y - len), ImVec2(c.x - ah, c.y - len + ah), ImVec2(c.x + ah, c.y - len + ah), col);
            dl->AddTriangleFilled(ImVec2(c.x, c.y + len), ImVec2(c.x - ah, c.y + len - ah), ImVec2(c.x + ah, c.y + len - ah), col);
        };

        auto draw_rotate_icon = [](ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col)
        {
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float w = (p1.x - p0.x);
            const float h = (p1.y - p0.y);
            const float r = ImMin(w, h) * 0.25f;
            const float thick = 2.0f;

            dl->AddCircle(c, r, col, 24, thick);
            // Arrow head at top-right quadrant
            const ImVec2 tip(c.x + r * 0.70f, c.y - r * 0.70f);
            const float ah = ImMin(w, h) * 0.10f;
            dl->AddTriangleFilled(tip, ImVec2(tip.x - ah, tip.y), ImVec2(tip.x, tip.y + ah), col);
        };

        auto draw_scale_icon = [](ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col)
        {
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float w = (p1.x - p0.x);
            const float h = (p1.y - p0.y);
            const float s = ImMin(w, h) * 0.40f;
            const float thick = 2.0f;

            ImVec2 a(c.x - s * 0.5f, c.y - s * 0.5f);
            ImVec2 b(c.x + s * 0.5f, c.y + s * 0.5f);
            dl->AddRect(a, b, col, 0.0f, 0, thick);

            // Diagonal arrow (bottom-left -> top-right)
            const ImVec2 p_from(c.x - s * 0.35f, c.y + s * 0.35f);
            const ImVec2 p_to(c.x + s * 0.35f, c.y - s * 0.35f);
            dl->AddLine(p_from, p_to, col, thick);
            const float ah = ImMin(w, h) * 0.10f;
            dl->AddTriangleFilled(p_to, ImVec2(p_to.x - ah, p_to.y), ImVec2(p_to.x, p_to.y + ah), col);
        };

        auto operation_button = [&](const char* id, const char* tooltip, ImGuizmo::OPERATION op, bool enabled, auto&& draw_icon)
        {
            const bool selected = (currentGizmoOperation == op);

            if (!enabled)
                ImGui::BeginDisabled();

            if (ImGui::Button(id, ImVec2(button_sz, button_sz)))
                currentGizmoOperation = op;

            const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (selected)
            {
                dl->AddCircleFilled(ImVec2(r.Min.x + 8.0f, r.Min.y + 8.0f), circle_r, accent);
            }

            const ImU32 icon_col = enabled ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec2 icon0(r.Min.x + icon_pad, r.Min.y + icon_pad);
            const ImVec2 icon1(r.Max.x - icon_pad, r.Max.y - icon_pad);
            draw_icon(dl, icon0, icon1, icon_col);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", tooltip);

            if (!enabled)
                ImGui::EndDisabled();
        };

        const bool hasSelection = (selectedInstance != nullptr);

        // Keep behavior consistent with the inspector gizmo controls:
        // - Translate always available
        // - Rotate available for any selected object (including lights)
        // - Scale available (matches existing inspector UI)
        operation_button("##op_translate", "Translate (1)", ImGuizmo::TRANSLATE, hasSelection, draw_translate_icon);
        ImGui::Spacing();
        operation_button("##op_rotate", "Rotate (2)", ImGuizmo::ROTATE, hasSelection, draw_rotate_icon);
        ImGui::Spacing();
        operation_button("##op_scale", "Scale (3)", ImGuizmo::SCALE, hasSelection, draw_scale_icon);

        // If nothing is selected, hint that the toolbar still affects the gizmo once an object is picked.
        if (!hasSelection)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + button_sz);
            // ImGui::TextDisabled("Select an object to use the gizmo.");
            ImGui::PopTextWrapPos();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// Fixed-layout right sidebar, no docking/tabs, LichtFeld-like.
static void RenderRightSidebar()
{
    if (!show_ObjectList && !show_Inspector && !show_Reconstructor)
        return;

    // Ensure we have a valid instances vector to work with.
    if (!g_Instances)
        return;

    auto& instances = *g_Instances;

    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Compute sidebar rect under the main menu bar.
    const float panel_h = vp->WorkSize.y - g_MainMenuHeight;
    if (panel_h <= 0.0f)
        return;

    // Clamp sidebar width.
    const float min_w = 280.0f;
    const float max_w = vp->WorkSize.x * 0.5f;
    g_RightPanelWidth = ImClamp(g_RightPanelWidth, min_w, max_w);

    const float panel_x = vp->WorkPos.x + vp->WorkSize.x - g_RightPanelWidth;
    const float panel_y = vp->WorkPos.y + g_MainMenuHeight;

    // Allow horizontal resize by grabbing the left edge.
    const float EDGE_GRAB_W = 6.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovering_edge =
        mouse.x >= panel_x - EDGE_GRAB_W && mouse.x <= panel_x + EDGE_GRAB_W &&
        mouse.y >= panel_y && mouse.y <= panel_y + panel_h;

    static bool resizing_panel = false;
    if (hovering_edge && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        resizing_panel = true;
    if (resizing_panel && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        resizing_panel = false;
    if (resizing_panel)
        g_RightPanelWidth = ImClamp(g_RightPanelWidth - ImGui::GetIO().MouseDelta.x, min_w, max_w);
    if (hovering_edge || resizing_panel)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(g_RightPanelWidth, panel_h), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.08f, 0.95f));

    if (ImGui::Begin("##RightSidebar", nullptr, flags))
    {
        const float avail_h = ImGui::GetContentRegionAvail().y;
        const float splitter_h = 5.0f;
        const float min_h = 80.0f;

        // Ensure ratios stay in a useful range.
        g_ObjPanelRatio      = ImClamp(g_ObjPanelRatio, 0.15f, 0.7f);
        g_InspectorRatio     = ImClamp(g_InspectorRatio, 0.15f, 0.7f);
        const float sum_ratio = g_ObjPanelRatio + g_InspectorRatio;
        if (sum_ratio > 0.9f)
        {
            g_ObjPanelRatio      *= 0.9f / sum_ratio;
            g_InspectorRatio     *= 0.9f / sum_ratio;
        }

        float obj_h        = avail_h * g_ObjPanelRatio;
        float inspector_h  = avail_h * g_InspectorRatio;
        float recon_h      = avail_h - obj_h - inspector_h - 2.0f * splitter_h;

        obj_h       = ImClamp(obj_h, min_h, avail_h - 2.0f * min_h - 2.0f * splitter_h);
        inspector_h = ImClamp(inspector_h, min_h, avail_h - obj_h - min_h - 2.0f * splitter_h);
        recon_h     = ImClamp(recon_h, min_h, avail_h - obj_h - inspector_h - 2.0f * splitter_h);

        // Helper to draw a section header.
        auto draw_section_header = [](const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.19f, 0.21f, 0.24f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::TextUnformatted(label);
        };

        // OBJECT LIST
        if (show_ObjectList && obj_h > 0.0f)
        {
            if (ImGui::BeginChild("##ObjPanel", ImVec2(0, obj_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                draw_section_header("Objects");

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::CollapsingHeader("##ObjectListTree", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // For each top-level instance, show its tree.
                    for (Instance* instance : instances)
                    {
                        ShowInstanceTree(instance, selectedInstance, instances);
                    }

                    // Now, show the drop target region for reparenting to top-level.
                    ShowTopLevelDropTarget(instances);
                }
            }
            ImGui::EndChild();
        }

        // Splitter between Object List and Inspector
        ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_Separator]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_HeaderHovered]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_HeaderActive]);
        ImGui::Button("##SplitObjInspector", ImVec2(-1.0f, splitter_h)); 
        if (ImGui::IsItemActive())
        {
            g_ObjPanelRatio = ImClamp(
                g_ObjPanelRatio + ImGui::GetIO().MouseDelta.y / avail_h,
                0.15f, 0.7f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImGui::PopStyleColor(3);

        // INSPECTOR
        if (show_Inspector && inspector_h > 0.0f)
        {
            if (ImGui::BeginChild("##InspectorPanel", ImVec2(0, inspector_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                draw_section_header("Inspector");
                // Inline body of previous Inspector window
                ImGui::SetWindowFontScale(0.85f);

                // If an instance is selected, show its transform controls.
                if (selectedInstance)
                {
                    // Gizmo is drawn in the 3D viewport window (##3DViewport) so it receives input and aligns with the scene
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);//collapsing header set to open initially
                    if (ImGui::CollapsingHeader("Transform Controls/Gizmo"))
                    {
                        ImGui::Separator();
                        ImGui::Text("Selected: %s", selectedInstance->name.c_str());
                        RenderInspectorBody(selectedInstance, instances);
                    }
                }
            }
            ImGui::EndChild();
        }

        // Splitter between Inspector and Reconstructor
        ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_Separator]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_HeaderHovered]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_HeaderActive]);
        ImGui::Button("##SplitInspectorRecon", ImVec2(-1.0f, splitter_h));
        if (ImGui::IsItemActive())
        {
            g_InspectorRatio = ImClamp(
                g_InspectorRatio + ImGui::GetIO().MouseDelta.y / avail_h,
                0.15f, 0.7f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImGui::PopStyleColor(3);

        // RECONSTRUCTOR
        if (show_Reconstructor && recon_h > 0.0f)
        {
            if (ImGui::BeginChild("##ReconPanel", ImVec2(0, recon_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
            {
                draw_section_header("3D Reconstructor");
                Reconstructor::Draw();
            }
            ImGui::EndChild();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

static void RenderInspectorBody(Instance* selectedInstance, std::vector<Instance*>& instances)
{
    if (!selectedInstance) return;
    float avail_width = ImGui::GetContentRegionAvail().x;
    float label_width = 110.0f;
    float input_width = avail_width - label_width;

    // Gizmo operation selection
    if (ImGui::RadioButton("Translate##op", currentGizmoOperation == ImGuizmo::TRANSLATE))
        currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::SameLine(0, 15);
    if (ImGui::RadioButton("Rotate##op", currentGizmoOperation == ImGuizmo::ROTATE))
        currentGizmoOperation = ImGuizmo::ROTATE;
    ImGui::SameLine(0, 15);
    if (ImGui::RadioButton("Scale##op", currentGizmoOperation == ImGuizmo::SCALE))
        currentGizmoOperation = ImGuizmo::SCALE;

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Position");
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(input_width);
    ImGui::DragFloat3("##pos", selectedInstance->position, 0.01f);

    // Rotation is now editable for any selected object (including lights).
    {
        float rotDeg[3] = { bx::toDeg(selectedInstance->rotation[0]), bx::toDeg(selectedInstance->rotation[1]), bx::toDeg(selectedInstance->rotation[2]) };
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Rotation");
        ImGui::SameLine(label_width);
        ImGui::SetNextItemWidth(input_width);
        if (ImGui::DragFloat3("##rot", rotDeg, 0.1f))
        {
            selectedInstance->rotation[0] = bx::toRad(rotDeg[0]);
            selectedInstance->rotation[1] = bx::toRad(rotDeg[1]);
            selectedInstance->rotation[2] = bx::toRad(rotDeg[2]);
        }
    }

    if (!selectedInstance->isLight)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Scale");
        ImGui::SameLine(label_width);
        ImGui::SetNextItemWidth(input_width);
        ImGui::DragFloat3("##scale", selectedInstance->scale, 0.01f);
    }

    ImGui::Spacing();
    if (currentGizmoOperation != ImGuizmo::SCALE)
    {
        if (ImGui::RadioButton("World##mode", currentGizmoMode == ImGuizmo::WORLD)) currentGizmoMode = ImGuizmo::WORLD;
        ImGui::SameLine(0, 15);
        if (ImGui::RadioButton("Local##mode", currentGizmoMode == ImGuizmo::LOCAL)) currentGizmoMode = ImGuizmo::LOCAL;
    }

    ImGui::Separator();
    if (selectedInstance->isLight)
    {
        if (ImGui::CollapsingHeader("Light Settings"))
        {
            const char* lightTypes[] = { "Directional", "Point", "Spot" };
            int currentType = static_cast<int>(selectedInstance->lightProps.type);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Type");
            ImGui::SameLine(label_width);
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::Combo("##lighttype", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes)))
            {
                selectedInstance->lightProps.type = static_cast<LightType>(currentType);
                if (selectedInstance->lightProps.type == LightType::Point)
                { selectedInstance->vertexBuffer = g_vbh_sphere; selectedInstance->indexBuffer = g_ibh_sphere; }
                else if (selectedInstance->lightProps.type == LightType::Spot || selectedInstance->lightProps.type == LightType::Directional)
                { selectedInstance->vertexBuffer = g_vbh_cone; selectedInstance->indexBuffer = g_ibh_cone; }
            }
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Color");
            ImGui::SameLine(label_width);
            ImGui::SetNextItemWidth(input_width);
            ImGui::ColorEdit4("##lightcol", selectedInstance->lightProps.color);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Intensity");
            ImGui::SameLine(label_width);
            ImGui::SetNextItemWidth(input_width);
            ImGui::DragFloat("##intensity", &selectedInstance->lightProps.intensity, 0.01f, 0.0f, 10.0f);
            bool debugVisible = selectedInstance->showDebugVisual;
            if (ImGui::Checkbox("Show Light Debug Visual", &debugVisible)) selectedInstance->showDebugVisual = debugVisible;
        }
    }
    else
    {
        if (ImGui::Checkbox("Attribute (Unlit Vertex Color) Mode", &useAttributeMode)) { }
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Object Color");
        ImGui::SameLine(label_width);
        ImGui::SetNextItemWidth(input_width);
        ImGui::ColorEdit3("##objcolor", selectedInstance->objectColor);
        if (ImGui::CollapsingHeader("Material Editor") && g_availableTextures)
        {
            ImGui::BeginChild("TextureSelection", ImVec2(0, 80), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t i = 0; i < g_availableTextures->size(); i++)
            {
                ImTextureID texID = static_cast<ImTextureID>(static_cast<uintptr_t>((*g_availableTextures)[i].handle.idx));
                if (ImGui::ImageButton(std::to_string(i).c_str(), texID, ImVec2(64, 64)))
                    selectedInstance->diffuseTexture = (*g_availableTextures)[i].handle;
                if (i < g_availableTextures->size() - 1) ImGui::SameLine();
            }
            ImGui::EndChild();
            if (ImGui::Button("Clear Texture"))
            {
                selectedInstance->diffuseTexture = BGFX_INVALID_HANDLE;
                selectedInstance->material.tiling[0] = selectedInstance->material.tiling[1] = 1.0f;
                selectedInstance->material.offset[0] = selectedInstance->material.offset[1] = 0.0f;
                selectedInstance->material.albedo[0] = selectedInstance->material.albedo[1] = selectedInstance->material.albedo[2] = selectedInstance->material.albedo[3] = 1.0f;
            }
        }
        // Geometry / topology tools for selected object.
        if (ImGui::CollapsingHeader("Geometry / Topology Tools"))
        {
            bool hasMesh = HasEditableMeshData(selectedInstance);
            if (!hasMesh)
            {
                ImGui::TextWrapped("No editable mesh data available for this object. This usually means it was created from a primitive type (cube, sphere, etc.) but not defined as editable, or was loaded externally without being registered.");
                ImGui::Spacing();
                if (ImGui::Button("Use primitives instead (Cube, Sphere, etc.)"))
                {
                    // Just show an info message
                }
            }
            else
            {
                MeshData* mesh = GetEditableMeshData(selectedInstance);
                // Boundary visualization
                static ImVec4 s_boundaryColor = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
                ImGui::Separator();
                ImGui::Text("Boundaries");
                ImGui::SameLine();
                ImGui::ColorEdit4("##boundaryColor", (float*)&s_boundaryColor, ImGuiColorEditFlags_NoInputs);
                if (ImGui::Button("Color Boundary Vertices"))
                {
                    uint8_t r = (uint8_t)(s_boundaryColor.x * 255.0f);
                    uint8_t g = (uint8_t)(s_boundaryColor.y * 255.0f);
                    uint8_t b = (uint8_t)(s_boundaryColor.z * 255.0f);
                    uint8_t a = (uint8_t)(s_boundaryColor.w * 255.0f);
                    uint32_t abgr = PackAbgr(a, b, g, r);
                    ColorBoundaryVertices(*mesh, abgr);
                    ApplyEditableMeshToInstance(selectedInstance);
                }

                // Subdivision
                static int s_subdivideLevels = 1;
                ImGui::Separator();
                ImGui::Text("Subdivision");
                ImGui::SetNextItemWidth(input_width);
                ImGui::SliderInt("Levels##subdiv", &s_subdivideLevels, 1, 3);
                if (ImGui::Button("Apply Subdivision"))
                {
                    SubdivideMesh(*mesh, s_subdivideLevels);
                    ApplyEditableMeshToInstance(selectedInstance);
                }

                // Merge close vertices
                static float s_mergeEpsilon = 0.001f;
                ImGui::Separator();
                ImGui::Text("Merge Vertices");
                ImGui::SetNextItemWidth(input_width);
                ImGui::DragFloat("Distance##merge", &s_mergeEpsilon, 0.0001f, 0.0f, 1.0f, "%.5f");
                if (ImGui::Button("Merge Close Vertices"))
                {
                    MergeCloseVertices(*mesh, s_mergeEpsilon);
                    ApplyEditableMeshToInstance(selectedInstance);
                }

                // Smoothing
                static int s_smoothIterations = 1;
                static float s_smoothFactor = 0.5f;
                ImGui::Separator();
                ImGui::Text("Smoothing");
                ImGui::SetNextItemWidth(input_width);
                ImGui::SliderInt("Iterations##smooth", &s_smoothIterations, 1, 10);
                ImGui::SetNextItemWidth(input_width);
                ImGui::SliderFloat("Factor##smooth", &s_smoothFactor, 0.01f, 1.0f);
                if (ImGui::Button("Smooth Mesh"))
                {
					SubdivideOnce(*mesh); // Subdivide once to add vertices for smoothing
                    SmoothMesh(*mesh, s_smoothIterations, s_smoothFactor);
                    ApplyEditableMeshToInstance(selectedInstance);
                }
            }

            // Object-level morphing between two instances (transforms & color).
            ImGui::Separator();
            ImGui::Text("Morph To Other Object");
            // Build list of candidate instances (exclude lights and self).
            std::vector<Instance*> morphCandidates;
            morphCandidates.reserve(instances.size());
            for (Instance* inst : instances)
            {
                if (!inst || inst == selectedInstance) continue;
                if (inst->isLight) continue;
                morphCandidates.push_back(inst);
            }

            static int s_morphIndex = -1;
            if (!morphCandidates.empty())
            {
                // Clamp stored index if candidate list shrank.
                if (s_morphIndex >= (int)morphCandidates.size())
                    s_morphIndex = (int)morphCandidates.size() - 1;

                std::vector<const char*> names;
                names.reserve(morphCandidates.size());
                for (Instance* inst : morphCandidates)
                    names.push_back(inst->name.c_str());

                ImGui::SetNextItemWidth(input_width);
                ImGui::Combo("Target##morph", &s_morphIndex,
                    names.data(), (int)names.size());

                static float s_morphT = 0.0f;
                ImGui::SetNextItemWidth(input_width);
                ImGui::SliderFloat("Amount##morph", &s_morphT, 0.0f, 1.0f);

                if (s_morphIndex >= 0 && s_morphIndex < (int)morphCandidates.size())
                {
                    Instance* target = morphCandidates[s_morphIndex];
                    if (target)
                    {
                        float t = s_morphT;
                        // Simple linear interpolation of transform and color.
                        for (int i = 0; i < 3; ++i)
                        {
                            selectedInstance->position[i] =
                                selectedInstance->position[i] * (1.0f - t) + target->position[i] * t;
                            selectedInstance->rotation[i] =
                                selectedInstance->rotation[i] * (1.0f - t) + target->rotation[i] * t;
                            selectedInstance->scale[i] =
                                selectedInstance->scale[i] * (1.0f - t) + target->scale[i] * t;
                        }
                        for (int i = 0; i < 4; ++i)
                        {
                            selectedInstance->objectColor[i] =
                                selectedInstance->objectColor[i] * (1.0f - t) + target->objectColor[i] * t;
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No other non-light objects available to morph to.");
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete Object"))
    {
        if (selectedInstance->parent)
        {
            auto it = std::find(selectedInstance->parent->children.begin(), selectedInstance->parent->children.end(), selectedInstance);
            if (it != selectedInstance->parent->children.end())
                gCmdManager.executeCommand(std::make_unique<DeleteInstanceCommand>(selectedInstance, selectedInstance->parent, std::distance(selectedInstance->parent->children.begin(), it)));
        }
        else
        {
            auto it = std::find(instances.begin(), instances.end(), selectedInstance);
            if (it != instances.end())
                gCmdManager.executeCommand(std::make_unique<DeleteInstanceCommand>(selectedInstance, &instances, std::distance(instances.begin(), it)));
        }
        selectedInstance = nullptr;
    }
    bool highlighted = highlightVisible;
    if (ImGui::Checkbox("Show highlight tint", &highlighted)) highlightVisible = highlighted;
}

int main(void)
{
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    GLFWwindow* window = glfwCreateWindow(WNDW_WIDTH, WNDW_HEIGHT, "AnitoScan", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Initialize BGFX
    bgfx::renderFrame();

    bgfx::Init bgfxinit;
    bgfxinit.type = bgfx::RendererType::OpenGL;
    bgfxinit.resolution.width = WNDW_WIDTH;
    bgfxinit.resolution.height = WNDW_HEIGHT;
    bgfxinit.resolution.reset = BGFX_RESET_VSYNC;
    bgfxinit.platformData.nwh = glfwGetWin32Window(window);
    if (!bgfx::init(bgfxinit)) {
        std::cerr << "Failed to initialize BGFX" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    //Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetupLichtFeldLikeStyle();                // <<< new

    // Load saved window visibility state (if any)
    LoadWindowVisibilityConfig("window_visibility.cfg");
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = "imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiWindowFlags window_flags = 0;
    // Base flags for tool windows: no collapse, not movable (LichtFeld-like fixed layout), but resizable.
    window_flags |= ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.Fonts->AddFontFromFileTTF("fonts/SNPro-Bold.ttf", 16.0f);
    ImFont* fontSmall = io.Fonts->AddFontFromFileTTF("fonts/SNPro-Bold.ttf", 28.0f);
    ImFont* fontMedium = io.Fonts->AddFontFromFileTTF("fonts/SNPro-Bold.ttf", 46.0f); 
    ImFont* fontLarge = io.Fonts->AddFontFromFileTTF("fonts/SNPro-Bold.ttf", 64.0f); // Baked at high res
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_Implbgfx_Init(255);

    glfwSetKeyCallback(window, glfw_keyCallback);

//    Gallery::LoadGallery("./screenshots");

    /*ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));*/


    // Load shaders
    // Create an off-screen picking render target (color)
    s_pickingRT = bgfx::createTexture2D(PICKING_DIM, PICKING_DIM, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
    );
    // Create a depth texture for the picking RT.
    s_pickingRTDepth = bgfx::createTexture2D(PICKING_DIM, PICKING_DIM, false, 1,
        bgfx::TextureFormat::D32F,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
    );
    bgfx::TextureHandle rt[2] =
    {
        s_pickingRT,
        s_pickingRTDepth
    };
    // Create a framebuffer using both the color and depth textures.
    s_pickingFB = bgfx::createFrameBuffer(BX_COUNTOF(rt), rt, true);

    // Create a CPU-readback texture for blitting.
    s_pickingReadTex = bgfx::createTexture2D(PICKING_DIM, PICKING_DIM, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
    );

    // Create the uniform for picking â€“ this will be set per-object.
    u_id = bgfx::createUniform("u_id", bgfx::UniformType::Vec4);

    // Load the picking shader program.
    // (Assumes you have compiled picking shaders "vs_picking_shaded.bin" and "fs_picking_id.bin")
    bgfx::ShaderHandle vsPick = loadShader("shaders\\vs_picking_shaded.bin");
    bgfx::ShaderHandle fsPick = loadShader("shaders\\fs_picking_id.bin");
    pickingProgram = bgfx::createProgram(vsPick, fsPick, true);

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float) // NEW: UV coordinates
        .end();

    //plane generation
    bgfx::VertexBufferHandle vbh_plane = bgfx::createVertexBuffer(
        bgfx::makeRef(planeVertices, sizeof(planeVertices)),
        layout
    );

    bgfx::IndexBufferHandle ibh_plane = bgfx::createIndexBuffer(
        bgfx::makeRef(planeIndices, sizeof(planeIndices))
    );

    //cube generation
    bgfx::VertexBufferHandle vbh_cube = bgfx::createVertexBuffer(
        bgfx::makeRef(cubeVertices, sizeof(cubeVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_cube = bgfx::createIndexBuffer(
        bgfx::makeRef(cubeIndices, sizeof(cubeIndices))
    );

    //capsule generation
    std::vector<PosColorVertex> capsuleVertices;
    std::vector<uint16_t> capsuleIndices;

    generateCapsule(1.0f, 1.5f, 20, 20, capsuleVertices, capsuleIndices);

    bgfx::VertexBufferHandle vbh_capsule = bgfx::createVertexBuffer(
        bgfx::makeRef(capsuleVertices.data(), sizeof(PosColorVertex) * capsuleVertices.size()),
        layout
    );

    bgfx::IndexBufferHandle ibh_capsule = bgfx::createIndexBuffer(
        bgfx::makeRef(capsuleIndices.data(), sizeof(uint16_t) * capsuleIndices.size())
    );

    std::vector<PosColorVertex> cylinderVertices;
    std::vector<uint16_t> cylinderIndices;

    generateCylinder(1.0f, 2.0f, 20, cylinderVertices, cylinderIndices);

    //cylinder generation
    bgfx::VertexBufferHandle vbh_cylinder = bgfx::createVertexBuffer(
        bgfx::makeRef(cylinderVertices.data(), sizeof(PosColorVertex) * cylinderVertices.size()),
        layout
    );

    bgfx::IndexBufferHandle ibh_cylinder = bgfx::createIndexBuffer(
        bgfx::makeRef(cylinderIndices.data(), sizeof(uint16_t) * cylinderIndices.size())
    );

    // Cone generation
    std::vector<PosColorVertex> coneVertices;
    std::vector<uint16_t> coneIndices;

    generateCone(1.0f, 2.0f, 20, coneVertices, coneIndices);

    bgfx::VertexBufferHandle vbh_cone = bgfx::createVertexBuffer(
        bgfx::makeRef(coneVertices.data(), sizeof(PosColorVertex) * coneVertices.size()),
        layout
    );

    bgfx::IndexBufferHandle ibh_cone = bgfx::createIndexBuffer(
        bgfx::makeRef(coneIndices.data(), sizeof(uint16_t) * coneIndices.size())
    );


    //sphere generation
    std::vector<PosColorVertex> sphereVertices;
    std::vector<uint16_t> sphereIndices;

    generateSphere(1.0f, 20, 20, sphereVertices, sphereIndices);

    bgfx::VertexBufferHandle vbh_sphere = bgfx::createVertexBuffer(
        bgfx::makeRef(sphereVertices.data(), sizeof(PosColorVertex) * sphereVertices.size()),
        layout
    );

    bgfx::IndexBufferHandle ibh_sphere = bgfx::createIndexBuffer(
        bgfx::makeRef(sphereIndices.data(), sizeof(uint16_t) * sphereIndices.size())
    );

    g_vbh_sphere = vbh_sphere;
    g_ibh_sphere = ibh_sphere;
    g_vbh_cone = vbh_cone;
    g_ibh_cone = ibh_cone;

    // Register base mesh templates for primitives and static meshes.
    g_BaseMeshData["plane"]   = MakeMeshDataFromArrays(planeVertices, planeIndices);
    g_BaseMeshData["cube"]    = MakeMeshDataFromArrays(cubeVertices, cubeIndices);
    g_BaseMeshData["capsule"] = MakeMeshDataFromVectors(capsuleVertices, capsuleIndices);
    g_BaseMeshData["cylinder"]= MakeMeshDataFromVectors(cylinderVertices, cylinderIndices);
    g_BaseMeshData["cone"]    = MakeMeshDataFromVectors(coneVertices, coneIndices);
    g_BaseMeshData["sphere"]  = MakeMeshDataFromVectors(sphereVertices, sphereIndices);

    //cornell box generation
    bgfx::VertexBufferHandle vbh_cornell = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxVertices, sizeof(cornellBoxVertices)),
        layout
    );

    bgfx::IndexBufferHandle ibh_cornell = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxIndices, sizeof(cornellBoxIndices))
    );

    bgfx::VertexBufferHandle vbh_innerCube = bgfx::createVertexBuffer(
        bgfx::makeRef(innerCubeVertices, sizeof(innerCubeVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_innerCube = bgfx::createIndexBuffer(
        bgfx::makeRef(innerCubeIndices, sizeof(innerCubeIndices))
    );
    bgfx::VertexBufferHandle vbh_floor = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxFloorVertices, sizeof(cornellBoxFloorVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_floor = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxFloorIndices, sizeof(cornellBoxFloorIndices))
    );
    bgfx::VertexBufferHandle vbh_ceiling = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxCeilingVertices, sizeof(cornellBoxCeilingVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_ceiling = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxCeilingIndices, sizeof(cornellBoxCeilingIndices))
    );
    bgfx::VertexBufferHandle vbh_back = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxBackVertices, sizeof(cornellBoxBackVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_back = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxBackIndices, sizeof(cornellBoxBackIndices))
    );
    bgfx::VertexBufferHandle vbh_left = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxLeftVertices, sizeof(cornellBoxLeftVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_left = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxLeftIndices, sizeof(cornellBoxLeftIndices))
    );
    bgfx::VertexBufferHandle vbh_right = bgfx::createVertexBuffer(
        bgfx::makeRef(cornellBoxRightVertices, sizeof(cornellBoxRightVertices)),
        layout
    );
    bgfx::IndexBufferHandle ibh_right = bgfx::createIndexBuffer(
        bgfx::makeRef(cornellBoxRightIndices, sizeof(cornellBoxRightIndices))
    );

    g_BaseMeshData["cornell_box"] = MakeMeshDataFromArrays(cornellBoxVertices, cornellBoxIndices);
    g_BaseMeshData["innerCube"]   = MakeMeshDataFromArrays(innerCubeVertices, innerCubeIndices);
    g_BaseMeshData["floor"]       = MakeMeshDataFromArrays(cornellBoxFloorVertices, cornellBoxFloorIndices);
    g_BaseMeshData["ceiling"]     = MakeMeshDataFromArrays(cornellBoxCeilingVertices, cornellBoxCeilingIndices);
    g_BaseMeshData["back"]        = MakeMeshDataFromArrays(cornellBoxBackVertices, cornellBoxBackIndices);
    g_BaseMeshData["left"]        = MakeMeshDataFromArrays(cornellBoxLeftVertices, cornellBoxLeftIndices);
    g_BaseMeshData["right"]       = MakeMeshDataFromArrays(cornellBoxRightVertices, cornellBoxRightIndices);

    //arrow primitive
    bgfx::IndexBufferHandle ibh_arrow = bgfx::createIndexBuffer(
        bgfx::makeRef(arrowIndices, sizeof(arrowIndices))
    );
    bgfx::VertexBufferHandle vbh_arrow = bgfx::createVertexBuffer(
        bgfx::makeRef(arrowVertices, sizeof(arrowVertices)),
        layout
    );
    g_BaseMeshData["arrow"] = MakeMeshDataFromArrays(arrowVertices, arrowIndices);

    //mesh generation
    MeshData meshData = loadMesh2("meshes/suzanne.obj");
    bgfx::VertexBufferHandle vbh_mesh;
    bgfx::IndexBufferHandle ibh_mesh;
    createMeshBuffers(meshData, vbh_mesh, ibh_mesh);

    //teapot generation
    MeshData teapotData = loadMesh("meshes/teapot.obj");
    bgfx::VertexBufferHandle vbh_teapot;
    bgfx::IndexBufferHandle ibh_teapot;
    createMeshBuffers(teapotData, vbh_teapot, ibh_teapot);

    //stanford bunny generation
    MeshData bunnyData = loadMesh("meshes/bunny.obj");
    bgfx::VertexBufferHandle vbh_bunny;
    bgfx::IndexBufferHandle ibh_bunny;
    createMeshBuffers(bunnyData, vbh_bunny, ibh_bunny);

    //lucy generation
    MeshData lucyData = loadMesh("meshes/lucy.obj");
    bgfx::VertexBufferHandle vbh_lucy;
    bgfx::IndexBufferHandle ibh_lucy;
    createMeshBuffers(lucyData, vbh_lucy, ibh_lucy);

    g_BaseMeshData["mesh"]   = meshData;
    g_BaseMeshData["teapot"] = teapotData;
    g_BaseMeshData["bunny"]  = bunnyData;
    g_BaseMeshData["lucy"]   = lucyData;

    //comic border generation
    MeshData comicborder = loadMesh("comic elements/comicborder.obj");
    bgfx::VertexBufferHandle vbh_comicborder;
    bgfx::IndexBufferHandle ibh_comicborder;
    createMeshBuffers(comicborder, vbh_comicborder, ibh_comicborder);

    //comic bubble object 1
    MeshData comicbubble1 = loadMesh("comic elements/comicbubble1_right.obj");
    bgfx::VertexBufferHandle vbh_comicbubble1;
    bgfx::IndexBufferHandle ibh_comicbubble1;
    createMeshBuffers(comicbubble1, vbh_comicbubble1, ibh_comicbubble1);

    //comic bubble object 2
    MeshData comicbubble2 = loadMesh("comic elements/comicbubble2_right.obj");
    bgfx::VertexBufferHandle vbh_comicbubble2;
    bgfx::IndexBufferHandle ibh_comicbubble2;
    createMeshBuffers(comicbubble2, vbh_comicbubble2, ibh_comicbubble2);

    //comic bubble object 3
    MeshData comicbubble3 = loadMesh("comic elements/comicbubble3_right.obj");
    bgfx::VertexBufferHandle vbh_comicbubble3;
    bgfx::IndexBufferHandle ibh_comicbubble3;
    createMeshBuffers(comicbubble3, vbh_comicbubble3, ibh_comicbubble3);

    //comic bubble object 4
    MeshData comicbubble4 = loadMesh("comic elements/comicbubble4_left.obj");
    bgfx::VertexBufferHandle vbh_comicbubble4;
    bgfx::IndexBufferHandle ibh_comicbubble4;
    createMeshBuffers(comicbubble4, vbh_comicbubble4, ibh_comicbubble4);

    //comic bubble object 5
    MeshData comicbubble5 = loadMesh("comic elements/comicbubble5_left.obj");
    bgfx::VertexBufferHandle vbh_comicbubble5;
    bgfx::IndexBufferHandle ibh_comicbubble5;
    createMeshBuffers(comicbubble5, vbh_comicbubble5, ibh_comicbubble5);

    //comic bubble object 6
    MeshData comicbubble6 = loadMesh("comic elements/comicbubble6_left.obj");
    bgfx::VertexBufferHandle vbh_comicbubble6;
    bgfx::IndexBufferHandle ibh_comicbubble6;
    createMeshBuffers(comicbubble6, vbh_comicbubble6, ibh_comicbubble6);

    //comic bubble object 7
    MeshData comicbubble7 = loadMesh("comic elements/comicbubble7_middle.obj");
    bgfx::VertexBufferHandle vbh_comicbubble7;
    bgfx::IndexBufferHandle ibh_comicbubble7;
    createMeshBuffers(comicbubble7, vbh_comicbubble7, ibh_comicbubble7);

    //comic bubble object 8
    MeshData comicbubble8 = loadMesh("comic elements/comicbubble8_middle.obj");
    bgfx::VertexBufferHandle vbh_comicbubble8;
    bgfx::IndexBufferHandle ibh_comicbubble8;
    createMeshBuffers(comicbubble8, vbh_comicbubble8, ibh_comicbubble8);

    g_BaseMeshData["comicborder"] = comicborder;
    g_BaseMeshData["comicbubble1"] = comicbubble1;
    g_BaseMeshData["comicbubble2"] = comicbubble2;
    g_BaseMeshData["comicbubble3"] = comicbubble3;
    g_BaseMeshData["comicbubble4"] = comicbubble4;
    g_BaseMeshData["comicbubble5"] = comicbubble5;
    g_BaseMeshData["comicbubble6"] = comicbubble6;
    g_BaseMeshData["comicbubble7"] = comicbubble7;
    g_BaseMeshData["comicbubble8"] = comicbubble8;

    // simple quad for text rendering
    bgfx::VertexBufferHandle vbh_textQuad = bgfx::createVertexBuffer(
        bgfx::copy(textQuadVertices, sizeof(textQuadVertices)),
        layout  // reuse your existing vertex layout (make sure it includes TexCoord0)
    );
    bgfx::IndexBufferHandle ibh_textQuad = bgfx::createIndexBuffer(
        bgfx::copy(textQuadIndices, sizeof(textQuadIndices))
    );

    g_BaseMeshData["text"] = MakeMeshDataFromArrays(textQuadVertices, textQuadIndices);

    std::unordered_map<std::string, std::pair<bgfx::VertexBufferHandle, bgfx::IndexBufferHandle>> bufferMap;

    bufferMap["cube"] = { vbh_cube, ibh_cube };
    bufferMap["capsule"] = { vbh_capsule, ibh_capsule };
    bufferMap["cylinder"] = { vbh_cylinder, ibh_cylinder };
    bufferMap["sphere"] = { vbh_sphere, ibh_sphere };
    bufferMap["plane"] = { vbh_plane, ibh_plane };
    bufferMap["cornell_box"] = { vbh_cornell, ibh_cornell };
    bufferMap["innerCube"] = { vbh_innerCube, ibh_innerCube };
    bufferMap["floor"] = { vbh_floor, ibh_floor };
    bufferMap["ceiling"] = { vbh_ceiling, ibh_ceiling };
    bufferMap["back"] = { vbh_back, ibh_back };
    bufferMap["left"] = { vbh_left, ibh_left };
    bufferMap["right"] = { vbh_right, ibh_right };
    bufferMap["mesh"] = { vbh_mesh, ibh_mesh };
    bufferMap["teapot"] = { vbh_teapot, ibh_teapot };
    bufferMap["bunny"] = { vbh_bunny, ibh_bunny };
    bufferMap["lucy"] = { vbh_lucy, ibh_lucy };
    bufferMap["light"] = { vbh_sphere, ibh_sphere };
    bufferMap["cone"] = { vbh_cone, ibh_cone };
    bufferMap["arrow"] = { vbh_arrow, ibh_arrow };
    bufferMap["empty"] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bufferMap["text"] = { vbh_textQuad, ibh_textQuad };
    bufferMap["comicborder"] = { vbh_comicborder, ibh_comicborder };
    bufferMap["comicbubble1"] = { vbh_comicbubble1, ibh_comicbubble1 };
    bufferMap["comicbubble2"] = { vbh_comicbubble2, ibh_comicbubble2 };
    bufferMap["comicbubble3"] = { vbh_comicbubble3, ibh_comicbubble3 };
    bufferMap["comicbubble4"] = { vbh_comicbubble4, ibh_comicbubble4 };
    bufferMap["comicbubble5"] = { vbh_comicbubble5, ibh_comicbubble5 };
    bufferMap["comicbubble6"] = { vbh_comicbubble6, ibh_comicbubble6 };
    bufferMap["comicbubble7"] = { vbh_comicbubble7, ibh_comicbubble7 };
    bufferMap["comicbubble8"] = { vbh_comicbubble8, ibh_comicbubble8 };

    std::unordered_map<std::string, std::string> importedObjMap;

    //Enable debug output
    bgfx::setDebug(BGFX_DEBUG_TEXT); // <-- Add this line here

    bgfx::setViewRect(0, 0, 0, WNDW_WIDTH, WNDW_HEIGHT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, g_AppBackgroundColor, 1.0f, 0);

    InputManager::initialize(window);

    //declare camera instance

    Camera camera;
    cameras.push_back(camera);
    std::vector<Instance*> instances;
    instances.reserve(100);
    // Expose the instances vector to UI helpers such as the fixed right sidebar.
    g_Instances = &instances;

    // Set up Reconstructor import callback
    Reconstructor::SetImportCallback([&instances, &importedObjMap](const std::string& objPath) {
        std::cout << "[Editor] Auto-importing mesh: " << objPath << std::endl;
        
        // Check if path is absolute or relative
        fs::path pathObj(objPath);
        std::string absPath;
        if (pathObj.is_absolute()) {
            absPath = objPath;
        } else {
            // Convert relative path to absolute
            absPath = (fs::current_path() / pathObj).string();
        }
        
        // Normalize path separators
        std::string normalizedAbsPath = ConvertBackslashesToForward(absPath);
        std::cout << "[DEBUG Import Callback] Loading mesh from absolute path: " << normalizedAbsPath << std::endl;
        
        // Check if file exists
        if (!fs::exists(absPath)) {
            std::cerr << "[Editor] ERROR: Auto-import file does not exist: " << absPath << std::endl;
            return;
        }
        
        // Load all meshes using the same logic as the Import OBJ menu
        // Pass absolute path to Assimp
        std::vector<ImportedMesh> importedMeshes = loadImportedMeshes(normalizedAbsPath);
        
        // Get relative path for scene file storage
        std::string relPath = GetRelativePath(absPath);
        std::string normalizedRelPath = ConvertBackslashesToForward(relPath);
        
        std::cout << "[DEBUG Import Callback] Loaded " << importedMeshes.size() << " meshes" << std::endl;
        std::string fileName = fs::path(normalizedRelPath).stem().string();
        
        if (!importedMeshes.empty()) {
            // Compute overall group center
            aiVector3D groupCenter(0.0f, 0.0f, 0.0f);
            for (const auto& impMesh : importedMeshes) {
                groupCenter.x += impMesh.transform.a4;
                groupCenter.y += impMesh.transform.b4;
                groupCenter.z += impMesh.transform.c4;
            }
            groupCenter.x /= importedMeshes.size();
            groupCenter.y /= importedMeshes.size();
            groupCenter.z /= importedMeshes.size();
            
            // Create an empty parent instance at the overall group center
            Instance* parentInstance = new Instance(instanceCounter++, fileName + "_group", "empty",
                groupCenter.x, groupCenter.y, groupCenter.z,
                BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
            instances.push_back(parentInstance);
            
            // For each imported mesh, create buffers and spawn a child instance
            for (size_t i = 0; i < importedMeshes.size(); ++i) {
                bgfx::VertexBufferHandle vbh_imported;
                bgfx::IndexBufferHandle ibh_imported;
                createMeshBuffers(importedMeshes[i].meshData, vbh_imported, ibh_imported);
                
                Instance* childInst = new Instance(instanceCounter++, fileName + "_" + std::to_string(i),
                    fileName, 0.0f, 0.0f, 0.0f,
                    vbh_imported, ibh_imported);
                childInst->meshNumber = i;
            // Cache editable mesh data for geometry tools.
            g_InstanceMeshData[childInst->id] = importedMeshes[i].meshData;
                
                // Decompose the imported mesh's transform
                aiVector3D scaling, position;
                aiQuaternion rotation;
                importedMeshes[i].transform.Decompose(scaling, rotation, position);
                
                // Set the child's position relative to the parent
                childInst->position[0] = position.x - groupCenter.x;
                childInst->position[1] = position.y - groupCenter.y;
                childInst->position[2] = position.z - groupCenter.z;
                childInst->rotation[0] = childInst->rotation[1] = childInst->rotation[2] = 0.0f;
                childInst->scale[0] = scaling.x;
                childInst->scale[1] = scaling.y;
                childInst->scale[2] = scaling.z;
                
                // Assign the diffuse texture from the imported mesh
                childInst->diffuseTexture = importedMeshes[i].diffuseTexture;
                
                // Apply diffuse color if present and no texture
                if (importedMeshes[i].hasDiffuseColor && !bgfx::isValid(childInst->diffuseTexture)) {
                    childInst->objectColor[0] = importedMeshes[i].diffuseColor[0];
                    childInst->objectColor[1] = importedMeshes[i].diffuseColor[1];
                    childInst->objectColor[2] = importedMeshes[i].diffuseColor[2];
                    childInst->objectColor[3] = importedMeshes[i].diffuseColor[3];
                }
                
                // Add this mesh as a child of the empty parent
                parentInstance->addChild(childInst);
            }
            
            std::cout << "[Editor] Auto-imported OBJ with " << importedMeshes.size()
                << " mesh(es) grouped under " << fileName << "_group" << std::endl;
            importedObjMap[fileName] = normalizedRelPath;
        }
    });
    
    // Set the main window reference for Reconstructor
    Reconstructor::SetMainWindow(window);

    bool modelMovement = false;

    float lightColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    float lightDir[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    float scale[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    int spawnPrimitive = 0;

    bool* p_open = NULL;

    u_diffuseTex = bgfx::createUniform("u_diffuseTex", bgfx::UniformType::Sampler);

    // Create uniforms (do this once)
    u_lights = bgfx::createUniform("u_lights", bgfx::UniformType::Vec4, MAX_LIGHTS * 4);
    u_numLights = bgfx::createUniform("u_numLights", bgfx::UniformType::Vec4);

    u_viewPos = bgfx::createUniform("u_viewPos", bgfx::UniformType::Vec4);

    bgfx::UniformHandle u_inkColor = bgfx::createUniform("u_inkColor", bgfx::UniformType::Vec4);
    bgfx::UniformHandle u_cameraPos = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
    bgfx::UniformHandle u_e = bgfx::createUniform("u_e", bgfx::UniformType::Vec4);
    bgfx::UniformHandle u_noiseTex = bgfx::createUniform("u_noiseTex", bgfx::UniformType::Sampler);
    bgfx::UniformHandle u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    bgfx::UniformHandle u_objectColor = bgfx::createUniform("u_objectColor", bgfx::UniformType::Vec4);
    bgfx::UniformHandle u_tint = bgfx::createUniform("u_tint", bgfx::UniformType::Vec4);

    // Create a uniform for extra parameters as a vec4.
    bgfx::UniformHandle u_extraParams = bgfx::createUniform("u_extraParams", bgfx::UniformType::Vec4);

    // Create a uniform for extra parameters as a vec4.
    bgfx::UniformHandle u_paramsLayer = bgfx::createUniform("u_paramsLayer", bgfx::UniformType::Vec4);

    // -------------- For texture/material use and preview --------------
    u_uvTransform = bgfx::createUniform("u_uvTransform", bgfx::UniformType::Vec4);
    u_albedoFactor = bgfx::createUniform("u_albedoFactor", bgfx::UniformType::Vec4);

    // For comic border and bubble change of colors
    bgfx::UniformHandle u_comicColor = bgfx::createUniform("u_comicColor", bgfx::UniformType::Vec4);

    static bgfx::TextureHandle logoTexture = BGFX_INVALID_HANDLE;
    if (!bgfx::isValid(logoTexture)) {
        logoTexture = loadTextureFile("assets/anitocrosshatch.png");  // adjust path
    }
    ImTextureID logoID = (ImTextureID)(uintptr_t)logoTexture.idx;

    // SHADER NOISE TEXTURE
    //bgfx::TextureHandle noiseTexture = loadTextureDDS("shaders\\noise1.dds");
    {
        TextureOption noiseTex;

        noiseTex.name = "Noise1(Default)";
        noiseTex.handle = loadTextureDDS("noise textures\\noise1.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise2";
        noiseTex.handle = loadTextureDDS("noise textures\\noise2.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise3";
        noiseTex.handle = loadTextureDDS("noise textures\\noise3.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise4";
        noiseTex.handle = loadTextureDDS("noise textures\\noise4.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise5";
        noiseTex.handle = loadTextureDDS("noise textures\\noise5.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise6";
        noiseTex.handle = loadTextureDDS("noise textures\\noise6.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise7";
        noiseTex.handle = loadTextureDDS("noise textures\\noise7.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise8";
        noiseTex.handle = loadTextureDDS("noise textures\\noise8.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise9";
        noiseTex.handle = loadTextureDDS("noise textures\\noise9.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise10";
        noiseTex.handle = loadTextureDDS("noise textures\\noise10.dds");
        availableNoiseTextures.push_back(noiseTex);

        noiseTex.name = "Noise11";
        noiseTex.handle = loadTextureDDS("noise textures\\noise11.dds");
        availableNoiseTextures.push_back(noiseTex);
    }

    noiseTexture = availableNoiseTextures[0].handle;

    // Texture
    std::vector<TextureOption> availableTextures;
    {
        TextureOption tex;

        // Asphalt 1
        tex.name = "Asphalt1";
        tex.handle = loadTextureDDS("textures\\Asphalt 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Bark 1
        tex.name = "Bark1";
        tex.handle = loadTextureDDS("textures\\Bark 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Brick 1
        tex.name = "Brick1";
        tex.handle = loadTextureDDS("textures\\Brick 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Carpet 1
        tex.name = "Carpet1";
        tex.handle = loadTextureDDS("textures\\Carpet 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Cobblestone 1
        tex.name = "Cobblestone1";
        tex.handle = loadTextureDDS("textures\\Cobblestone 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Concrete 1
        tex.name = "Concrete1";
        tex.handle = loadTextureDDS("textures\\Concrete 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Dirt 1
        tex.name = "Dirt1";
        tex.handle = loadTextureDDS("textures\\Dirt 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Fabric 1
        tex.name = "Fabric1";
        tex.handle = loadTextureDDS("textures\\Fabric 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Food 1
        tex.name = "Food1";
        tex.handle = loadTextureDDS("textures\\Food 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Glass 1
        tex.name = "Glass1";
        tex.handle = loadTextureDDS("textures\\Glass 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Glass 2
        tex.name = "Glass2";
        tex.handle = loadTextureDDS("textures\\Glass 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Grass 1
        tex.name = "Grass1";
        tex.handle = loadTextureDDS("textures\\Grass 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Grass 2
        tex.name = "Grass2";
        tex.handle = loadTextureDDS("textures\\Grass 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Leaves 1
        tex.name = "Leaves1";
        tex.handle = loadTextureDDS("textures\\Leaves 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Metal 1
        tex.name = "Metal10";
        tex.handle = loadTextureDDS("textures\\Metal 10.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Paint 1
        tex.name = "Paint1";
        tex.handle = loadTextureDDS("textures\\Paint 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Rock 1
        tex.name = "Rock1";
        tex.handle = loadTextureDDS("textures\\Rocks 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Shingles 1
        tex.name = "Shingles1";
        tex.handle = loadTextureDDS("textures\\Shingles 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Snow 1
        tex.name = "Snow1";
        tex.handle = loadTextureDDS("textures\\Snow 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Stone 1
        tex.name = "Stone1";
        tex.handle = loadTextureDDS("textures\\Stone 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Stone 2
        tex.name = "Stone2";
        tex.handle = loadTextureDDS("textures\\Stone 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Tile 1
        tex.name = "Tile1";
        tex.handle = loadTextureDDS("textures\\Tile 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Tile 2
        tex.name = "Tile2";
        tex.handle = loadTextureDDS("textures\\Tile 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Tile 3
        tex.name = "Tile3";
        tex.handle = loadTextureDDS("textures\\Tile 3.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Wood 1
        tex.name = "Wood1";
        tex.handle = loadTextureDDS("textures\\Wood 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Wood 2
        tex.name = "Wood2";
        tex.handle = loadTextureDDS("textures\\Wood 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Wood 3
        tex.name = "Wood3";
        tex.handle = loadTextureDDS("textures\\Wood 3.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Wooden Boards 1
        tex.name = "Wooden Boards1";
        tex.handle = loadTextureDDS("textures\\Wooden Boards 1.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Wooden Boards 2
        tex.name = "Wooden Boards2";
        tex.handle = loadTextureDDS("textures\\Wooden Boards 2.dds");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);

        // Table 1
        tex.name = "Table 1";
        tex.handle = loadTextureFile("textures\\table1.png");
        std::cout << "Loaded texture '" << tex.name << "' with handle: " << tex.handle.idx << std::endl;
        availableTextures.push_back(tex);
    }
    g_availableTextures = &availableTextures;

    //plane
    bgfx::TextureHandle planeTexture = loadTextureDDS("shaders\\texture2.dds");
    // Create a default white texture once (static variable)
    static bgfx::TextureHandle defaultWhiteTexture = BGFX_INVALID_HANDLE;
    if (defaultWhiteTexture.idx == bgfx::kInvalidHandle)
    {
        const uint32_t whitePixel = 0xffffffff;
        const bgfx::Memory* mem = bgfx::copy(&whitePixel, sizeof(whitePixel));
        defaultWhiteTexture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8, 0, mem);
    }

    // Load the comic element shaders.
    bgfx::ShaderHandle vsh_comic = loadShader("shaders\\v_comic.bin");
    bgfx::ShaderHandle fsh_comic = loadShader("shaders\\f_comic.bin");
    bgfx::ProgramHandle comicProgram = bgfx::createProgram(vsh_comic, fsh_comic, true);

    // Load dedicated text shader.
    bgfx::ShaderHandle vsh_text = loadShader("shaders\\v_text.bin");
    bgfx::ShaderHandle fsh_text = loadShader("shaders\\f_text.bin");
    bgfx::ProgramHandle textProgram = bgfx::createProgram(vsh_text, fsh_text, true);

    // Load shaders and create program once
    bgfx::ShaderHandle vsh = loadShader("shaders\\v_out21.bin");
    bgfx::ShaderHandle fsh = loadShader("shaders\\f_out28.bin");

    bgfx::ProgramHandle defaultProgram = bgfx::createProgram(vsh, fsh, true);

    // Load unlit vertex-color shaders for Attribute mode
    bgfx::ShaderHandle vsh_unlit = loadShader("shaders\\v_unlit_color.bin");
    bgfx::ShaderHandle fsh_unlit = loadShader("shaders\\f_unlit_color.bin");
    if (bgfx::isValid(vsh_unlit) && bgfx::isValid(fsh_unlit))
    {
        unlitColorProgram = bgfx::createProgram(vsh_unlit, fsh_unlit, true);
#ifdef _WIN32
        OutputDebugStringA("[AttributeMode] Unlit vertex-color program created.\n");
#endif
    }
    else
    {
#ifdef _WIN32
        OutputDebugStringA("[AttributeMode] Failed to load v_unlit_color.bin or f_unlit_color.bin.\n");
#endif
    }

    // Load the debug light shader:
    bgfx::ShaderHandle debugVsh = loadShader("shaders\\v_lightdebug_out1.bin");
    bgfx::ShaderHandle debugFsh = loadShader("shaders\\f_lightdebug_out1.bin");
    bgfx::ProgramHandle lightDebugProgram = bgfx::createProgram(debugVsh, debugFsh, true);

    //spawn plane
    spawnInstance(camera, "plane", "plane", vbh_plane, ibh_plane, instances);
    instances.back()->position[0] = 0.0f;
    instances.back()->position[1] = -4.0f;
    instances.back()->position[2] = 0.0f;

    // --- Spawn Cornell Box with Hierarchy ---
    //Instance* cornellBox = new Instance(instanceCounter++, "cornell_box", "empty", 8.0f, 0.0f, -5.0f, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    //// Create a walls node (dummy instance without geometry)
    //Instance* wallsNode = new Instance(instanceCounter++, "walls", "empty", 0.0f, -1.0f, 0.0f, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    //wallsNode->scale[0] = 0.2f;
    //wallsNode->scale[1] = 0.2f;
    //wallsNode->scale[2] = 0.2f;

    //Instance* floorPlane = new Instance(instanceCounter++, "floor", "plane", 0.0f, -6.0f, 0.0f, vbh_plane, ibh_plane);
    //wallsNode->addChild(floorPlane);
    //Instance* ceilingPlane = new Instance(instanceCounter++, "ceiling", "plane", 0.0f, 14.0f, 0.0f, vbh_plane, ibh_plane);
    //wallsNode->addChild(ceilingPlane);
    //Instance* backPlane = new Instance(instanceCounter++, "back", "plane", 0.0f, 4.0f, -10.0f, vbh_plane, ibh_plane);
    //backPlane->rotation[0] = 1.57f;
    //wallsNode->addChild(backPlane);
    //Instance* leftPlane = new Instance(instanceCounter++, "left_wall", "plane", 10.0f, 4.0f, 0.0f, vbh_plane, ibh_plane);
    //leftPlane->objectColor[0] = 1.0f; leftPlane->objectColor[1] = 0.0f; leftPlane->objectColor[2] = 0.0f; leftPlane->objectColor[3] = 1.0f;
    //leftPlane->rotation[2] = 1.57f;
    //wallsNode->addChild(leftPlane);
    //Instance* rightPlane = new Instance(instanceCounter++, "right_wall", "plane", -10.0f, 4.0f, 0.0f, vbh_plane, ibh_plane);
    //rightPlane->objectColor[0] = 0.0f; rightPlane->objectColor[1] = 1.0f; rightPlane->objectColor[2] = 0.0f; rightPlane->objectColor[3] = 1.0f;
    //rightPlane->rotation[2] = 1.57f;
    //wallsNode->addChild(rightPlane);

    //Instance* innerCube = new Instance(instanceCounter++, "inner_cube", "cube", 0.8f, -1.5f, 0.4f, vbh_cube, ibh_cube);
    //innerCube->rotation[1] = 0.2f;
    //innerCube->scale[0] = 0.6f;
    //innerCube->scale[1] = 0.6f;
    //innerCube->scale[2] = 0.6f;
    //Instance* innerRectBox = new Instance(instanceCounter++, "inner_rectbox", "cube", -1.0f, -0.7f, 0.4f, vbh_cube, ibh_cube);
    //innerRectBox->rotation[1] = -0.3f;
    //innerRectBox->scale[0] = 0.6f;
    //innerRectBox->scale[1] = 1.5f;
    //innerRectBox->scale[2] = 0.6f;
    //cornellBox->addChild(wallsNode);
    //cornellBox->addChild(innerCube);
    //cornellBox->addChild(innerRectBox);
    //instances.push_back(cornellBox);

    //spawnInstance(camera, "teapot", "teapot", vbh_teapot, ibh_teapot, instances);
    //instances.back()->position[0] = 3.0f;
    //instances.back()->position[1] = -1.0f;
    //instances.back()->position[2] = -5.0f;
    //instances.back()->scale[0] *= 0.03f;
    //instances.back()->scale[1] *= 0.03f;
    //instances.back()->scale[2] *= 0.03f;

    //spawnInstance(camera, "bunny", "bunny", vbh_bunny, ibh_bunny, instances);
    //instances.back()->position[0] = -1.0f;
    //instances.back()->position[1] = -1.0f;
    //instances.back()->position[2] = -5.0f;
    //instances.back()->scale[0] *= 10.0f;
    //instances.back()->scale[1] *= 10.0f;
    //instances.back()->scale[2] *= 10.0f;

    //spawnInstance(camera, "lucy", "lucy", vbh_lucy, ibh_lucy, instances);
    //instances.back()->position[0] = -4.0f;
    //instances.back()->position[1] = -1.0f;
    //instances.back()->position[2] = -5.0f;
    //instances.back()->scale[0] *= 0.01f;
    //instances.back()->scale[1] *= 0.01f;
    //instances.back()->scale[2] *= 0.01f;

    spawnLight(camera, vbh_sphere, ibh_sphere, vbh_cone, ibh_cone, instances);

    Logger::GetInstance();

    static bgfx::FrameBufferHandle g_frameBuffer = BGFX_INVALID_HANDLE;
    static bgfx::TextureHandle g_frameBufferTex = BGFX_INVALID_HANDLE;

    ImGuiWindowFlags menu_flags = 0;
    menu_flags |= ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBackground;
    /*menu_flags |= ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground;*/

    uint16_t screenshotWidth = 1920;
    uint16_t screenshotHeight = 1080;

    // Create a readable render target texture
    bgfx::TextureHandle screenshotColorTex = bgfx::createTexture2D(
        screenshotWidth,
        screenshotHeight,
        false, 1,
        bgfx::TextureFormat::BGRA8,
        BGFX_TEXTURE_RT
    );

    // Create a framebuffer with that texture
    bgfx::FrameBufferHandle screenshotFB = bgfx::createFrameBuffer(1, &screenshotColorTex, true);

    bool takingScreenshot = false;
    static char screenshotName[256] = "screenshot";
    static bool openScreenshotPopup = false;

    //MAIN LOOP
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        //static VideoPlayer videoPlayer;
        //static bool videoLoaded = false;
        //if (!videoLoaded)
        //{
        //    videoLoaded = videoPlayer.load("videos\\AnitoCrossHatchTrailer.mp4");
        //}
        static bool showMainMenu = true;
        static bool showCreditsPage = false;
        static bool showGallery = false;

        // OVERRIDE default blue tabs:
        ImGuiStyle& style = ImGui::GetStyle();
        // Inactive window title bar: #145C48
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f); // inactive
        // Active window title bar: #2ccb6f (green accent)
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // active
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f); // collapsed

        // Optional: Tabs
        style.Colors[ImGuiCol_Tab] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f); // unfocused
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // active
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); 

        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f);   // unfocused inactive
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // unfocused active

        // Button colors
        style.Colors[ImGuiCol_Button] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f); // inactive button
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // hovered
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // active

        // Header colors
        style.Colors[ImGuiCol_Header] = ImVec4(0.078f, 0.361f, 0.282f, 1.0f); // inactive header
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // hovered
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // active

        // Accent colors for interactive elements
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // green checkmark
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.173f, 0.796f, 0.435f, 1.0f); // green slider
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.200f, 1.0f, 0.5f, 1.0f); // brighter green when active

        ImGui_ImplGlfw_NewFrame();
        ImGui_Implbgfx_NewFrame();
        ImGui::NewFrame();

        if (selectedInstance && !ImGui::GetIO().WantCaptureKeyboard)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) {
                currentGizmoOperation = ImGuizmo::TRANSLATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_2)) {
                currentGizmoOperation = ImGuizmo::ROTATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_3)) {
                currentGizmoOperation = ImGuizmo::SCALE;
            }
            // Delete selected instance with Delete key (undoable)
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (selectedInstance)
                {
                    if (selectedInstance->parent)
                    {
                        Instance* parent = selectedInstance->parent;
                        auto it = std::find(parent->children.begin(), parent->children.end(), selectedInstance);
                        if (it != parent->children.end())
                        {
                            size_t idx = std::distance(parent->children.begin(), it);
                            gCmdManager.executeCommand(
                                std::make_unique<DeleteInstanceCommand>(selectedInstance, parent, idx)
                            );
                        }
                    }
                    else
                    {
                        auto it = std::find(instances.begin(), instances.end(), selectedInstance);
                        if (it != instances.end())
                        {
                            size_t idx = std::distance(instances.begin(), it);
                            gCmdManager.executeCommand(
                                std::make_unique<DeleteInstanceCommand>(selectedInstance, &instances, idx)
                            );
                        }
                    }
                    selectedInstance = nullptr;
                }
            }
        }
        if (showMainMenu)
        {
            //VIDEO BG
            //// Update the video frame each frame.
            //videoPlayer.update();
            //// Render the video background
            //{
            //    //ImGui_ImplGlfw_NewFrame();
            //    //ImGui_Implbgfx_NewFrame();
            //    //ImGui::NewFrame();

            //    // Create a full-screen window for the video background.
            //    // Use window flags to remove decorations and inputs.
            //    ImGui::SetNextWindowPos(ImVec2(0, 0));
            //    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            //    ImGui::Begin("Video Background", nullptr,
            //        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            //        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
            //    // Render the video texture to fill the background.
            //    ImGui::Image((ImTextureID)(uintptr_t)(videoPlayer.texture.idx), ImGui::GetIO().DisplaySize);
            //    ImGui::End();
            //}

            // Render the main menu overlay on top (if active)
            if (showMainMenu)
            {
                // Main menu window with semi-transparent background.
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.8f));

                //button colors
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.078f, 0.361f, 0.282f, 1.0f)); // Inactive
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.173f, 0.796f, 0.435f, 1.0f)); // Hovered
                ImGui::Begin("Main Menu", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
                ImGuiIO& io = ImGui::GetIO();
                ImVec2 displaySize = io.DisplaySize;
                ImGui::SetWindowPos(ImVec2(0, 0));
                ImGui::SetWindowSize(displaySize);

                // Center the title text
                float windowWidth = displaySize.x;
                float windowHeight = displaySize.y;

                // Logo size
                ImVec2 logoSize = ImVec2(150, 150); // adjust as needed

                // Center the logo above the title
                float logoX = (windowWidth - logoSize.x) * 0.5f;
                float logoY = (windowHeight * 0.3f) - (logoSize.y + 20); // 20px spacing

                // Set font scale before measuring
                //ImGui::SetWindowFontScale(3.0f);
				ImGui::PushFont(fontLarge);

                // Measure text at correct scale
                ImVec2 titleSize = ImGui::CalcTextSize("AnitoScan");

                // Center position
                float titleX = (windowWidth - titleSize.x) * 0.5f;
                float titleY = (windowHeight * 0.3f) - (titleSize.y * 0.5f);

                ImGui::SetCursorPos(ImVec2(titleX, titleY));
                ImGui::Text("AnitoScan");
                ImGui::PopFont();

                // Restore font scale
                //ImGui::SetWindowFontScale(2.0f);
                ImGui::PushFont(fontSmall);

                // Vertical spacing
                ImGui::Dummy(ImVec2(0, 50));

                float buttonWidth = 500.0f;
                float buttonHeight = 100.0f;
                float spacing = 10.0f;

                // Get window size
                ImVec2 windowSize = ImGui::GetWindowSize();

                // Total height of all buttons + spacing between them
                int buttonCount = 4;
                float totalHeight = buttonCount * buttonHeight + (buttonCount - 1) * spacing;

                // Start Y so buttons are vertically centered
                ImGui::SetCursorPosY((windowSize.y - totalHeight) * 0.70f);

                auto CenterButton = [&](const char* label) {
                    ImGui::SetCursorPosX((windowSize.x - buttonWidth) * 0.5f);
                    return ImGui::Button(label, ImVec2(buttonWidth, buttonHeight));
                    };

                if (CenterButton("Start")) {
                    showMainMenu = false;
                    showCreditsPage = false;
                    showGallery = false;
                }
                ImGui::Dummy(ImVec2(0.0f, spacing));

                // if (CenterButton("Gallery")) {
                //     showMainMenu = false;
                //     showCreditsPage = false;
                //     showGallery = true;
                // }
                // ImGui::Dummy(ImVec2(0.0f, spacing));

                if (CenterButton("Credits")) {
                    showCreditsPage = true;
                    showMainMenu = false;
                }
                ImGui::Dummy(ImVec2(0.0f, spacing));

                if (CenterButton("Exit")) {
                    glfwSetWindowShouldClose(window, true);
                }
                ImGui::PopFont();
                ImGui::End();
                //ImGui::PopStyleColor();
                ImGui::PopStyleColor(3); // Remove the 3 pushed colors

                //ImGui::Render();
                //ImGui_Implbgfx_RenderDrawLists(ImGui::GetDrawData());
            }
        }
        // Render the Credits page if active
        else if (showCreditsPage)
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

            // Opaque black background (no transparency)
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 10));

            ImGui::Begin("Credits Page", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoScrollbar);

            ImVec2 displaySize = ImGui::GetIO().DisplaySize;

            // Center child window that contains the actual credits content
            ImVec2 creditsWindowSize = ImVec2(500, 400);
            ImVec2 centerPos = ImVec2((displaySize.x - creditsWindowSize.x) * 0.5f,
                (displaySize.y - creditsWindowSize.y) * 0.5f);
            ImGui::SetCursorPos(centerPos);

            // Begin the visual container
            ImGui::BeginChild("CreditsBox", creditsWindowSize, true, ImGuiWindowFlags_NoScrollbar);

            // Title
            ImGui::SetCursorPosY(20);
            // ImGui::SetWindowFontScale(2.2f);
            ImGui::PushFont(fontMedium);
            ImVec2 titleSize = ImGui::CalcTextSize("CREDITS");
            ImGui::SetCursorPosX((creditsWindowSize.x - titleSize.x) * 0.5f);
            ImGui::Text("CREDITS");
            ImGui::PopFont();
            // ImGui::SetWindowFontScale(1.0f);

            ImGui::Dummy(ImVec2(0, 20));

            // Names and roles
            ImGui::Text("Developed by:");
            ImGui::Separator();
           /* DELA CRUZ, DIEGO J.
                KHAN, RENEE ALTHEA F.
                NGO, WAY WE P.
                SACDALAN, JUSTIN MORRIE E.*/

            ImGui::BulletText("SACDALAN, JUSTIN MORRIE E");
            ImGui::BulletText("DELA CRUZ, DIEGO J.");
            ImGui::BulletText("KHAN, RENEE ALTHEA F.");
            ImGui::BulletText("NGO, WAY WE P.");

            ImGui::Dummy(ImVec2(0, 30));

            // Centered Back button
            ImVec2 btnSize = ImVec2(120, 40);
            ImVec2 btnPos = ImVec2((creditsWindowSize.x - btnSize.x) * 0.5f,
                ImGui::GetCursorPosY());
            ImGui::SetCursorPos(btnPos);
            if (ImGui::Button("Back", btnSize)) {
                showCreditsPage = false;
                showMainMenu = true;
            }

            ImGui::EndChild();
            ImGui::End();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }
        else if (showGallery) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Gallery", &showGallery, ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Back")) {
                showCreditsPage = false;
                showGallery = false; // Close gallery
                showMainMenu = true;
            }
            ImGui::BeginChild("Thumbnails", ImGui::GetIO().DisplaySize, true);
            const float maxThumbHeight = 200.0f;
            const float padding = 10.0f;
            float x = 0.0f;
            float maxWidth = ImGui::GetContentRegionAvail().x;
            for (int i = 0; i < (int)Gallery::textures.size(); i++) {
                ImVec2 original = Gallery::imgSizes[i];
                float ratio = original.x / original.y;
                ImVec2 thumbSize = ImVec2(maxThumbHeight * ratio, maxThumbHeight);

                if (x + thumbSize.x > maxWidth) {
                    x = 0.0f;
                    ImGui::NewLine();
                }

                ImGui::PushID(i);
                ImGui::Image((ImTextureID)(uintptr_t)Gallery::textures[i].idx, thumbSize);
                if (ImGui::IsItemClicked()) {
                    Gallery::selectedImage = i;
                    Gallery::fullscreenOpen = true;
					showGallery = false; // Hide gallery when image is selected
                }
                ImGui::PopID();
                x += thumbSize.x + padding;
                ImGui::SameLine();
            }
            ImGui::EndChild();
            ImGui::End();
        }
        else if (Gallery::fullscreenOpen && Gallery::selectedImage >= 0) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 viewport = io.DisplaySize;
            ImVec2 imgSize = Gallery::imgSizes[Gallery::selectedImage];
            // Compute scale to fit viewport
            float scale = 1.0f;
            if (imgSize.x > viewport.x || imgSize.y > viewport.y) {
                float sx = viewport.x / imgSize.x;
                float sy = viewport.y / imgSize.y;
                scale = (sx < sy) ? sx : sy;
            }
            ImVec2 displaySize(imgSize.x * scale, imgSize.y * scale);
            ImVec2 pos((viewport.x - displaySize.x) * 0.5f, (viewport.y - displaySize.y) * 0.5f);

            // Draw full-screen black background
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(viewport);
            ImGui::Begin("##BgFull", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), viewport, IM_COL32(0, 0, 0, 255));
            ImGui::End();

            // Draw centered image window
            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(displaySize);
            ImGui::Begin("##FullScreen", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
            ImGui::Image((ImTextureID)(uintptr_t)Gallery::textures[Gallery::selectedImage].idx, displaySize);
            ImGui::End();

            // Click or ESC to close
            if (ImGui::IsMouseClicked(0)) {
                Gallery::fullscreenOpen = false;
				showGallery = true; // Show gallery again
            }
        }
        // Only render main editor (menu bar, sidebar, dock, tool windows) when past the start menu.
        // This avoids drawing two layers (start menu + editor) and keeps the top bar interactable after Start.
        if (!showMainMenu && !takingScreenshot && !showCreditsPage && !showGallery && !Gallery::fullscreenOpen)
        {
            //imgui loop
            //ImGui_ImplGlfw_NewFrame();
            //ImGui_Implbgfx_NewFrame();
            //ImGui::NewFrame();

            //transformation gizmo
            ImGuizmo::BeginFrame();

            // 3D Viewport as its own window (LichtFeld-style): rounded corners, resizes with right sidebar
            {
                const float viewportW = viewport->Size.x - g_LeftPanelWidth - g_RightPanelWidth;
                const float viewportH = viewport->Size.y - g_MainMenuHeight;
                ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + g_LeftPanelWidth, viewport->Pos.y + g_MainMenuHeight), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(viewportW > 0 ? viewportW : 1.0f, viewportH > 0 ? viewportH : 1.0f), ImGuiCond_Always);

                ImGuiWindowFlags viewport_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse
                    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

                const float viewport_rounding = 12.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, viewport_rounding);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // transparent so 3D shows through
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.25f, 0.23f, 0.6f)); // subtle border

                if (ImGui::Begin("##3DViewport", nullptr, viewport_flags))
                {
                    g_ViewportRectX = ImGui::GetWindowPos().x;
                    g_ViewportRectY = ImGui::GetWindowPos().y;
                    g_ViewportRectW = ImGui::GetWindowSize().x;
                    g_ViewportRectH = ImGui::GetWindowSize().y;
                    // Reserve content area so the window has correct size; 3D is drawn by bgfx into this rect
                    ImGui::Dummy(ImGui::GetContentRegionAvail());
                    // Content rect in screen space for the 3D viewport area.
                    ImVec2 rectMin = ImGui::GetItemRectMin();
                    ImVec2 rectMax = ImGui::GetItemRectMax();
                    float rectW = rectMax.x - rectMin.x;
                    float rectH = rectMax.y - rectMin.y;

                    if (rectW > 1.0f && rectH > 1.0f)
                    {
                        // Build current camera view / projection matrices for the main scene
                        // and the transform gizmo.
                        float view[16];
                        Camera& cam = cameras[currentCameraIndex];
                        bx::mtxLookAt(view, cam.position,
                            bx::add(cam.position, cam.front),
                            cam.up);

                        float proj[16];
                        bx::mtxProj(proj, cam.fov, rectW / rectH,
                            cam.nearClip, cam.farClip,
                            bgfx::getCaps()->homogeneousDepth);

                        // --- Viewport orientation gizmo (bottom-right) with colored spheres ---
                        // Clicking on a sphere snaps the camera to that axis view direction.
                        // Dragging smoothly rotates the camera.
                        // Each sphere is colored according to its axis: Red=X, Green=Y, Blue=Z
                        {
                            const ImVec2 gizmoSize(96.0f, 96.0f);
                            const float padding = 12.0f;
                            ImVec2 gizmoPos(
                                rectMax.x - gizmoSize.x - padding,
                                rectMax.y - gizmoSize.y - padding);

                            DrawOrientationSphereGizmo(view, proj, gizmoPos, gizmoSize, rectW, rectH, cam);

                            // Note: Camera updates are handled directly in DrawOrientationSphereGizmo,
                            // so no need to process ImGuizmo::IsUsing() like before.
                        }

                        // Draw transform gizmo for the selected object (if any), using the same
                        // camera matrices and viewport rect so input stays consistent.
                        if (selectedInstance)
                        {
                            DrawGizmoForSelected(selectedInstance, rectMin.x, rectMin.y, view, proj, rectW, rectH);
                        }
                    }
                }
                ImGui::End();

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
            }

            // Use main menu bar so it stays on top and receives clicks (viewport menu bar, not a regular window)
            if (ImGui::BeginMainMenuBar())
            {
                g_MainMenuHeight = ImGui::GetWindowSize().y;
                
                
                if (ImGui::BeginMenu("File", true))
                {
                    if (ImGui::MenuItem("Open.."))
                    {
                        //std::string loadFilePath = openFileDialog(false); // Open load dialog
                        //if (!loadFilePath.empty())
                        //    loadSceneText(loadFilePath, instances, availableTextures);
                        importedObjMap = loadSceneFromFile(instances, availableTextures, bufferMap);
                    }
                    if (ImGui::MenuItem("Save", "Ctrl+S"))
                    {
                        //std::string saveFilePath = openFileDialog(true); // Open save dialog
                        //if (!saveFilePath.empty())
                        //    saveScene(saveFilePath, instances, availableTextures);
                        saveSceneToFile(instances, availableTextures, importedObjMap);
                    }
                    // ========================
                    // UPDATED IMPORT MENU CODE
                    // ========================

                    /*
                    Inside your ImGui File menu, replace the old OBJ import code with the following:
                    This code:
                      1. Opens a file dialog.
                      2. Loads all meshes from the file (with centering, as done in loadImportedMeshes).
                      3. Creates an empty parent instance (a grouping node) at the center.
                      4. For each imported mesh, creates vertex/index buffers and spawns a child instance.
                      5. Adds each child to the empty parent so that scaling the parent scales all children.
                    */
                    if (ImGui::MenuItem("Import OBJ"))
                    {
                        const char* modelFilter =
                            "All 3D Models\0*.obj;*.fbx;*.dae;*.gltf;*.glb;*.ply;*.stl;*.3ds\0"
                            "OBJ Files (*.obj)\0*.obj\0"
                            "FBX Files (*.fbx)\0*.fbx\0"
                            "COLLADA Files (*.dae)\0*.dae\0"
                            "glTF Files (*.gltf;*.glb)\0*.gltf;*.glb\0"
                            "PLY Files (*.ply)\0*.ply\0"
                            "STL Files (*.stl)\0*.stl\0"
                            "3DS Files (*.3ds)\0*.3ds\0"
                            "All Files (*.*)\0*.*\0";

                        std::string absPath = OpenFileDialog(glfwGetWin32Window(window), modelFilter);
                        
                        if (absPath.empty())
                        {
                            std::cout << "[Import OBJ] File dialog was cancelled or failed." << std::endl;
                        }
                        else
                        {
                            std::cout << "[Import OBJ] Selected absolute path: " << absPath << std::endl;
                            
                            // Check if file exists
                            if (!fs::exists(absPath))
                            {
                                std::cerr << "[Import OBJ] ERROR: File does not exist: " << absPath << std::endl;
                            }
                            else
                            {
                                // Convert to absolute path with forward slashes for consistency
                                fs::path absPathObj(absPath);
                                std::string normalizedAbsPath = ConvertBackslashesToForward(absPathObj.string());
                                std::cout << "[Import OBJ] Normalized absolute path: " << normalizedAbsPath << std::endl;
                                
                                // Load all meshes (with textures) using the updated importer.
                                // Pass absolute path to Assimp, but store relative path for scene saving
                                std::vector<ImportedMesh> importedMeshes = loadImportedMeshes(normalizedAbsPath);
                                
                                // Get relative path for scene file storage
                                std::string relPath = GetRelativePath(absPath);
                                std::string normalizedRelPath = ConvertBackslashesToForward(relPath);
                                std::cout << "[Import OBJ] Relative path (for scene file): " << normalizedRelPath << std::endl;
                                
                                std::string fileName = fs::path(normalizedRelPath).stem().string();

                                if (importedMeshes.empty())
                                {
                                    std::cerr << "[Import OBJ] ERROR: Failed to load any meshes from file. Check console for Assimp errors." << std::endl;
                                }
                                else
                                {
                                    // Compute overall group center by averaging each mesh's global translation.
                                    aiVector3D groupCenter(0.0f, 0.0f, 0.0f);
                                    for (const auto& impMesh : importedMeshes) {
                                        groupCenter.x += impMesh.transform.a4;
                                        groupCenter.y += impMesh.transform.b4;
                                        groupCenter.z += impMesh.transform.c4;
                                    }
                                    if (!importedMeshes.empty()) {
                                        groupCenter.x /= importedMeshes.size();
                                        groupCenter.y /= importedMeshes.size();
                                        groupCenter.z /= importedMeshes.size();
                                    }

                                    // Create an empty parent instance at the overall group center.
                                    Instance* parentInstance = new Instance(instanceCounter++, fileName + "_group", "empty",
                                        groupCenter.x, groupCenter.y, groupCenter.z,
                                        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
                                    instances.push_back(parentInstance);

                                    // For each imported mesh, create buffers and spawn a child instance.
                                    for (size_t i = 0; i < importedMeshes.size(); ++i)
                                    {
                                        bgfx::VertexBufferHandle vbh_imported;
                                        bgfx::IndexBufferHandle ibh_imported;
                                        createMeshBuffers(importedMeshes[i].meshData, vbh_imported, ibh_imported);

                                        Instance* childInst = new Instance(instanceCounter++, fileName + "_" + std::to_string(i),
                                            fileName, 0.0f, 0.0f, 0.0f,
                                            vbh_imported, ibh_imported);
                                        childInst->meshNumber = i;
                                        // Cache editable mesh data for geometry tools.
                                        g_InstanceMeshData[childInst->id] = importedMeshes[i].meshData;
                                        // Decompose the imported mesh's transform.
                                        aiVector3D scaling, position;
                                        aiQuaternion rotation;
                                        importedMeshes[i].transform.Decompose(scaling, rotation, position);
                                        // Set the child's position relative to the parent (group center).
                                        childInst->position[0] = position.x - groupCenter.x;
                                        childInst->position[1] = position.y - groupCenter.y;
                                        childInst->position[2] = position.z - groupCenter.z;
                                        // For simplicity, we leave rotation at zero or convert the quaternion if desired.
                                        childInst->rotation[0] = childInst->rotation[1] = childInst->rotation[2] = 0.0f;
                                        childInst->scale[0] = scaling.x;
                                        childInst->scale[1] = scaling.y;
                                        childInst->scale[2] = scaling.z;

                                        // *** NEW: Assign the diffuse texture from the imported mesh ***
                                        childInst->diffuseTexture = importedMeshes[i].diffuseTexture;
                                        // --- NEW: Apply diffuse color if present and no texture ---
                                        if (importedMeshes[i].hasDiffuseColor && !bgfx::isValid(childInst->diffuseTexture)) {
                                            childInst->objectColor[0] = importedMeshes[i].diffuseColor[0];
                                            childInst->objectColor[1] = importedMeshes[i].diffuseColor[1];
                                            childInst->objectColor[2] = importedMeshes[i].diffuseColor[2];
                                            childInst->objectColor[3] = importedMeshes[i].diffuseColor[3];
                                            std::cout << "[INFO] Applied MTL diffuse color to: " << childInst->name << std::endl;
                                        }
                                        // Add this mesh as a child of the empty parent.
                                        parentInstance->addChild(childInst);
                                    }

                                    std::cout << "[Import OBJ] Successfully imported " << importedMeshes.size()
                                        << " mesh(es) grouped under " << fileName << "_group" << std::endl;
                                    importedObjMap[fileName] = normalizedRelPath;
                                }
                            }
                        }
                    }
                    if (ImGui::MenuItem("Import Texture"))
                    {
                        // Open a file dialog to select a texture file.
                        std::string texFilePath = openFileDialog(false);
                        if (!texFilePath.empty())
                        {
                            // Optionally, convert backslashes to forward slashes.
                            std::string normalizedPath = ConvertBackslashesToForward(texFilePath);
                            std::cout << "Importing texture from: " << normalizedPath << std::endl;
                            // Load the texture (currently only DDS files are supported).
                            bgfx::TextureHandle newTexture = loadTextureFile(normalizedPath.c_str());
                            if (newTexture.idx != bgfx::kInvalidHandle)
                            {
                                // Create a TextureOption entry for the new texture.
                                TextureOption texOpt;
                                texOpt.name = fs::path(normalizedPath).stem().string();
                                texOpt.handle = newTexture;
                                availableTextures.push_back(texOpt);
                                std::cout << "Texture '" << texOpt.name << "' imported, handle: "
                                    << texOpt.handle.idx << std::endl;
                            }
                            else
                            {
                                std::cout << "Failed to load texture." << std::endl;
                            }
                        }
                    }
                    if (ImGui::MenuItem("Back to Main Menu"))
                    {
                        showMainMenu = true;
                    }
                    if (ImGui::MenuItem("Exit"))
                    {
                        glfwSetWindowShouldClose(window, true);
                    }

                    //if (ImGui::MenuItem("Close", "Ctrl+W")) { /* Do stuff */ }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Add"))
                {
                    if (ImGui::BeginMenu("Objects"))
                    {
                        if (ImGui::MenuItem("Cube"))
                        {
                            spawnInstanceAtCenter("cube", "cube", vbh_cube, ibh_cube, instances);
                            std::cout << "Cube spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Capsule"))
                        {
                            spawnInstanceAtCenter("capsule", "capsule", vbh_capsule, ibh_capsule, instances);
                            std::cout << "Capsule spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Cylinder"))
                        {
                            spawnInstanceAtCenter("cylinder", "cylinder", vbh_cylinder, ibh_cylinder, instances);
                            std::cout << "Cylinder spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Cone"))
                        {
                            spawnInstanceAtCenter("cone", "cone", vbh_cone, ibh_cone, instances);
                            std::cout << "Cone spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Sphere"))
                        {
                            spawnInstanceAtCenter("sphere", "sphere", vbh_sphere, ibh_sphere, instances);
                            std::cout << "Sphere spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Plane"))
                        {
                            spawnInstanceAtCenter("plane", "plane", vbh_plane, ibh_plane, instances);
                            std::cout << "Plane spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Suzanne"))
                        {
                            spawnInstanceAtCenter("mesh", "mesh", vbh_mesh, ibh_mesh, instances);
                            std::cout << "Suzanne spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Cornell Box"))
                        {
                            float x = 0.0f;
                            float y = 0.0f;
                            float z = 0.0f;
                            // --- Spawn Cornell Box with Hierarchy ---
                            Instance* cornellBox = new Instance(instanceCounter++, "cornell_box", "empty", x, y, z, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
                            // Create a walls node (dummy instance without geometry)
                            Instance* wallsNode = new Instance(instanceCounter++, "walls", "empty", 0.0f, -1.0f, 0.0f, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
                            wallsNode->scale[0] = 0.2f;
                            wallsNode->scale[1] = 0.2f;
                            wallsNode->scale[2] = 0.2f;

                            Instance* floorPlane = new Instance(instanceCounter++, "floor", "plane", 0.0f, -6.0f, 0.0f, vbh_plane, ibh_plane);
                            RegisterInstanceMeshFromType(floorPlane);
                            wallsNode->addChild(floorPlane);
                            Instance* ceilingPlane = new Instance(instanceCounter++, "ceiling", "plane", 0.0f, 14.0f, 0.0f, vbh_plane, ibh_plane);
                            RegisterInstanceMeshFromType(ceilingPlane);
                            wallsNode->addChild(ceilingPlane);
                            Instance* backPlane = new Instance(instanceCounter++, "back", "plane", 0.0f, 4.0f, -10.0f, vbh_plane, ibh_plane);
                            RegisterInstanceMeshFromType(backPlane);
                            backPlane->rotation[0] = 1.57f;
                            wallsNode->addChild(backPlane);
                            Instance* leftPlane = new Instance(instanceCounter++, "left_wall", "plane", 10.0f, 4.0f, 0.0f, vbh_plane, ibh_plane);
                            RegisterInstanceMeshFromType(leftPlane);
                            leftPlane->objectColor[0] = 1.0f; leftPlane->objectColor[1] = 0.0f; leftPlane->objectColor[2] = 0.0f; leftPlane->objectColor[3] = 1.0f;
                            leftPlane->rotation[2] = 1.57f;
                            wallsNode->addChild(leftPlane);
                            Instance* rightPlane = new Instance(instanceCounter++, "right_wall", "plane", -10.0f, 4.0f, 0.0f, vbh_plane, ibh_plane);
                            RegisterInstanceMeshFromType(rightPlane);
                            rightPlane->objectColor[0] = 0.0f; rightPlane->objectColor[1] = 1.0f; rightPlane->objectColor[2] = 0.0f; rightPlane->objectColor[3] = 1.0f;
                            rightPlane->rotation[2] = 1.57f;
                            wallsNode->addChild(rightPlane);

                            Instance* innerCube = new Instance(instanceCounter++, "inner_cube", "cube", 0.8f, -1.5f, 0.4f, vbh_cube, ibh_cube);
                            innerCube->rotation[1] = 0.2f;
                            innerCube->scale[0] = 0.6f;
                            innerCube->scale[1] = 0.6f;
                            innerCube->scale[2] = 0.6f;
                            Instance* innerRectBox = new Instance(instanceCounter++, "inner_rectbox", "cube", -1.0f, -0.7f, 0.4f, vbh_cube, ibh_cube);
                            innerRectBox->rotation[1] = -0.3f;
                            innerRectBox->scale[0] = 0.6f;
                            innerRectBox->scale[1] = 1.5f;
                            innerRectBox->scale[2] = 0.6f;
                            cornellBox->addChild(wallsNode);
                            cornellBox->addChild(innerCube);
                            cornellBox->addChild(innerRectBox);
                            RegisterInstanceMeshFromType(innerCube);
                            RegisterInstanceMeshFromType(innerRectBox);
                            instances.push_back(cornellBox);
                            std::cout << "Cornell Box spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Teapot"))
                        {
                            spawnInstanceAtCenter("teapot", "teapot", vbh_teapot, ibh_teapot, instances);
                            std::cout << "Teapot spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Bunny"))
                        {
                            spawnInstanceAtCenter("bunny", "bunny", vbh_bunny, ibh_bunny, instances);
                            std::cout << "Bunny spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Lucy"))
                        {
                            spawnInstanceAtCenter("lucy", "lucy", vbh_lucy, ibh_lucy, instances);
                            std::cout << "Lucy spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Empty"))
                        {
                            spawnInstanceAtCenter("empty", "empty", BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, instances);
                            std::cout << "Empty object spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Arrow"))
                        {
                            spawnInstanceAtCenter("arrow", "arrow", vbh_arrow, ibh_arrow, instances);
                            std::cout << "Arrow spawned" << std::endl;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Lights"))
                    {
                        if (ImGui::MenuItem("Regular Light"))
                        {
                            spawnLight(camera, vbh_sphere, ibh_sphere, vbh_cone, ibh_cone, instances);
                            std::cout << "Light spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Rotating Spotlight"))
                        {
                            float radius = 5.0f;
                            float height = 5.0f;
                            Instance* rotatingLight = new Instance(instanceCounter++, "rotating_light", "light",
                                radius, height, 0.0f, vbh_cone, ibh_cone);
                            rotatingLight->isLight = true;
                            rotatingLight->lightProps.type = LightType::Spot;
                            rotatingLight->lightProps.intensity = 2.0f;
                            rotatingLight->lightProps.range = 20.0f;
                            rotatingLight->lightProps.coneAngle = 0.5f;  // Narrower beam
                            instances.push_back(rotatingLight);
                            std::cout << "Rotating spotlight added" << std::endl;
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Comic Elements"))
                    {
                        if (ImGui::MenuItem("Comic Border"))
                        {
                            spawnInstanceAtCenter("comicborder", "comicborder", vbh_comicborder, ibh_comicborder, instances);
                            std::cout << "Comic Border object spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Text"))
                        {
                            // Spawn a new instance with type "text" using the text quad buffers.
                            spawnInstanceAtCenter("comicBubble", "text", vbh_textQuad, ibh_textQuad, instances);
                            Instance* textInst = instances.back();
                            // Set a default text string.
                            textInst->textContent = "New Comic Bubble";

                            // Immediately generate its text texture.
                            updateTextTexture(textInst);
                            std::cout << "Comic bubble Text spawned" << std::endl;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Comic Bubble Object 1 - Right"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject1", "comicbubble1", vbh_comicbubble1, ibh_comicbubble1, instances);
                            std::cout << "Comic Bubble Object 1 - Right spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 2 - Right"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject2", "comicbubble2", vbh_comicbubble2, ibh_comicbubble2, instances);
                            std::cout << "Comic Bubble Object 2 - Right spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 3 - Right"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject3", "comicbubble3", vbh_comicbubble3, ibh_comicbubble3, instances);
                            std::cout << "Comic Bubble Object 3 - Right spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 4 - Left"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject4", "comicbubble4", vbh_comicbubble4, ibh_comicbubble4, instances);
                            std::cout << "Comic Bubble Object 4 - Left spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 5 - Left"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject5", "comicbubble5", vbh_comicbubble5, ibh_comicbubble5, instances);
                            std::cout << "Comic Bubble Object 5 - Left spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 6 - Left"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject6", "comicbubble6", vbh_comicbubble6, ibh_comicbubble6, instances);
                            std::cout << "Comic Bubble Object 6 - Left spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 7 - Middle"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject7", "comicbubble7", vbh_comicbubble7, ibh_comicbubble7, instances);
                            std::cout << "Comic Bubble Object 7 - Middle spawned" << std::endl;
                        }
                        if (ImGui::MenuItem("Comic Bubble Object 8 - Middle"))
                        {
                            spawnInstanceAtCenter("comicbubbleobject8", "comicbubble8", vbh_comicbubble8, ibh_comicbubble8, instances);
                            std::cout << "Comic Bubble Object 8 - Middle spawned" << std::endl;
                        }
                        ImGui::EndMenu();
                    }

                    if (ImGui::MenuItem("Import OBJ"))
                    {
                        const char* modelFilter =
                            "All 3D Models\0*.obj;*.fbx;*.dae;*.gltf;*.glb;*.ply;*.stl;*.3ds\0"
                            "OBJ Files (*.obj)\0*.obj\0"
                            "FBX Files (*.fbx)\0*.fbx\0"
                            "COLLADA Files (*.dae)\0*.dae\0"
                            "glTF Files (*.gltf;*.glb)\0*.gltf;*.glb\0"
                            "PLY Files (*.ply)\0*.ply\0"
                            "STL Files (*.stl)\0*.stl\0"
                            "3DS Files (*.3ds)\0*.3ds\0"
                            "All Files (*.*)\0*.*\0";

                        std::string absPath = OpenFileDialog(glfwGetWin32Window(window), modelFilter);
                        
                        if (absPath.empty())
                        {
                            std::cout << "[Import OBJ] File dialog was cancelled or failed." << std::endl;
                        }
                        else
                        {
                            std::cout << "[Import OBJ] Selected absolute path: " << absPath << std::endl;
                            
                            // Check if file exists
                            if (!fs::exists(absPath))
                            {
                                std::cerr << "[Import OBJ] ERROR: File does not exist: " << absPath << std::endl;
                            }
                            else
                            {
                                // Convert to absolute path with forward slashes for consistency
                                fs::path absPathObj(absPath);
                                std::string normalizedAbsPath = ConvertBackslashesToForward(absPathObj.string());
                                std::cout << "[Import OBJ] Normalized absolute path: " << normalizedAbsPath << std::endl;
                                
                                // Load all meshes (with textures) using the updated importer.
                                // Pass absolute path to Assimp, but store relative path for scene saving
                                std::vector<ImportedMesh> importedMeshes = loadImportedMeshes(normalizedAbsPath);
                                
                                // Get relative path for scene file storage
                                std::string relPath = GetRelativePath(absPath);
                                std::string normalizedRelPath = ConvertBackslashesToForward(relPath);
                                std::cout << "[Import OBJ] Relative path (for scene file): " << normalizedRelPath << std::endl;
                                
                                std::string fileName = fs::path(normalizedRelPath).stem().string();

                                if (importedMeshes.empty())
                                {
                                    std::cerr << "[Import OBJ] ERROR: Failed to load any meshes from file. Check console for Assimp errors." << std::endl;
                                }
                                else
                                {
                                    // Compute overall group center by averaging each mesh's global translation.
                                    aiVector3D groupCenter(0.0f, 0.0f, 0.0f);
                                    for (const auto& impMesh : importedMeshes) {
                                        groupCenter.x += impMesh.transform.a4;
                                        groupCenter.y += impMesh.transform.b4;
                                        groupCenter.z += impMesh.transform.c4;
                                    }
                                    if (!importedMeshes.empty()) {
                                        groupCenter.x /= importedMeshes.size();
                                        groupCenter.y /= importedMeshes.size();
                                        groupCenter.z /= importedMeshes.size();
                                    }

                                    // Create an empty parent instance at the overall group center.
                                    Instance* parentInstance = new Instance(instanceCounter++, fileName + "_group", "empty",
                                        groupCenter.x, groupCenter.y, groupCenter.z,
                                        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
                                    instances.push_back(parentInstance);

                                    // For each imported mesh, create buffers and spawn a child instance.
                                    for (size_t i = 0; i < importedMeshes.size(); ++i)
                                    {
                                        bgfx::VertexBufferHandle vbh_imported;
                                        bgfx::IndexBufferHandle ibh_imported;
                                        createMeshBuffers(importedMeshes[i].meshData, vbh_imported, ibh_imported);

                                        Instance* childInst = new Instance(instanceCounter++, fileName + "_" + std::to_string(i),
                                            fileName, 0.0f, 0.0f, 0.0f,
                                            vbh_imported, ibh_imported);
                                        childInst->meshNumber = i;
                                        // Decompose the imported mesh's transform.
                                        aiVector3D scaling, position;
                                        aiQuaternion rotation;
                                        importedMeshes[i].transform.Decompose(scaling, rotation, position);
                                        // Set the child's position relative to the parent (group center).
                                        childInst->position[0] = position.x - groupCenter.x;
                                        childInst->position[1] = position.y - groupCenter.y;
                                        childInst->position[2] = position.z - groupCenter.z;
                                        // For simplicity, we leave rotation at zero or convert the quaternion if desired.
                                        childInst->rotation[0] = childInst->rotation[1] = childInst->rotation[2] = 0.0f;
                                        childInst->scale[0] = scaling.x;
                                        childInst->scale[1] = scaling.y;
                                        childInst->scale[2] = scaling.z;

                                        // *** NEW: Assign the diffuse texture from the imported mesh ***
                                        childInst->diffuseTexture = importedMeshes[i].diffuseTexture;
                                        // --- NEW: Apply diffuse color if present and no texture ---
                                        if (importedMeshes[i].hasDiffuseColor && !bgfx::isValid(childInst->diffuseTexture)) {
                                            childInst->objectColor[0] = importedMeshes[i].diffuseColor[0];
                                            childInst->objectColor[1] = importedMeshes[i].diffuseColor[1];
                                            childInst->objectColor[2] = importedMeshes[i].diffuseColor[2];
                                            childInst->objectColor[3] = importedMeshes[i].diffuseColor[3];
                                            std::cout << "[INFO] Applied MTL diffuse color to: " << childInst->name << std::endl;
                                        }
                                        // Add this mesh as a child of the empty parent.
                                        parentInstance->addChild(childInst);
                                    }

                                    std::cout << "[Import OBJ] Successfully imported " << importedMeshes.size()
                                        << " mesh(es) grouped under " << fileName << "_group" << std::endl;
                                    importedObjMap[fileName] = normalizedRelPath;
                                }
                            }
                        }
                    }

                    // Load Sample Models submenu (dynamic discovery)
                    if (ImGui::BeginMenu("Load Sample Models"))
                    {
                        fs::path samplesRoot = fs::path("meshes") / "samples";
                        if (fs::exists(samplesRoot) && fs::is_directory(samplesRoot))
                        {
                            // Helper to check extensions
                            auto isMeshExt = [](const fs::path &p)->bool {
                                std::string ext = p.extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                return (ext == ".obj" || ext == ".ply" || ext == ".stl");
                            };

                            // Gather candidate display names and first mesh path for each
                            std::map<std::string, fs::path> samplesMap;

                            for (auto &entry : fs::directory_iterator(samplesRoot))
                            {
                                std::string itemName = entry.path().filename().string();
                                fs::path meshFile;

                                if (fs::is_regular_file(entry.path()) && isMeshExt(entry.path()))
                                {
                                    // Use file name as display
                                    samplesMap[itemName] = entry.path();
                                    continue;
                                }

                                if (fs::is_directory(entry.path()))
                                {
                                    // 1) top-level files
                                    for (auto &f : fs::directory_iterator(entry.path()))
                                    {
                                        if (fs::is_regular_file(f.path()) && isMeshExt(f.path()))
                                        {
                                            meshFile = f.path();
                                            break;
                                        }
                                    }
                                    // 2) reconstruction/
                                    if (meshFile.empty())
                                    {
                                        fs::path recon = entry.path() / "reconstruction";
                                        if (fs::exists(recon) && fs::is_directory(recon))
                                        {
                                            for (auto &f : fs::recursive_directory_iterator(recon))
                                            {
                                                if (fs::is_regular_file(f.path()) && isMeshExt(f.path()))
                                                {
                                                    meshFile = f.path();
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    // 3) recursive fallback
                                    if (meshFile.empty())
                                    {
                                        for (auto &f : fs::recursive_directory_iterator(entry.path()))
                                        {
                                            if (fs::is_regular_file(f.path()) && isMeshExt(f.path()))
                                            {
                                                meshFile = f.path();
                                                break;
                                            }
                                        }
                                    }

                                    if (!meshFile.empty())
                                        samplesMap[itemName] = meshFile;
                                }
                            }

                            if (samplesMap.empty())
                            {
                                ImGui::MenuItem("No samples found");
                            }
                            else
                            {
                                for (const auto &p : samplesMap)
                                {
                                    const std::string &displayName = p.first;
                                    const fs::path &meshPath = p.second;
                                    if (ImGui::MenuItem(displayName.c_str()))
                                    {
                                        std::string samplePath = meshPath.string();
                                        std::cout << "[Load Sample] Attempting to load: " << samplePath << std::endl;

                                        if (!fs::exists(samplePath))
                                        {
                                            std::cerr << "[Load Sample] ERROR: File does not exist: " << samplePath << std::endl;
                                        }
                                        else
                                        {
                                            std::vector<ImportedMesh> importedMeshes = loadImportedMeshes(samplePath);
                                            if (importedMeshes.empty())
                                            {
                                                std::cerr << "[Load Sample] ERROR: Failed to load meshes from: " << samplePath << std::endl;
                                            }
                                            else
                                            {
                                                // Compute group center
                                                aiVector3D groupCenter(0.0f, 0.0f, 0.0f);
                                                for (const auto& impMesh : importedMeshes)
                                                {
                                                    groupCenter.x += impMesh.transform.a4;
                                                    groupCenter.y += impMesh.transform.b4;
                                                    groupCenter.z += impMesh.transform.c4;
                                                }
                                                if (!importedMeshes.empty())
                                                {
                                                    groupCenter.x /= importedMeshes.size();
                                                    groupCenter.y /= importedMeshes.size();
                                                    groupCenter.z /= importedMeshes.size();
                                                }

                                                Instance* parentInstance = new Instance(instanceCounter++, displayName + "_group", "empty",
                                                    groupCenter.x, groupCenter.y, groupCenter.z,
                                                    BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
                                                instances.push_back(parentInstance);

                                                for (size_t i = 0; i < importedMeshes.size(); ++i)
                                                {
                                                    bgfx::VertexBufferHandle vbh_imported;
                                                    bgfx::IndexBufferHandle ibh_imported;
                                                    createMeshBuffers(importedMeshes[i].meshData, vbh_imported, ibh_imported);

                                                    Instance* childInst = new Instance(instanceCounter++, displayName + "_" + std::to_string(i), displayName,
                                                        0.0f, 0.0f, 0.0f, vbh_imported, ibh_imported);
                                                    childInst->meshNumber = i;

                                                    aiVector3D scaling, position;
                                                    aiQuaternion rotation;
                                                    importedMeshes[i].transform.Decompose(scaling, rotation, position);

                                                    childInst->position[0] = position.x - groupCenter.x;
                                                    childInst->position[1] = position.y - groupCenter.y;
                                                    childInst->position[2] = position.z - groupCenter.z;
                                                    childInst->rotation[0] = childInst->rotation[1] = childInst->rotation[2] = 0.0f;
                                                    childInst->scale[0] = scaling.x;
                                                    childInst->scale[1] = scaling.y;
                                                    childInst->scale[2] = scaling.z;

                                                    childInst->diffuseTexture = importedMeshes[i].diffuseTexture;
                                                    if (importedMeshes[i].hasDiffuseColor && !bgfx::isValid(childInst->diffuseTexture))
                                                    {
                                                        childInst->objectColor[0] = importedMeshes[i].diffuseColor[0];
                                                        childInst->objectColor[1] = importedMeshes[i].diffuseColor[1];
                                                        childInst->objectColor[2] = importedMeshes[i].diffuseColor[2];
                                                        childInst->objectColor[3] = importedMeshes[i].diffuseColor[3];
                                                    }

                                                    parentInstance->addChild(childInst);
                                                }

                                                std::cout << "[Load Sample] Successfully loaded " << displayName << " with " << importedMeshes.size() << " mesh(es)" << std::endl;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            ImGui::MenuItem("No samples found");
                        }

                        ImGui::EndMenu();
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, gCmdManager.canUndo()))
                        gCmdManager.undo();
                    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, gCmdManager.canRedo()))
                        gCmdManager.redo();
                    if (ImGui::MenuItem("Delete Last Instance"))
                    {
                        if (!instances.empty())
                        {
                            Instance* inst = instances.back();
                            size_t idx = instances.size() - 1;
                            gCmdManager.executeCommand(
                                std::make_unique<DeleteInstanceCommand>(inst, &instances, idx)
                            );
                            selectedInstance = nullptr;
                            std::cout << "Last Instance removed (undoable)" << std::endl;
                        }
                    }
                    if (ImGui::MenuItem("Clear All Instances"))
                    {
                        if (!instances.empty())
                        {
                            gCmdManager.executeCommand(
                                std::make_unique<ClearInstancesCommand>(&instances)
                            );
                            selectedInstance = nullptr;
                        }
                    }
                    ImGui::EndMenu();
                }
                // VIEW MENU
                if (ImGui::BeginMenu("View", true))
                {
                    // ImGui::MenuItem("Inspector", nullptr, &show_Inspector);
                    // ImGui::MenuItem("Object List", nullptr, &show_ObjectList);
                    ImGui::MenuItem("Gallery", nullptr, &show_Gallery);
                    // ImGui::MenuItem("3D Reconstructor", nullptr, &show_Reconstructor);
                    ImGui::MenuItem("Info", nullptr, &show_Info);
                    ImGui::MenuItem("Controls", nullptr, &show_Controls);
                    ImGui::MenuItem("Screenshot", nullptr, &show_Screenshot);
                    ImGui::MenuItem("Camera Settings", nullptr, &show_CameraSettings);
                    ImGui::MenuItem("Cameras", nullptr, &show_Cameras);
                    ImGui::MenuItem("Log Console", nullptr, &show_LogConsole);
                    ImGui::EndMenu();
                }
            ImGui::EndMainMenuBar();
            }
            else
            { g_MainMenuHeight = 32.0f; }

            // Fixed left-hand toolbar (operations: translate/rotate/scale)
            RenderLeftSidebar();

            // Fixed right-hand sidebar (Object List / Inspector / Reconstructor),
            // in a LichtFeld-like stacked layout with no tabs or docking.
            RenderRightSidebar();

            // Inspector and Reconstructor are drawn only in the right sidebar (see RenderRightSidebar).
            // Removed floating Inspector window to avoid duplicate UI and imgui.ini restoring old layout.

            if (show_LogConsole)
            {
                Logger::GetInstance().DrawImGuiLogger(&show_LogConsole);
            }

            if (show_Gallery)
            {
            ImGui::Begin("Gallery", &show_Gallery, window_flags);
           
            ImGui::End();
            }

            // 3D Reconstructor is now rendered inside the fixed right sidebar (RenderRightSidebar)

            /*-------------------------------------------------------------------------------------*/

            if (show_Info)
            {
            ImGui::Begin("Info", &show_Info, window_flags);

            if (ImGui::BeginTable("Info", 2, ImGuiTableFlags_NoBordersInBody))
            {

                ImGui::TableNextColumn();
                ImGui::Text("Crosshatch Editor Demo Build");
                ImGui::Text("Press F1 to toggle bgfx stats");
                ImGui::Text("FPS: %.1f ", ImGui::GetIO().Framerate);
                ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

                ImGui::TableNextColumn();
                ImGui::Text("Rendered Instances: %d", instances.size());
                ImGui::Text("Selected Instance: %s", selectedInstance ? selectedInstance->name.c_str() : "None");
                ImGui::Text("Selected Instance ID: %d", selectedInstance ? selectedInstance->id : -1);
                ImGui::Text("Selected Instance Parent: %s", selectedInstance && selectedInstance->parent ? selectedInstance->parent->name.c_str() : "None");
                ImGui::Text("Selected Instance Children: %d", selectedInstance ? selectedInstance->children.size() : 0);
                ImGui::Separator();
                ImGui::Text("Camera Position: %.2f, %.2f, %.2f", cameras[currentCameraIndex].position.x, cameras[currentCameraIndex].position.y, cameras[currentCameraIndex].position.z);
                //ImGui::Text("Frame: % 7.3f[ms]", 1000.0f / bgfx::getStats()->cpuTimeFrame);
                ImGui::Text("");
                ImGui::EndTable();
            }
            ImGui::End();
            }

            if (show_Controls)
            {
            ImGui::Begin("Controls", &show_Controls, window_flags);

            /*ImGui::Text("Controls:");
            ImGui::Text("WASD - Move Camera");
            ImGui::Text("Right Click - Rotate Camera");
            ImGui::Text("Ctrl + Right Click - Pan Camera");
            ImGui::Text("Shift - Move Down");
            ImGui::Text("Space - Move Up");
            ImGui::Text("1 - Switch Gizmo to Translate");
            ImGui::Text("2 - Switch Gizmo to Rotate");
            ImGui::Text("3 - Switch Gizmo to Scale");
            ImGui::Text("Left Click - Select Object");
            ImGui::Text("Double Left Click - Teleport to Object");
            ImGui::Text("F1 - Toggle bgfx stats");
            ImGui::Text("F2 - Disable/Enable UI");
            ImGui::Text("F3 - Take Screenshot");*/


            if (ImGui::BeginTable("ControlsTable", 3, ImGuiTableFlags_NoBordersInBody))
            {
                // Column 1
                ImGui::TableNextColumn();
                //ImGui::Text("Controls:");
                ImGui::Text("WASD - Move Camera");
                ImGui::Text("Left Click - Select Object");
                ImGui::Text("Double Left Click - Teleport to Object");
                ImGui::Text("Right Click - Rotate Camera");
                ImGui::Text("Ctrl + Right Click - Pan Camera");
                ImGui::Text("Shift - Move Down");
                ImGui::Text("Space - Move Up");
                ImGui::Text("Scroll - Zoom In / Out");

                // Column 2
                ImGui::TableNextColumn();
                ImGui::Text("1 - Switch Gizmo to Translate");
                ImGui::Text("2 - Switch Gizmo to Rotate");
                ImGui::Text("3 - Switch Gizmo to Scale");

                // Column 3
                ImGui::TableNextColumn();
                ImGui::Text("F1 - Toggle bgfx stats");
                ImGui::Text("F2 - Disable/Enable UI");
                ImGui::Text("F3 - Take Screenshot");

                ImGui::EndTable();
            }

            ImGui::End();
            }

            if (show_Screenshot)
            {
            ImGui::Begin("Screenshot", &show_Screenshot, window_flags);
            ImGui::InputText("Filename", screenshotName, IM_ARRAYSIZE(screenshotName));
            if (ImGui::Button("Save")) {
                std::string fileNameStr = screenshotName;
                if (!fileNameStr.empty()) {
                    takeScreenshotAsPng(BGFX_INVALID_HANDLE, screenshotName);
                }
                ImGui::CloseCurrentPopup();
                openScreenshotPopup = false;
            }
            ImGui::Text("To Take a screenshot");
            ImGui::Text("enter the filename that you want");
            ImGui::Text("then turn off the UI using F2");
            ImGui::Text("and press the screenshot button F3.");

            ImGui::End();
            }


            //ImGui::Begin("Crosshatch Shader Settings");
            //ImGui::Checkbox("Use Global Crosshatch Shader Settings", &useGlobalCrosshatchSettings);
            //if (useGlobalCrosshatchSettings) {
            //    const char* modeItems[] = { "Simple Lighting" };
            //    ImGui::Combo("Shader Mode", &crosshatchMode, modeItems, IM_ARRAYSIZE(modeItems));
            //    ImGui::Spacing(); ImGui::Spacing();
            //    // --- Show controls depending on the mode ---
            //    if (crosshatchMode == 0)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.0 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Smoothness", &epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Density", &strokeMultiplier, 0.01f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Primary Hatch Angle", &lineAngle1, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Secondary Hatch Angle", &lineAngle2, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Scale", &patternScale, 0.01f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Thickness", &lineThickness, 0.01f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (crosshatchMode == 1)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.1 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Smoothness", &epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Density", &strokeMultiplier, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Angle", &lineAngle1, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Scale", &patternScale, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Thickness", &lineThickness, 0.01f, -15.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (crosshatchMode == 2)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.2 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Line Smoothness", &epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Density", &strokeMultiplier, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Angle", &lineAngle1, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Scale", &patternScale, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Thickness", &lineThickness, 0.01f, -15.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        // Inner layer settings:
            //        ImGui::DragFloat("Inner Hatch Scale", &layerPatternScale, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Density", &layerStrokeMult, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Angle", &layerAngle, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Thickness", &layerLineThickness, 0.01f, -15.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (crosshatchMode == 3)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.3 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Line Smoothness", &epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Density", &strokeMultiplier, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Angle", &lineAngle1, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Scale", &patternScale, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Thickness", &lineThickness, 0.01f, -15.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        // Inner layer settings:
            //        ImGui::DragFloat("Inner Hatch Scale", &layerPatternScale, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Density", &layerStrokeMult, 0.01f, 0.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Angle", &layerAngle, 0.01f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Thickness", &layerLineThickness, 0.01f, -15.0f, 15.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (crosshatchMode == 4)
            //    {
            //        ImGui::Text("Simple Lighting (No Crosshatch)");
            //    }

            //    if (crosshatchMode != 4) {
            //        ImGui::Spacing(); ImGui::Spacing();
            //        if (ImGui::Button("Reset Crosshatch Settings")) {
            //            ResetCrosshatchSettings();
            //        }
            //        ImGui::Spacing(); ImGui::Spacing();
            //    }

            //    // New: noise texture selection
            //    if (!availableNoiseTextures.empty() && crosshatchMode != 4)
            //    {
            //        // Automatically update currentNoiseIndex based on the instance's noise texture.
            //        bool found = false;
            //        for (int i = 0; i < (int)availableNoiseTextures.size(); i++)
            //        {
            //            if (availableNoiseTextures[i].handle.idx == noiseTexture.idx)
            //            {
            //                globalCurrentNoiseIndex = i;
            //                found = true;
            //                break;
            //            }
            //        }
            //        if (!found)
            //        {
            //            // If the instance doesn't have a valid noise texture, default to index 0.
            //            globalCurrentNoiseIndex = 0;
            //            noiseTexture = availableNoiseTextures[0].handle;
            //        }

            //        // Build an array of c-strings from the names in availableNoiseTextures
            //        std::vector<const char*> noiseNames;
            //        noiseNames.reserve(availableNoiseTextures.size());
            //        for (auto& n : availableNoiseTextures)
            //        {
            //            noiseNames.push_back(n.name.c_str());
            //        }

            //        ImGui::Spacing(); ImGui::Spacing();
            //        ImGui::Separator();
            //        ImGui::Spacing(); ImGui::Spacing();
            //        // Let user pick which noise texture to use
            //        if (ImGui::Combo("Noise Pattern", &globalCurrentNoiseIndex, noiseNames.data(), (int)noiseNames.size()))
            //        {
            //            noiseTexture = availableNoiseTextures[globalCurrentNoiseIndex].handle;
            //        }

            //        ImGui::Text("Noise Texture Preview:");
            //        if (bgfx::isValid(noiseTexture))
            //        {
            //            ImTextureID noiseID = (ImTextureID)(uintptr_t)(noiseTexture.idx);
            //            ImGui::Image(noiseID, ImVec2(256, 256));
            //        }
            //        else
            //        {
            //            ImGui::Text("No valid noise texture selected.");
            //        }
            //    }
            //}
            //else if (selectedInstance && selectedInstance->isLight == false) {
            //    const char* modeItems[] = { "Crosshatch Ver 1.0", "Crosshatch Ver 1.1", "Crosshatch Ver 1.2", "Crosshatch Ver 1.3", "Simple Lighting" };
            //    ImGui::Combo("Shader Mode", &selectedInstance->crosshatchMode, modeItems, IM_ARRAYSIZE(modeItems));
            //    ImGui::Spacing(); ImGui::Spacing();
            //    // --- Show controls depending on the mode ---
            //    if (selectedInstance->crosshatchMode == 0)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.0 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", selectedInstance->inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Smoothness", &selectedInstance->epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Density", &selectedInstance->strokeMultiplier, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Primary Hatch Angle", &selectedInstance->lineAngle1, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Secondary Hatch Angle", &selectedInstance->lineAngle2, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Scale", &selectedInstance->patternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Weight", &selectedInstance->lineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &selectedInstance->transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (selectedInstance->crosshatchMode == 1)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.1 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", selectedInstance->inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Smoothness", &selectedInstance->epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Density", &selectedInstance->strokeMultiplier, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Angle", &selectedInstance->lineAngle1, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Scale", &selectedInstance->patternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Line Weight", &selectedInstance->lineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &selectedInstance->transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (selectedInstance->crosshatchMode == 2)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.2 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", selectedInstance->inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Line Smoothness", &selectedInstance->epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Density", &selectedInstance->strokeMultiplier, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Angle", &selectedInstance->lineAngle1, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Scale", &selectedInstance->patternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Weight", &selectedInstance->lineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        // Inner layer settings:
            //        ImGui::DragFloat("Inner Hatch Scale", &selectedInstance->layerPatternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Density", &selectedInstance->layerStrokeMult, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Angle", &selectedInstance->layerAngle, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Weight", &selectedInstance->layerLineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &selectedInstance->transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (selectedInstance->crosshatchMode == 3)
            //    {
            //        ImGui::Text("Crosshatch Ver 1.3 Settings:");
            //        ImGui::ColorEdit4("Hatch Color", selectedInstance->inkColor);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Line Smoothness", &selectedInstance->epsilonValue, 0.001f, 0.0f, 0.1f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Density", &selectedInstance->strokeMultiplier, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Angle", &selectedInstance->lineAngle1, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Scale", &selectedInstance->patternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Outer Hatch Weight", &selectedInstance->lineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        // Inner layer settings:
            //        ImGui::DragFloat("Inner Hatch Scale", &selectedInstance->layerPatternScale, 0.1f, 0.1f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Density", &selectedInstance->layerStrokeMult, 0.1f, 0.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Angle", &selectedInstance->layerAngle, 0.1f, 0.0f, TAU);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Inner Hatch Weight", &selectedInstance->layerLineThickness, 0.1f, -10.0f, 10.0f);
            //        ImGui::SetNextItemWidth(100);
            //        ImGui::DragFloat("Hatch Opacity", &selectedInstance->transparencyValue, 0.01f, 0.0f, 1.0f);
            //    }
            //    else if (selectedInstance->crosshatchMode == 4)
            //    {
            //        ImGui::Text("Simple Lighting (No Crosshatch)");
            //    }

            //    // New: noise texture selection
            //    if (!availableNoiseTextures.empty() && selectedInstance->crosshatchMode != 4)
            //    {
            //        // Automatically update currentNoiseIndex based on the instance's noise texture.
            //        bool found = false;
            //        for (int i = 0; i < (int)availableNoiseTextures.size(); i++)
            //        {
            //            if (availableNoiseTextures[i].handle.idx == selectedInstance->noiseTexture.idx)
            //            {
            //                currentNoiseIndex = i;
            //                found = true;
            //                break;
            //            }
            //        }
            //        if (!found)
            //        {
            //            // If the instance doesn't have a valid noise texture, default to index 0.
            //            currentNoiseIndex = 0;
            //            selectedInstance->noiseTexture = availableNoiseTextures[0].handle;
            //        }

            //        // Build an array of c-strings from the names in availableNoiseTextures
            //        std::vector<const char*> noiseNames;
            //        noiseNames.reserve(availableNoiseTextures.size());
            //        for (auto& n : availableNoiseTextures)
            //        {
            //            noiseNames.push_back(n.name.c_str());
            //        }

            //        ImGui::Spacing(); ImGui::Spacing();
            //        ImGui::Separator();
            //        ImGui::Spacing(); ImGui::Spacing();
            //        // Let user pick which noise texture to use
            //        if (ImGui::Combo("Noise Pattern", &currentNoiseIndex, noiseNames.data(), (int)noiseNames.size()))
            //        {
            //            selectedInstance->noiseTexture = availableNoiseTextures[currentNoiseIndex].handle;
            //        }

            //        ImGui::Text("Noise Texture Preview:");
            //        if (bgfx::isValid(selectedInstance->noiseTexture))
            //        {
            //            ImTextureID noiseID = (ImTextureID)(uintptr_t)(selectedInstance->noiseTexture.idx);
            //            ImGui::Image(noiseID, ImVec2(256, 256));
            //        }
            //        else
            //        {
            //            ImGui::Text("No valid noise texture selected.");
            //        }
            //    }
            //}
            //ImGui::End();
			//ImGui::Begin("Crosshatch Shader Settings");
            // Add a new window for camera settings
            if (show_CameraSettings)
            {
            ImGui::Begin("Camera Settings", &show_CameraSettings, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::SetWindowFontScale(0.85f);
            Camera& activeCamera = cameras[currentCameraIndex];
            // Basic camera controls
            ImGui::SliderFloat("Movement Speed", &activeCamera.movementSpeed, 0.1f, 20.0f);
            ImGui::SliderFloat("Mouse Sensitivity", &activeCamera.mouseSensitivity, 0.01f, 1.0f);
            ImGui::Checkbox("Constrain Pitch", &activeCamera.constrainPitch);

            // View settings
            ImGui::Separator();
            ImGui::SliderFloat("Field of View", &activeCamera.fov, 30.0f, 120.0f);
            ImGui::SliderFloat("Near Clip", &activeCamera.nearClip, 0.01f, 10.0f);
            ImGui::SliderFloat("Far Clip", &activeCamera.farClip, 100.0f, 5000.0f);

            // Position/orientation info
            ImGui::Separator();
            ImGui::Text("Position: %.2f, %.2f, %.2f", activeCamera.position.x, activeCamera.position.y, activeCamera.position.z);
            ImGui::Text("Rotation: Yaw %.2fÂ°, Pitch %.2fÂ°", activeCamera.yaw, activeCamera.pitch);

            static int current_preset = 0;
            const char* presets[] = { "Default", "Wide Angle", "Telephoto" };
            if (ImGui::Combo("Preset", &current_preset, presets, IM_ARRAYSIZE(presets))) {
                switch (current_preset) {
                case 0: // Default
                    activeCamera.fov = 60.f;
                    activeCamera.nearClip = 0.1f;
                    activeCamera.farClip = 1000.f;
                    break;
                case 1: // Wide Angle
                    activeCamera.fov = 90.f;
                    activeCamera.nearClip = 0.5f;
                    activeCamera.farClip = 500.f;
                    break;
                case 2: // Telephoto
                    activeCamera.fov = 30.f;
                    activeCamera.nearClip = 1.0f;
                    activeCamera.farClip = 2000.f;
                    break;
                }
            }
            // Reset button
            if (ImGui::Button("Reset Camera")) {
                activeCamera = Camera(); // Reset to defaults
            }

            ImGui::End();
            }
            
            if (show_Cameras)
            {
            ImGui::Begin("Cameras", &show_Cameras, window_flags);
            for (size_t i = 0; i < cameras.size(); i++) {
                char label[64];
                sprintf(label, "Camera %d", static_cast<int>(i));
                if (ImGui::Selectable(label, (int)i == currentCameraIndex)) {
                    currentCameraIndex = static_cast<int>(i);
                }
            }
            if (ImGui::Button("Create New Camera")) {
                createNewCamera();
            }
            ImGui::End();
            }
            //ImGui::Render();
            //ImGui_Implbgfx_RenderDrawLists(ImGui::GetDrawData());
        }

        // Always call these last â€” after all ImGui windows
        ImGui::Render();
        ImGui_Implbgfx_RenderDrawLists(ImGui::GetDrawData());

        //handle inputs
        Camera& activeCamera = cameras[currentCameraIndex];

        int width = static_cast<int>(viewport->Size.x);
        int height = static_cast<int>(viewport->Size.y);
        // 3D viewport = dedicated window rect (LichtFeld-style)
        int view3DWidth = static_cast<int>(g_ViewportRectW);
        int view3DHeight = static_cast<int>(g_ViewportRectH);
        if (view3DWidth < 1) view3DWidth = 1;
        if (view3DHeight < 1) view3DHeight = 1;
        int mouseX_input = static_cast<int>(InputManager::getMouseX());
        int mouseY_input = static_cast<int>(InputManager::getMouseY());
        bool mouseIn3DArea = (mouseX_input >= (int)g_ViewportRectX && mouseX_input < (int)(g_ViewportRectX + g_ViewportRectW)
            && mouseY_input >= (int)g_ViewportRectY && mouseY_input < (int)(g_ViewportRectY + g_ViewportRectH));

        //Don't process movement input unless user is in the actual 3D editor.
        //Also pause camera controls while using any ImGuizmo (transform or view gizmo)
        //so they don't fight each other.
        if (!showMainMenu && !showCreditsPage)
        {
            bool gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (!gizmoActive)
            {
                InputManager::update(activeCamera, 0.016f, mouseIn3DArea);
            }

            if (InputManager::isKeyToggled(GLFW_KEY_F2))
            {
                takingScreenshot = !takingScreenshot;
            }

            if (InputManager::isKeyToggled(GLFW_KEY_F3))
            {
                takeScreenshotAsPng(BGFX_INVALID_HANDLE, screenshotName);
            }
        }

        if (width == 0 || height == 0)
        {
            continue;
        }

        // --- Object Picking Pass ---
        // Only when in editor (past start menu), left click, not over UI, and mouse in 3D content area (exclude right sidebar).
        // Skip picking when the mouse is over or using the gizmo so we don't change selection while interacting with it.
        if (!showMainMenu && !showCreditsPage)
        {
            bool skipPickingForGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            if (InputManager::isMouseClicked(GLFW_MOUSE_BUTTON_LEFT) && mouseIn3DArea && !skipPickingForGizmo)
            {
                if (InputManager::getSkipPickingPass) {
                    // Use a dedicated view ID for picking (choose one not used by your normal rendering)
                    const uint32_t PICKING_VIEW_ID = 0;
                    bgfx::setViewFrameBuffer(PICKING_VIEW_ID, s_pickingFB);
                    bgfx::setViewRect(PICKING_VIEW_ID, 0, 0, PICKING_DIM, PICKING_DIM);

                    // Use the same camera for the picking pass.
                    Camera& activeCamera = cameras[currentCameraIndex];
                    float view[16];
                    bx::mtxLookAt(view, activeCamera.position, bx::add(activeCamera.position, activeCamera.front), activeCamera.up);
                    float proj[16];
                    bx::mtxProj(proj, activeCamera.fov, float(view3DWidth) / float(view3DHeight), activeCamera.nearClip, activeCamera.farClip, bgfx::getCaps()->homogeneousDepth);
                    bgfx::setViewTransform(PICKING_VIEW_ID, view, proj);

                    // Render each instance with the picking shader.
                    for (const Instance* instance : instances)
                    {
                        renderInstancePickingRecursive(instance, nullptr, PICKING_VIEW_ID);
                    }

                    // Blit the picking render target to the CPU-readable texture.
                    const uint32_t PICKING_BLIT_VIEW = 2;
                    bgfx::blit(PICKING_BLIT_VIEW, s_pickingReadTex, 0, 0, s_pickingRT);
                    // Submit a frame to ensure the blit is complete.
                    bgfx::frame();

                    // Read back the texture data into s_pickingBlitData.
                    bgfx::readTexture(s_pickingReadTex, s_pickingBlitData);

                    // Detach the picking framebuffer by setting it to BGFX_INVALID_HANDLE.
                    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
                    // Reset the viewport to the 3D viewport window rect (view 1).
                    bgfx::setViewRect(1, (uint16_t)g_ViewportRectX, (uint16_t)g_ViewportRectY, (uint16_t)view3DWidth, (uint16_t)view3DHeight);
                    // Reset the view transforms for your normal scene.
                    bgfx::setViewTransform(1, view, proj);
                    InputManager::toggleSkipPickingPass();
                }
                // Use a dedicated view ID for picking (choose one not used by your normal rendering)
                const uint32_t PICKING_VIEW_ID = 0;
                bgfx::setViewFrameBuffer(PICKING_VIEW_ID, s_pickingFB);
                bgfx::setViewRect(PICKING_VIEW_ID, 0, 0, PICKING_DIM, PICKING_DIM);

                // Use the same camera for the picking pass.
                Camera& activeCamera = cameras[currentCameraIndex];
                float view[16];
                bx::mtxLookAt(view, activeCamera.position, bx::add(activeCamera.position, activeCamera.front), activeCamera.up);
                float proj[16];
                bx::mtxProj(proj, activeCamera.fov, float(view3DWidth) / float(view3DHeight), activeCamera.nearClip, activeCamera.farClip, bgfx::getCaps()->homogeneousDepth);
                bgfx::setViewTransform(PICKING_VIEW_ID, view, proj);

                // Render each instance with the picking shader.
                for (const Instance* instance : instances)
                {
                    renderInstancePickingRecursive(instance, nullptr, PICKING_VIEW_ID);
                }

                // Blit the picking render target to the CPU-readable texture.
                const uint32_t PICKING_BLIT_VIEW = 2;
                bgfx::blit(PICKING_BLIT_VIEW, s_pickingReadTex, 0, 0, s_pickingRT);
                // Submit a frame to ensure the blit is complete.
                bgfx::frame();

                // Read back the texture data into s_pickingBlitData.
                bgfx::readTexture(s_pickingReadTex, s_pickingBlitData);

                // Convert mouse to viewport-window-local coords for picking.
                float mouseLocalX = (float)(mouseX_input - (int)g_ViewportRectX);
                float mouseLocalY = (float)(mouseY_input - (int)g_ViewportRectY);
                int mouseY_flip = view3DHeight - (int)mouseLocalY;
                int pickX = (int)(mouseLocalX * (float)PICKING_DIM / (float)view3DWidth);
                int pickY = (mouseY_flip * PICKING_DIM) / view3DHeight;

                // Clamp the coordinates.
                pickX = std::max(0, std::min(pickX, PICKING_DIM - 1));
                pickY = std::max(0, std::min(pickY, PICKING_DIM - 1));

                // Read the pixel (RGBA8: 4 bytes per pixel)
                int pixelIndex = (pickY * PICKING_DIM + pickX) * 4;
                uint8_t r = s_pickingBlitData[pixelIndex + 0];
                uint8_t g = s_pickingBlitData[pixelIndex + 1];
                uint8_t b = s_pickingBlitData[pixelIndex + 2];

                // Decode the ID from the red channel.
                uint32_t pickedID = (r << 16) | (g << 8) | b;

                // Search through instances to find the one with this ID.
                Instance* pickedInstance = findInstanceById(instances, pickedID);
                if (pickedInstance)
                {
                    if (selectedInstance == pickedInstance) {
                        selectedInstance = nullptr;
                    }
                    else {
                        selectedInstance = pickedInstance;
                        std::cout << "Picked object: " << selectedInstance->name << std::endl;
                    }
                }
                // Detach the picking framebuffer by setting it to BGFX_INVALID_HANDLE.
                bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
                // Reset the viewport to the 3D viewport window rect (view 1).
                bgfx::setViewRect(1, (uint16_t)g_ViewportRectX, (uint16_t)g_ViewportRectY, (uint16_t)view3DWidth, (uint16_t)view3DHeight);
                // Reset the view transforms for your normal scene.
                bgfx::setViewTransform(1, view, proj);
            }
        }

        //if (InputManager::isKeyToggled(GLFW_KEY_BACKSPACE) && !instances.empty())
        //{
        //    Instance* inst = instances.back();
        //    instances.pop_back();
        //    //instanceCounter--;
        //    delete inst;
        //    std::cout << "Last Instance removed" << std::endl;
        //}

        float lightsData[MAX_LIGHTS * 16]; // 16 floats per light.
        int numLights = 0;
        for (const Instance* inst : instances) {
            if (numLights >= MAX_LIGHTS)
                break;
            collectLights(inst, lightsData, numLights);
        }
        // Set u_lights uniform with (numLights * 4) vec4's.
        bgfx::setUniform(u_lights, lightsData, numLights * 4);
        float numLightsArr[4] = { static_cast<float>(numLights), 0, 0, 0 };
        bgfx::setUniform(u_numLights, numLightsArr);

        bgfx::reset(width, height, BGFX_RESET_VSYNC);
        // View 0: full-screen dark background so the 3D viewport window shape (rounded corners) is visible
        bgfx::setViewRect(0, 0, 0, (uint16_t)width, (uint16_t)height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, g_AppBackgroundColor, 1.0f, 0);
        bgfx::touch(0);

        // View 1: 3D scene only in the viewport window rect (LichtFeld-style, rounded window)
        bgfx::setViewRect(1, (uint16_t)g_ViewportRectX, (uint16_t)g_ViewportRectY, (uint16_t)view3DWidth, (uint16_t)view3DHeight);

        float view[16];
        bx::mtxLookAt(view, activeCamera.position, bx::add(activeCamera.position, activeCamera.front), activeCamera.up);

        float proj[16];
        bx::mtxProj(proj, activeCamera.fov, float(view3DWidth) / float(view3DHeight), activeCamera.nearClip, activeCamera.farClip, bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(1, view, proj);

        // Set model matrix
        float mtx[16];
        bx::mtxSRT(mtx, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        bgfx::setTransform(mtx);

        bgfx::setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, g_ViewportClearColor, 1.0f, 0);

        bgfx::touch(1);

        // World reference helpers (draw first so scene objects appear above in depth):
        // - Red: X axis
        // - Green: Y axis
        // - Blue: Z axis
        // - Grid: XZ plane (camera-relative "infinite" tiles)
        // Prefer unlit vertex-color program so axes/grid are bright and mostly
        // independent from crosshatch shading, but fall back to defaultProgram
        // if the unlit shaders are not available.
        const float tintBasic[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
        bgfx::setUniform(u_tint, tintBasic);
        bgfx::ProgramHandle gridProgram =
            bgfx::isValid(unlitColorProgram) ? unlitColorProgram : defaultProgram;
        DrawWorldAxesAndGrid(1, activeCamera, gridProgram);


        float viewPos[4] = { camera.position.x, camera.position.y, camera.position.z, 1.0f };

        //bgfx::setUniform(u_lightDir, lightDir);
        //bgfx::setUniform(u_lightColor, lightColor);

        bgfx::setUniform(u_viewPos, viewPos);

        float cameraPos[4] = { cameras[currentCameraIndex].position.x, cameras[currentCameraIndex].position.y, cameras[currentCameraIndex].position.z, 1.0f };
        float epsilon[4] = { 0.02f, 0.0f, 0.0f, 0.0f }; // Pack the epsilon value into the first element; the other three can be 0.

        bgfx::setUniform(u_inkColor, inkColor);
        bgfx::setUniform(u_cameraPos, cameraPos);
        // Set epsilon uniform:
        float epsilonUniform[4] = { epsilonValue, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(u_e, epsilonUniform);
        //bgfx::setTexture(0, u_noiseTex, availableNoiseTextures[currentNoiseIndex].handle); // Bind the noise texture to texture

        // Prepare an array of 4 floats.
        // Set u_params uniform:
        float paramsUniform[4] = { 0.0f, strokeMultiplier, lineAngle1, lineAngle2 };
        bgfx::setUniform(u_params, paramsUniform);

        // Prepare an array of 4 floats.
        float extraParamsUniform[4] = { patternScale, lineThickness, transparencyValue, float(crosshatchMode) };
        // Set the uniform for extra parameters.
        bgfx::setUniform(u_extraParams, extraParamsUniform);


        // Prepare an array of 4 floats.
        float paramsLayerUniform[4] = { layerPatternScale, layerStrokeMult, layerAngle, layerLineThickness };
        // Set the uniform for extra parameters.
        bgfx::setUniform(u_paramsLayer, paramsLayerUniform);

        // Enable stats or debug text
        bgfx::setDebug(s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);

        // Calculate delta time
        static float lastFrameTime = 0.0f;
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;
        // Update rotating lights
        updateRotatingLights(instances, deltaTime);

        // tintBasic is already defined and set before drawing the grid (to prevent grid from inheriting highlight tints)
        bgfx::submit(1, defaultProgram);

        for (const auto& instance : instances)
        {
            float model[16];
            //bx::mtxTranslate(model, instance.position[0], instance.position[1], instance.position[2]);

            // Build a Scale-Rotate-Translate matrix.
            // Note: bx::mtxSRT expects parameters in the order:
            // (result, scaleX, scaleY, scaleZ, rotX, rotY, rotZ, transX, transY, transZ)
            bx::mtxSRT(model,
                instance->scale[0], instance->scale[1], instance->scale[2],
                instance->rotation[0], instance->rotation[1], instance->rotation[2],
                instance->position[0], instance->position[1], instance->position[2]);

            bgfx::setTransform(model);

            // 1) Build the uvTransform (tilingU, tilingV, offsetU, offsetV).
            float uvTransform[4] =
            {
                instance->material.tiling[0],
                instance->material.tiling[1],
                instance->material.offset[0],
                instance->material.offset[1]
            };
            bgfx::setUniform(u_uvTransform, uvTransform);

            // 2) Albedo factor
            //    (r, g, b, a)
            // bgfx::setUniform(u_albedoFactor, instance->material.albedo);

            if (instance->isLight && instance->lightAnim.enabled) {
                float time = static_cast<float>(glfwGetTime());
                // Update each axis (x, y, z) with a sine-based offset.
                for (int i = 0; i < 3; i++) {
                    instance->position[i] = instance->basePosition[i] +
                        instance->lightAnim.amplitude[i] * sin(time * instance->lightAnim.frequency[i] + instance->lightAnim.phase[i]);
                }
            }

            drawInstance(instance, defaultProgram, lightDebugProgram, textProgram, comicProgram, u_comicColor, u_noiseTex, u_diffuseTex, u_objectColor, u_tint, u_inkColor, u_e, u_params, u_extraParams, u_paramsLayer, defaultWhiteTexture, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, instance->objectColor); // your usual shader program
        }

        // Update your vertex layout to include normals
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float) // NEW: UV coordinates
            .end();



        bx::mtxSRT(mtx, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        bgfx::setTransform(mtx);

        // End frame

        bgfx::frame();


    }
    // Save current window visibility state so it persists between sessions
    SaveWindowVisibilityConfig("window_visibility.cfg");

    for (const auto& instance : instances)
    {
        const bgfx::VertexBufferHandle invalidVbh = BGFX_INVALID_HANDLE;
        const bgfx::IndexBufferHandle invalidIbh = BGFX_INVALID_HANDLE;
        // Destroy the vertex and index buffers if they are valid.
        if (instance->vertexBuffer.idx != invalidVbh.idx &&
            instance->indexBuffer.idx != invalidIbh.idx)
        {
            bgfx::destroy(instance->vertexBuffer);
            bgfx::destroy(instance->indexBuffer);
        }
        deleteInstance(instance);
    }

    bgfx::destroy(vbh_plane);
    bgfx::destroy(ibh_plane);
    bgfx::destroy(vbh_sphere);
    bgfx::destroy(ibh_sphere);
    bgfx::destroy(vbh_cube);
    bgfx::destroy(ibh_cube);
    bgfx::destroy(vbh_capsule);
    bgfx::destroy(ibh_capsule);
    bgfx::destroy(vbh_cylinder);
    bgfx::destroy(ibh_cylinder);
    bgfx::destroy(vbh_mesh);
    bgfx::destroy(ibh_mesh);
    bgfx::destroy(vbh_cornell);
    bgfx::destroy(ibh_cornell);
    bgfx::destroy(vbh_innerCube);
    bgfx::destroy(ibh_innerCube);
    bgfx::destroy(defaultProgram);
    bgfx::destroy(lightDebugProgram);
    ImGui_ImplGlfw_Shutdown();

    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
} 
