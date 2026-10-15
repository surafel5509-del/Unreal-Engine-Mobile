#include <doctest.h>
#include "editor_core/SceneDocument.h"
namespace {
const std::string object = R"({"kind":"Cube","x":0,"y":0,"z":0,"rx":20,"ry":30,"rz":0,"scale":1,"r":1,"g":0.5,"b":0})";
std::string sceneJson(const std::string& objects) { return "{\"version\":1,\"objects\":[" + objects + "]}"; }
}
TEST_CASE("Editor scene validates before applying") {
    auto parsed = nova::editor::parseScene(sceneJson(object));
    REQUIRE(parsed);
    REQUIRE(parsed.value().size() == 1);
    CHECK(parsed.value()[0].mesh == nova::renderer::BuiltinMesh::kCube);
    CHECK(parsed.value()[0].tint.y == doctest::Approx(0.5));
    CHECK(nova::editor::parseScene(sceneJson("")));
    CHECK_FALSE(nova::editor::parseScene("{}"));
    CHECK_FALSE(nova::editor::parseScene("{\"version\":1,\"objects\":[null]}"));
    CHECK_FALSE(nova::editor::parseScene("{\"version\":2,\"objects\":[]}"));
}
TEST_CASE("Editor rejects invalid transforms and unsupported meshes") {
    for (auto replacement : {std::string("\"scale\":0"), std::string("\"scale\":11"), std::string("\"scale\":1e200"), std::string("\"scale\":\"bad\"")}) {
        std::string value = object;
        value.replace(value.find("\"scale\":1"), 9, replacement);
        CHECK_FALSE(nova::editor::parseScene(sceneJson(value)));
    }
    auto value = object;
    value.replace(value.find("Cube"), 4, "Unknown");
    CHECK_FALSE(nova::editor::parseScene(sceneJson(value)));
}
TEST_CASE("Editor enforces document and render budgets") {
    std::string objects;
    for (int i = 0; i < 32; ++i) { if (i) objects += ','; objects += object; }
    CHECK(nova::editor::parseScene(sceneJson(objects)));
    CHECK_FALSE(nova::editor::parseScene(sceneJson(objects + ',' + object)));
    CHECK_FALSE(nova::editor::parseScene(std::string(65537, ' ')));
}

#include "renderer/BuiltinGeometry.h"
#include "editor_core/Picking.h"
TEST_CASE("Built-in atlas is finite, bounded and pickable") {
    using namespace nova;
    const auto atlas=renderer::buildBuiltinGeometry(renderer::defaultTerrain());
    CHECK(atlas.vertices.size()<30000);
    for(float v:atlas.vertices) CHECK(std::isfinite(v));
    for(int i=1;i<=8;++i){CHECK(atlas.ranges[i].count>0);CHECK(atlas.ranges[i].count%3==0);CHECK((atlas.ranges[i].first+atlas.ranges[i].count)*6<=atlas.vertices.size());}
    CHECK(renderer::hitMesh(atlas,renderer::BuiltinMesh::kCube,math::Vec3(0,0,3),math::Vec3(0,0,-1))==doctest::Approx(2.5f));
    CHECK(renderer::hitMesh(atlas,renderer::BuiltinMesh::kCube,math::Vec3(2,0,3),math::Vec3(0,0,-1))<0);
    CHECK(editor::hitBounds(math::Vec3(0,0,3),math::Vec3(0,0,-1),scene::Bounds{})==doctest::Approx(2.5f));
    CHECK(editor::hitBounds(math::Vec3(2,0,3),math::Vec3(0,0,-1),scene::Bounds{})<0);
}
TEST_CASE("Terrain accepts bounded heights, rejects wrong size and outliers") {
    using namespace nova;
    CHECK(editor::parseTerrain(sceneJson(object)));
    CHECK_FALSE(editor::parseTerrain(R"({"terrain":[0,0]})"));
    std::string h="["; for(int i=0;i<289;++i){if(i)h+=",";h+="0";}h+="]";
    CHECK(editor::parseTerrain("{\"terrain\":"+h+"}"));
    h.replace(1,1,"4");CHECK_FALSE(editor::parseTerrain("{\"terrain\":"+h+"}"));
    auto hidden=object;hidden.insert(hidden.size()-1,",\"enabled\":false,\"spin\":true,\"sx\":2");
    auto parsed=editor::parseScene(sceneJson(hidden));REQUIRE(parsed);
    CHECK_FALSE(parsed.value()[0].enabled);CHECK(parsed.value()[0].spin);CHECK(parsed.value()[0].transform.scale.x==2);
    hidden.replace(hidden.find("\"sx\":2"),6,"\"sx\":0");CHECK_FALSE(editor::parseScene(sceneJson(hidden)));
}
