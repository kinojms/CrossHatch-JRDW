#include "ModelingTools.h"
#include <imgui.h>
#include <map>
#include <algorithm>
#include <cmath>
#include <bx/math.h>

// Helper to get midpoint for Subdivision
uint32_t ModelingTools::getMidpoint(uint32_t p1, uint32_t p2,
    std::vector<PosColorVertex>& verts,
    std::map<std::pair<uint32_t, uint32_t>, uint32_t>& cache) {
    // Ensure consistent order for key
    std::pair<uint32_t, uint32_t> edge = (p1 < p2) ? std::make_pair(p1, p2) : std::make_pair(p2, p1);

    if (cache.find(edge) != cache.end()) {
        return cache[edge];
    }

    // Create new vertex
    PosColorVertex v1 = verts[p1];
    PosColorVertex v2 = verts[p2];
    PosColorVertex mid;

    // Linear interpolation of attributes
    mid.x = (v1.x + v2.x) * 0.5f;
    mid.y = (v1.y + v2.y) * 0.5f;
    mid.z = (v1.z + v2.z) * 0.5f;
    mid.nx = (v1.nx + v2.nx) * 0.5f; // Note: Should re-normalize later
    mid.ny = (v1.ny + v2.ny) * 0.5f;
    mid.nz = (v1.nz + v2.nz) * 0.5f;
    mid.u = (v1.u + v2.u) * 0.5f;
    mid.v = (v1.v + v2.v) * 0.5f;
    mid.abgr = v1.abgr; // Simple copy color

    // Add to list
    uint32_t index = (uint32_t)verts.size();
    verts.push_back(mid);
    cache[edge] = index;
    return index;
}

void ModelingTools::Subdivide(Instance* inst) {
    if (!inst) return;

    MeshData newMesh;
    newMesh.vertices = inst->cpuMesh.vertices; // Start with existing verts
    // Indices will be rebuilt entirely
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpointCache;

    const auto& oldInds = inst->cpuMesh.indices;

    // Iterate over existing triangles
    for (size_t i = 0; i < oldInds.size(); i += 3) {
        uint32_t i0 = oldInds[i];
        uint32_t i1 = oldInds[i + 1];
        uint32_t i2 = oldInds[i + 2];

        uint32_t a = getMidpoint(i0, i1, newMesh.vertices, midpointCache);
        uint32_t b = getMidpoint(i1, i2, newMesh.vertices, midpointCache);
        uint32_t c = getMidpoint(i2, i0, newMesh.vertices, midpointCache);

        // 4 new triangles
        // T1
        newMesh.indices.push_back(i0); newMesh.indices.push_back(a); newMesh.indices.push_back(c);
        // T2
        newMesh.indices.push_back(i1); newMesh.indices.push_back(b); newMesh.indices.push_back(a);
        // T3
        newMesh.indices.push_back(i2); newMesh.indices.push_back(c); newMesh.indices.push_back(b);
        // T4 (Center)
        newMesh.indices.push_back(a);  newMesh.indices.push_back(b); newMesh.indices.push_back(c);
    }

    // Recompute normals for better look
    computeNormals(newMesh.vertices, newMesh.indices);

    // Command History
    // We execute manually here to update the pointer, then push to stack
    inst->cpuMesh = newMesh;
    inst->updateGPU();
}

void ModelingTools::Smooth(Instance* inst, float intensity, int iterations) {
    if (!inst) return;

    MeshData currentMesh = inst->cpuMesh;

    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<PosColorVertex> smoothedVerts = currentMesh.vertices;
        std::vector<int> neighborCount(currentMesh.vertices.size(), 0);
        std::vector<bx::Vec3> accumulatedPos(currentMesh.vertices.size(), { 0,0,0 });

        // Accumulate neighbor positions
        for (size_t i = 0; i < currentMesh.indices.size(); i += 3) {
            uint32_t idx[3] = { currentMesh.indices[i], currentMesh.indices[i + 1], currentMesh.indices[i + 2] };

            for (int j = 0; j < 3; ++j) {
                uint32_t me = idx[j];
                uint32_t next = idx[(j + 1) % 3];
                uint32_t prev = idx[(j + 2) % 3];

                // Add neighbors
                accumulatedPos[me] = bx::add(accumulatedPos[me], { currentMesh.vertices[next].x, currentMesh.vertices[next].y, currentMesh.vertices[next].z });
                accumulatedPos[me] = bx::add(accumulatedPos[me], { currentMesh.vertices[prev].x, currentMesh.vertices[prev].y, currentMesh.vertices[prev].z });
                neighborCount[me] += 2;
            }
        }

        // Apply
        for (size_t i = 0; i < smoothedVerts.size(); ++i) {
            if (neighborCount[i] > 0) {
                bx::Vec3 avg = bx::mul(accumulatedPos[i], 1.0f / neighborCount[i]);
                bx::Vec3 original = { currentMesh.vertices[i].x, currentMesh.vertices[i].y, currentMesh.vertices[i].z };
                bx::Vec3 result = bx::lerp(original, avg, intensity);

                smoothedVerts[i].x = result.x;
                smoothedVerts[i].y = result.y;
                smoothedVerts[i].z = result.z;
            }
        }
        currentMesh.vertices = smoothedVerts;
    }

    computeNormals(currentMesh.vertices, currentMesh.indices);
    inst->cpuMesh = currentMesh;
    inst->updateGPU();
}

