#include "renderer/BuiltinGeometry.h"
#include <cmath>
namespace nova::renderer {
namespace {
using math::Vec3;
void triangle(GeometryAtlas& a, Vec3 p, Vec3 q, Vec3 r, Vec3 color) {
    const auto normal = glm::normalize(glm::cross(q - p, r - p));
    const float shade = 0.45f + 0.55f * std::abs(glm::dot(normal, glm::normalize(Vec3(-0.4f, 0.85f, 0.3f))));
    color *= shade;
    for (const auto v : {p, q, r})
        a.vertices.insert(a.vertices.end(), {v.x, v.y, v.z, color.x, color.y, color.z});
}
void box(GeometryAtlas& a, Vec3 center, Vec3 size, Vec3 color) {
    const Vec3 v[] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    const int faces[][4] = {{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{3,7,6,2},{0,1,5,4}};
    for (const auto& f : faces) {
        auto point = [&](int i) { return center + v[i] * size * 0.5f; };
        triangle(a, point(f[0]), point(f[1]), point(f[2]), color);
        triangle(a, point(f[0]), point(f[2]), point(f[3]), color);
    }
}
void cone(GeometryAtlas& a, float y, float radius, float height, Vec3 color, int sides = 10) {
    for (int i = 0; i < sides; ++i) {
        const float t = i * math::kPi * 2 / sides, u = (i + 1) * math::kPi * 2 / sides;
        const Vec3 p(radius * std::cos(t), y, radius * std::sin(t));
        const Vec3 q(radius * std::cos(u), y, radius * std::sin(u));
        triangle(a, p, Vec3(0,y+height,0), q, color);
        triangle(a, p, q, Vec3(0,y,0), color);
    }
}
}
std::vector<float> defaultTerrain() {
    std::vector<float> heights;
    for (int z = 0; z < kTerrainSide; ++z) for (int x = 0; x < kTerrainSide; ++x) {
        const float px = (x - 8) * 0.75f, pz = (z - 8) * 0.75f;
        heights.push_back(0.9f * (1 - (px*px+pz*pz)/30) + 0.18f*std::sin(px*1.3f)*std::cos(pz) - 0.15f);
    }
    return heights;
}
GeometryAtlas buildBuiltinGeometry(const std::vector<float>& input) {
    GeometryAtlas a;
    auto begin = [&](BuiltinMesh mesh) { a.ranges[static_cast<int>(mesh)].first = static_cast<u32>(a.vertices.size()/6); };
    auto end = [&](BuiltinMesh mesh) { auto& r=a.ranges[static_cast<int>(mesh)]; r.count=static_cast<u32>(a.vertices.size()/6)-r.first; };
    begin(BuiltinMesh::kTriangle);
    triangle(a, Vec3(0,.75f,0), Vec3(-.75f,-.65f,0), Vec3(.75f,-.65f,0), Vec3(.65f,.8f,1));
    end(BuiltinMesh::kTriangle);
    begin(BuiltinMesh::kCube); box(a, Vec3(0), Vec3(1), Vec3(.8f,.88f,1)); end(BuiltinMesh::kCube);
    begin(BuiltinMesh::kPlane);
    triangle(a, Vec3(-.5f,0,-.5f),Vec3(-.5f,0,.5f),Vec3(.5f,0,.5f),Vec3(1));
    triangle(a, Vec3(-.5f,0,-.5f),Vec3(.5f,0,.5f),Vec3(.5f,0,-.5f),Vec3(1));
    end(BuiltinMesh::kPlane);
    begin(BuiltinMesh::kCone); cone(a,-.5f,.5f,1,Vec3(1)); end(BuiltinMesh::kCone);
    begin(BuiltinMesh::kSphere);
    for (int j=0;j<8;++j) for(int i=0;i<12;++i) {
        auto p=[&](int x,int y) { const float lat=-math::kPi/2+y*math::kPi/8, lon=x*math::kPi/6;
            return Vec3(std::cos(lat)*std::cos(lon),std::sin(lat),std::cos(lat)*std::sin(lon))*.5f; };
        if(j>0) triangle(a,p(i,j),p(i+1,j),p(i,j+1),Vec3(1));
        if(j<7) triangle(a,p(i+1,j),p(i+1,j+1),p(i,j+1),Vec3(1));
    }
    end(BuiltinMesh::kSphere);
    begin(BuiltinMesh::kTree);
    box(a,Vec3(0,.6f,0),Vec3(.18f,1.2f,.18f),Vec3(.37f,.21f,.11f));
    cone(a,.5f,.65f,1.5f,Vec3(.17f,.48f,.24f)); cone(a,1.2f,.45f,1.2f,Vec3(.23f,.6f,.3f));
    end(BuiltinMesh::kTree);
    begin(BuiltinMesh::kHouse);
    box(a,Vec3(0,.4f,0),Vec3(1,.8f,.9f),Vec3(.8f,.62f,.37f));
    box(a,Vec3(0,.25f,.456f),Vec3(.23f,.5f,.02f),Vec3(.2f,.15f,.1f));
    for(float x : {-.32f,.32f}) box(a,Vec3(x,.5f,.456f),Vec3(.18f,.2f,.02f),Vec3(.3f,.75f,.9f));
    const Vec3 roof(.5f,.24f,.12f);
    triangle(a,Vec3(-.65f,.8f,.6f),Vec3(.65f,.8f,.6f),Vec3(0,1.4f,.6f),roof);
    triangle(a,Vec3(-.65f,.8f,-.6f),Vec3(0,1.4f,-.6f),Vec3(.65f,.8f,-.6f),roof);
    triangle(a,Vec3(-.65f,.8f,.6f),Vec3(0,1.4f,.6f),Vec3(0,1.4f,-.6f),roof);
    triangle(a,Vec3(-.65f,.8f,.6f),Vec3(0,1.4f,-.6f),Vec3(-.65f,.8f,-.6f),roof);
    triangle(a,Vec3(.65f,.8f,.6f),Vec3(.65f,.8f,-.6f),Vec3(0,1.4f,-.6f),roof);
    triangle(a,Vec3(.65f,.8f,.6f),Vec3(0,1.4f,-.6f),Vec3(0,1.4f,.6f),roof);
    end(BuiltinMesh::kHouse);
    begin(BuiltinMesh::kTerrain);
    const auto h = input.size()==kTerrainSamples ? input : defaultTerrain();
    auto point=[&](int x,int z) { return Vec3((x-8)*.75f,h[z*kTerrainSide+x],(z-8)*.75f); };
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const float t = h[z*kTerrainSide+x];
        Vec3 color = t < .08f ? Vec3(.75f,.68f,.43f) : Vec3(.27f,.48f,.23f);
        if(t>1.4f) color=Vec3(.58f,.62f,.61f);
        triangle(a,point(x,z),point(x,z+1),point(x+1,z+1),color);
        triangle(a,point(x,z),point(x+1,z+1),point(x+1,z),color);
    }
    end(BuiltinMesh::kTerrain);
    return a;
}
float hitMesh(const GeometryAtlas& atlas, BuiltinMesh mesh, math::Vec3 origin, math::Vec3 direction) {
    const auto index=static_cast<usize>(mesh);
    if(index>=atlas.ranges.size()) return -1;
    const auto range=atlas.ranges[index]; float nearest=10000;
    auto point=[&](u32 vertex) { const auto p=static_cast<usize>(vertex)*6;return math::Vec3(atlas.vertices[p],atlas.vertices[p+1],atlas.vertices[p+2]); };
    for(u32 i=range.first;i<range.first+range.count;i+=3) {
        const auto a=point(i), e1=point(i+1)-a, e2=point(i+2)-a;
        const auto p=glm::cross(direction,e2); const float determinant=glm::dot(e1,p);
        if(std::abs(determinant)<1e-7f) continue;
        const float inverse=1/determinant;const auto t=origin-a;
        const float u=glm::dot(t,p)*inverse;if(u<0 || u>1)continue;
        const auto q=glm::cross(t,e1);const float v=glm::dot(direction,q)*inverse;if(v<0 || u+v>1)continue;
        const float distance=glm::dot(e2,q)*inverse;if(distance>=0 && distance<nearest)nearest=distance;
    }
    return nearest<10000 ? nearest : -1;
}
scene::Bounds builtinBounds(BuiltinMesh mesh) {
    if(mesh==BuiltinMesh::kTerrain) return {math::Vec3(0),math::Vec3(6,3,6)};
    if(mesh==BuiltinMesh::kTree) return {math::Vec3(0,1.2f,0),math::Vec3(.7f,1.2f,.7f)};
    if(mesh==BuiltinMesh::kHouse) return {math::Vec3(0,.7f,0),math::Vec3(.7f,.7f,.7f)};
    return {math::Vec3(0),math::Vec3(.75f)};
}
}
