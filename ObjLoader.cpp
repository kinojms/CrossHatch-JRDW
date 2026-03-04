#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <array>
#include <algorithm>
#include <cmath>

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
};

struct Vec4 {
    float x, y, z, w;
};

// Helper function to convert RGB float values (0.0-1.0) to ABGR uint32_t format
static uint32_t rgbToAbgr(float r, float g, float b, float a = 1.0f) {
    // Clamp values to [0, 1] range
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));
    a = std::max(0.0f, std::min(1.0f, a));
    
    // Convert to 0-255 range and pack as ABGR
    uint8_t r8 = static_cast<uint8_t>(r * 255.0f);
    uint8_t g8 = static_cast<uint8_t>(g * 255.0f);
    uint8_t b8 = static_cast<uint8_t>(b * 255.0f);
    uint8_t a8 = static_cast<uint8_t>(a * 255.0f);
    
    return (static_cast<uint32_t>(a8) << 24) | (static_cast<uint32_t>(b8) << 16) | 
           (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);
}

bool ObjLoader::loadObj(const std::string& filepath,
    std::vector<Vertex>& vertices,
    std::vector<uint16_t>& indices) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<Vec4> colors; // Store vertex colors (r, g, b, a)
    std::unordered_map<std::string, uint16_t> uniqueVertices;
    bool hasVertexColors = false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v") {
            Vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
            
            // Vertex color: 6 floats total for nerf/instant-ngp "v x y z r b g" (r,b,g order).
            // Also support common OBJ "v x y z r g b [a]".
            float c0, c1, c2, a = 1.0f;
            if (iss >> c0 >> c1 >> c2) {
                if (!(iss >> a)) a = 1.0f;
                // Nerf/instant-ngp custom format: file order is r b g -> store as (r,g,b) = (c0, c2, c1).
                float r = c0, g = c2, b = c1;
                colors.push_back({r, g, b, a});
                hasVertexColors = true;
            } else {
                colors.push_back({1.0f, 1.0f, 1.0f, 1.0f});
            }
        }
        else if (type == "vn") {
            Vec3 normal;
            iss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        else if (type == "vt") {
            Vec2 texCoord;
            iss >> texCoord.x >> texCoord.y;
            texCoords.push_back(texCoord);
        }
        else if (type == "f") {
            for (int i = 0; i < 3; ++i) {
                std::string vertexData;
                iss >> vertexData;

                if (uniqueVertices.count(vertexData) == 0) {
                    std::istringstream viss(vertexData);
                    std::string indexStr;
                    int indices[3] = { 0, 0, 0 };
                    int j = 0;
                    while (std::getline(viss, indexStr, '/')) {
                        if (!indexStr.empty()) {
                            indices[j] = std::stoi(indexStr) - 1;
                        }
                        ++j;
                    }

                    Vertex vertex;
                    vertex.x = positions[indices[0]].x;
                    vertex.y = positions[indices[0]].y;
                    vertex.z = positions[indices[0]].z;

                    if (indices[1] < texCoords.size()) {
                        vertex.u = texCoords[indices[1]].x;
                        vertex.v = texCoords[indices[1]].y;
                    }
                    else {
                        vertex.u = 0.0f;
                        vertex.v = 0.0f;
                    }

                    if (indices[2] < normals.size()) {
                        vertex.nx = normals[indices[2]].x;
                        vertex.ny = normals[indices[2]].y;
                        vertex.nz = normals[indices[2]].z;
                    }
                    else {
                        vertex.nx = 0.0f;
                        vertex.ny = 1.0f;
                        vertex.nz = 0.0f;
                    }

                    // Set vertex color
                    if (indices[0] < colors.size()) {
                        Vec4 color = colors[indices[0]];
                        vertex.abgr = rgbToAbgr(color.x, color.y, color.z, color.w);
                    }
                    else {
                        // Default to white if no color available
                        vertex.abgr = rgbToAbgr(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    uniqueVertices[vertexData] = static_cast<uint16_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertexData]);
            }
            std::swap(indices[indices.size() - 2], indices[indices.size() - 1]);
        }
    }

    if (normals.empty()) {
        computeNormals(vertices, indices);
    }

    return true;
}

void ObjLoader::computeNormals(std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices) {
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint16_t i0 = indices[i];
        uint16_t i1 = indices[i + 1];
        uint16_t i2 = indices[i + 2];

        Vec3 v0 = { vertices[i0].x, vertices[i0].y, vertices[i0].z };
        Vec3 v1 = { vertices[i1].x, vertices[i1].y, vertices[i1].z };
        Vec3 v2 = { vertices[i2].x, vertices[i2].y, vertices[i2].z };

        Vec3 edge1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
        Vec3 edge2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
        Vec3 normal = {
            edge1.y * edge2.z - edge1.z * edge2.y,
            edge1.z * edge2.x - edge1.x * edge2.z,
            edge1.x * edge2.y - edge1.y * edge2.x
        };

        // Normalize the normal
        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }

		normal.x *= -1.0f;
		normal.y *= -1.0f;
		normal.z *= -1.0f;

        vertices[i0].nx += normal.x; vertices[i0].ny += normal.y; vertices[i0].nz += normal.z;
        vertices[i1].nx += normal.x; vertices[i1].ny += normal.y; vertices[i1].nz += normal.z;
        vertices[i2].nx += normal.x; vertices[i2].ny += normal.y; vertices[i2].nz += normal.z;
    }

    for (auto& vertex : vertices) {
        float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
        if (length > 0) {
            vertex.nx /= length;
            vertex.ny /= length;
            vertex.nz /= length;
        }
    }
}

bgfx::VertexBufferHandle ObjLoader::createVertexBuffer(const std::vector<Vertex>& vertices) {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, true) // ABGR format, normalized
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    return bgfx::createVertexBuffer(
        bgfx::makeRef(vertices.data(), sizeof(Vertex) * vertices.size()),
        layout
    );
}

bgfx::IndexBufferHandle ObjLoader::createIndexBuffer(const std::vector<uint16_t>& indices) {
    return bgfx::createIndexBuffer(
        bgfx::makeRef(indices.data(), sizeof(uint16_t) * indices.size())
    );
}