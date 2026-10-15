#pragma once
#include "core/Math.h"
#include "scene/Culling.h"
#include <algorithm>
#include <cmath>
namespace nova::editor {
// Slab intersection. Transform the world ray into object-local space without
// normalizing it: returned t remains comparable across nonuniform scales.
inline float hitBounds(math::Vec3 origin, math::Vec3 direction, const scene::Bounds& box) {
    float nearT = 0.0f, farT = 10000.0f;
    for (int axis=0;axis<3;++axis) {
        const float lo=box.center[axis]-box.halfExtent[axis], hi=box.center[axis]+box.halfExtent[axis];
        if(std::abs(direction[axis])<1e-7f) { if(origin[axis]<lo || origin[axis]>hi) return -1; }
        else {
            float a=(lo-origin[axis])/direction[axis], b=(hi-origin[axis])/direction[axis];
            if(a>b) std::swap(a,b);
            nearT=std::max(nearT,a); farT=std::min(farT,b);
            if(nearT>farT) return -1;
        }
    }
    return nearT;
}
}