void ModelingTools::MergeVertices(Instance* inst, float threshold) {
    if (!inst) return;

    MeshData newMesh;
    std::vector<uint32_t> remap(inst->cpuMesh.vertices.size());

    // Simple brute force O(N^2) for simplicity. 
    // In production, use Octree or Spatial Hash.

    for (size_t i = 0; i < inst->cpuMesh.vertices.size(); ++i) {
        bool found = false;
        PosColorVertex& v = inst->cpuMesh.vertices[i];

        // Check against already added vertices
        for (size_t j = 0; j < newMesh.vertices.size(); ++j) {
            PosColorVertex& existing = newMesh.vertices[j];
            float distSq = bx::length(bx::sub(
                bx::Vec3{ v.x, v.y, v.z },
                bx::Vec3{ existing.x, existing.y, existing.z }
            )); // Note: bx::length(vec) isn't squared, careful. Using dist directly.

            // Manual dist sq
            float dx = v.x - existing.x;
            float dy = v.y - existing.y;
            float dz = v.z - existing.z;
            float dSq = dx * dx + dy * dy + dz * dz;

            if (dSq < threshold * threshold) {
                remap[i] = (uint32_t)j;
                found = true;
                break;
            }
        }

        if (!found) {
            remap[i] = (uint32_t)newMesh.vertices.size();
            newMesh.vertices.push_back(v);
        }
    }

    // Remap indices
    for (uint32_t idx : inst->cpuMesh.indices) {
        newMesh.indices.push_back(remap[idx]);
    }

    inst->cpuMesh = newMesh;
    inst->updateGPU();
}

void ModelingTools::MorphToSphere(Instance* inst, float factor) {
    if (!inst) return;

    // Calculate center
    float cx = 0, cy = 0, cz = 0;
    for (auto& v : inst->cpuMesh.vertices) {
        cx += v.x; cy += v.y; cz += v.z;
    }
    cx /= inst->cpuMesh.vertices.size();
    cy /= inst->cpuMesh.vertices.size();
    cz /= inst->cpuMesh.vertices.size();

    // Calculate average radius
    float avgRadius = 0;
    for (auto& v : inst->cpuMesh.vertices) {
        float dx = v.x - cx;
        float dy = v.y - cy;
        float dz = v.z - cz;
        avgRadius += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    avgRadius /= inst->cpuMesh.vertices.size();

    // Apply morph
    for (auto& v : inst->cpuMesh.vertices) {
        float dx = v.x - cx;
        float dy = v.y - cy;
        float dz = v.z - cz;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (len > 0.0001f) {
            // Project to sphere surface
            float nx = dx / len;
            float ny = dy / len;
            float nz = dz / len;

            float targetX = cx + nx * avgRadius;
            float targetY = cy + ny * avgRadius;
            float targetZ = cz + nz * avgRadius;

            // Lerp
            v.x = v.x + (targetX - v.x) * factor;
            v.y = v.y + (targetY - v.y) * factor;
            v.z = v.z + (targetZ - v.z) * factor;
        }
    }

    computeNormals(inst->cpuMesh.vertices, inst->cpuMesh.indices);
    inst->updateGPU();
}

void ModelingTools::RenderPanel(Instance* selectedInst, CommandManager* cmdManager) {
    ImGui::Begin("Modeling Tools");

    if (selectedInst) {
        // --- Shortcuts Handling ---
        // CTRL+D for Subdivide
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            MeshData old = selectedInst->cpuMesh;
            Subdivide(selectedInst);
            cmdManager->executeCommand(std::make_unique<MeshEditCommand>(selectedInst, old, selectedInst->cpuMesh));
        }

        // --- UI Buttons ---

        if (ImGui::Button("Subdivide (Ctrl+D)")) {
            MeshData old = selectedInst->cpuMesh;
            Subdivide(selectedInst);
            cmdManager->executeCommand(std::make_unique<MeshEditCommand>(selectedInst, old, selectedInst->cpuMesh));
        }

        ImGui::Separator();

        static float smoothInt = 0.5f;
        static int smoothIter = 1;
        ImGui::SliderFloat("Smoothness", &smoothInt, 0.0f, 1.0f);
        ImGui::SliderInt("Iterations", &smoothIter, 1, 5);
        if (ImGui::Button("Smooth Mesh")) {
            MeshData old = selectedInst->cpuMesh;
            Smooth(selectedInst, smoothInt, smoothIter);
            cmdManager->executeCommand(std::make_unique<MeshEditCommand>(selectedInst, old, selectedInst->cpuMesh));
        }

        ImGui::Separator();

        static float mergeDist = 0.01f;
        ImGui::DragFloat("Merge Dist", &mergeDist, 0.001f, 0.0001f, 1.0f, "%.4f");
        if (ImGui::Button("Merge Vertices")) {
            MeshData old = selectedInst->cpuMesh;
            MergeVertices(selectedInst, mergeDist);
            cmdManager->executeCommand(std::make_unique<MeshEditCommand>(selectedInst, old, selectedInst->cpuMesh));
        }

        ImGui::Separator();

        static float morphFactor = 0.5f;
        ImGui::SliderFloat("Sphere Factor", &morphFactor, 0.0f, 1.0f);
        if (ImGui::Button("Morph to Sphere")) {
            MeshData old = selectedInst->cpuMesh;
            MorphToSphere(selectedInst, morphFactor);
            cmdManager->executeCommand(std::make_unique<MeshEditCommand>(selectedInst, old, selectedInst->cpuMesh));
        }
    }
    else {
        ImGui::TextDisabled("Select an object to edit.");
    }

    ImGui::Separator();

    // --- Undo/Redo Buttons ---
    // Shortcuts: Ctrl+Z / Ctrl+Y
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (cmdManager->canUndo()) cmdManager->undo();
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        if (cmdManager->canRedo()) cmdManager->redo();
    }

    if (ImGui::Button("Undo (Ctrl+Z)")) {
        if (cmdManager->canUndo()) cmdManager->undo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) {
        if (cmdManager->canRedo()) cmdManager->redo();
    }

    ImGui::End();
}