#include "editor_core/SceneDocument.h"
#include <cmath>
#include "renderer/BuiltinGeometry.h"
namespace nova::editor {
Result<std::vector<Object>, std::string> parseScene(std::string_view text) {
    using ResultType = Result<std::vector<Object>, std::string>;
    if (text.size() > 65536) return ResultType::failure("scene exceeds 64 KiB");
    auto parsed = assets::parseJson(text);
    if (!parsed || !parsed.value().isObject()) return ResultType::failure("invalid scene JSON");
    const auto& root = parsed.value();
    if (!root.at("version").isNumber() || root.at("version").asNumber() != 1.0)
        return ResultType::failure("unsupported scene version");
    const auto& objects = root.at("objects");
    if (!objects.isArray() || objects.size() > 32) return ResultType::failure("scene must contain 0 to 32 objects");
    std::vector<Object> result;
    for (const auto& value : objects.asArray()) {
        if (!value.isObject()) return ResultType::failure("object expected");
        Object object;
        const auto& kind = value.at("kind").asString();
        if (kind == "Cube") object.mesh = renderer::BuiltinMesh::kCube;
        else if (kind == "Plane") object.mesh = renderer::BuiltinMesh::kPlane;
        else if (kind == "Cone") object.mesh = renderer::BuiltinMesh::kCone;
        else if (kind == "Sphere") object.mesh = renderer::BuiltinMesh::kSphere;
        else if (kind == "Tree") object.mesh = renderer::BuiltinMesh::kTree;
        else if (kind == "House") object.mesh = renderer::BuiltinMesh::kHouse;
        else if (kind == "Terrain") object.mesh = renderer::BuiltinMesh::kTerrain;
        else if (kind != "Triangle") return ResultType::failure("unknown built-in mesh");
        for (auto key : {"enabled", "spin"}) if(value.contains(key) && !value.at(key).isBool())
            return ResultType::failure("enabled/spin must be boolean");
        object.enabled = value.boolAt("enabled", true);
        object.spin = value.boolAt("spin", false);
        float numbers[10]{};
        const char* keys[] = {"x", "y", "z", "rx", "ry", "rz", "scale", "r", "g", "b"};
        for (int i = 0; i < 10; ++i) {
            const auto& number = value.at(keys[i]);
            if (!number.isNumber() || !std::isfinite(number.asNumber()) || std::abs(number.asNumber()) > 1000.0)
                return ResultType::failure(std::string("invalid field: ") + keys[i]);
            numbers[i] = number.asFloat();
        }
        if (numbers[6] < 0.05f || numbers[6] > 10.0f) return ResultType::failure("scale must be 0.05 to 10");
        for (int i = 7; i < 10; ++i)
            if (numbers[i] < 0 || numbers[i] > 1) return ResultType::failure("color must be 0 to 1");
        object.transform.position = math::Vec3(numbers[0], numbers[1], numbers[2]);
        object.transform.rotation = math::quatRotationZ(math::toRadians(numbers[5])) *
            math::quatRotationY(math::toRadians(numbers[4])) * math::quatRotationX(math::toRadians(numbers[3]));
        object.transform.scale = math::Vec3(numbers[6]);
        int axis=0;
        for (auto key : {"sx", "sy", "sz"}) {
            if(value.contains(key)) {
                const auto& v=value.at(key);
                if(!v.isNumber() || !std::isfinite(v.asNumber()) || v.asNumber()<0.05 || v.asNumber()>10)
                    return ResultType::failure("axis scale must be 0.05 to 10");
                object.transform.scale[axis]=v.asFloat();
            }
            ++axis;
        }
        object.tint = math::Vec4(numbers[7], numbers[8], numbers[9], 1.0f);
        result.push_back(object);
    }
    return ResultType::success(std::move(result));
}
Result<std::vector<float>, std::string> parseTerrain(std::string_view text) {
    using R=Result<std::vector<float>, std::string>;
    if(text.size()>65536) return R::failure("scene exceeds 64 KiB");
    auto json=assets::parseJson(text);
    if(!json) return R::failure("invalid scene JSON");
    const auto& h=json.value().at("terrain");
    if(h.isNull()) return R::success(renderer::defaultTerrain());
    if(!h.isArray() || h.size()!=renderer::kTerrainSamples) return R::failure("terrain requires 289 heights");
    std::vector<float> values;
    for(const auto& v:h.asArray()) {
        if(!v.isNumber() || !std::isfinite(v.asNumber()) || v.asNumber() < -3 || v.asNumber()>3)
            return R::failure("terrain heights must be finite and between -3 and 3");
        values.push_back(v.asFloat());
    }
    return R::success(std::move(values));
}
}
