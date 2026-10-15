#include "editor_core/SceneDocument.h"
#include <cmath>
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
        else if (kind != "Triangle") return ResultType::failure("unknown mesh (use Cube or Triangle)");
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
        object.tint = math::Vec4(numbers[7], numbers[8], numbers[9], 1.0f);
        result.push_back(object);
    }
    return ResultType::success(std::move(result));
}
}
