#pragma once
#include "renderer/DrawList.h"
#include <array>
namespace nova::renderer {
inline constexpr int kTerrainSide = 17;
inline constexpr int kTerrainSamples = kTerrainSide * kTerrainSide;
struct MeshRange { u32 first = 0; u32 count = 0; };
struct GeometryAtlas {
    std::vector<float> vertices; // position XYZ + vertex color RGB
    std::array<MeshRange, 9> ranges{};
};
std::vector<float> defaultTerrain();
GeometryAtlas buildBuiltinGeometry(const std::vector<float>& heights);
scene::Bounds builtinBounds(BuiltinMesh mesh);
float hitMesh(const GeometryAtlas& atlas, BuiltinMesh mesh, math::Vec3 origin, math::Vec3 direction);
}
