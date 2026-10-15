#include <doctest.h>
#include "editor_core/SceneDocument.h"
namespace {
const std::string object = R"({"kind":"Cube","x":0,"y":0,"z":0,"rx":20,"ry":30,"rz":0,"scale":1,"r":1,"g":0.5,"b":0})";
std::string scene(const std::string& objects) { return "{\"version\":1,\"objects\":[" + objects + "]}"; }
}
TEST_CASE("Editor scene validates before applying") {
    auto parsed = nova::editor::parseScene(scene(object));
    REQUIRE(parsed);
    REQUIRE(parsed.value().size() == 1);
    CHECK(parsed.value()[0].mesh == nova::renderer::BuiltinMesh::kCube);
    CHECK(parsed.value()[0].tint.y == doctest::Approx(0.5));
    CHECK(nova::editor::parseScene(scene("")));
    CHECK_FALSE(nova::editor::parseScene("{}"));
    CHECK_FALSE(nova::editor::parseScene("{\"version\":1,\"objects\":[null]}"));
    CHECK_FALSE(nova::editor::parseScene("{\"version\":2,\"objects\":[]}"));
}
TEST_CASE("Editor rejects invalid transforms and unsupported meshes") {
    for (auto replacement : {std::string("\"scale\":0"), std::string("\"scale\":11"), std::string("\"scale\":1e200"), std::string("\"scale\":\"bad\"")}) {
        std::string value = object;
        value.replace(value.find("\"scale\":1"), 9, replacement);
        CHECK_FALSE(nova::editor::parseScene(scene(value)));
    }
    auto value = object;
    value.replace(value.find("Cube"), 4, "Unknown");
    CHECK_FALSE(nova::editor::parseScene(scene(value)));
}
TEST_CASE("Editor enforces document and render budgets") {
    std::string objects;
    for (int i = 0; i < 32; ++i) { if (i) objects += ','; objects += object; }
    CHECK(nova::editor::parseScene(scene(objects)));
    CHECK_FALSE(nova::editor::parseScene(scene(objects + ',' + object)));
    CHECK_FALSE(nova::editor::parseScene(std::string(65537, ' ')));
}
