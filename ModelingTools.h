#pragma once
#include "CrossHatchEditor.h" // Access to Instance and MeshData
#include <vector>

class ModelingTools {
public:
    // --- The Operations ---

    // Splits every triangle into 4 smaller triangles
    static void Subdivide(Instance* inst);

    // Moves vertices closer to the average of their neighbors
    static void Smooth(Instance* inst, float intensity = 0.5f, int iterations = 1);

    // Merges vertices that are within a specific distance of each other
    static void MergeVertices(Instance* inst, float threshold = 0.001f);

    // Morphs the mesh towards a sphere shape (Cast Modifier style)
    static void MorphToSphere(Instance* inst, float factor);

    // --- UI & Input ---
    // Call this inside your ImGui render loop
    static void RenderPanel(Instance* selectedInst, CommandManager* cmdManager);

private:
    // Helper to calculate edge midpoints for subdivision
    static uint32_t getMidpoint(uint32_t p1, uint32_t p2,
        std::vector<PosColorVertex>& verts,
        std::map<std::pair<uint32_t, uint32_t>, uint32_t>& cache);
};

// --- Undo/Redo Command for Mesh Operations ---
class MeshEditCommand : public ICommand {
    Instance* inst;
    MeshData oldMesh;
    MeshData newMesh;
public:
    MeshEditCommand(Instance* i, const MeshData& before, const MeshData& after)
        : inst(i), oldMesh(before), newMesh(after) {
    }

    void execute() override {
        inst->cpuMesh = newMesh;
        inst->updateGPU();
    }
    void undo() override {
        inst->cpuMesh = oldMesh;
        inst->updateGPU();
    }
};