#pragma once
#include "assets/Json.h"
#include "renderer/DrawList.h"
#include "scene/Transform.h"
namespace nova::editor {
struct Object {
    scene::Transform transform;
    renderer::BuiltinMesh mesh = renderer::BuiltinMesh::kTriangle;
    math::Vec4 tint{1.0f};
    bool enabled = true;
    bool spin = false;
};
// Parse before changing the live scene: invalid documents never partially apply.
Result<std::vector<Object>, std::string> parseScene(std::string_view json);
Result<std::vector<float>, std::string> parseTerrain(std::string_view json);
}
