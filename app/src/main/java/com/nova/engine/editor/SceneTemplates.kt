package com.nova.engine.editor

import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.*

object SceneTemplates {
    val meshes = listOf("Cube", "Sphere", "Plane", "Cone", "Tree", "House", "Terrain", "Triangle")
    fun objectOf(kind: String, name: String = kind): JSONObject = JSONObject().apply {
        put("kind", kind); put("name", name); put("enabled", true); put("spin", false)
        for (k in listOf("x", "y", "z", "rx", "ry", "rz")) put(k, 0.0)
        put("scale", 1.0); put("r", 1.0); put("g", 1.0); put("b", 1.0)
    }
    fun camera(yaw: Double = 30.0, pitch: Double = 28.0, distance: Double = 9.0) = JSONObject().apply {
        put("yaw", yaw); put("pitch", pitch); put("distance", distance)
        put("x", 0.0); put("y", 0.3); put("z", 0.0)
    }
    fun blank() = JSONObject().put("version", 1).put("name", "Untitled").put("objects", JSONArray())
        .put("camera", camera(0.0, 10.0, 4.0))
    fun height(x: Double, z: Double) = 0.9 * (1 - (x*x + z*z)/30) + 0.18 * sin(x*1.3) * cos(z) - 0.15
    fun heights() = JSONArray().apply { for (z in 0..16) for (x in 0..16) put(height((x-8)*.75, (z-8)*.75)) }
    fun island(): JSONObject {
        val objects = JSONArray()
        objects.put(objectOf("Terrain", "Highland terrain"))
        objects.put(objectOf("Plane", "Lake water").put("sx", 10.0).put("sy", 1.0).put("sz", 10.0)
            .put("y", 0.03).put("r", .22).put("g", .65).put("b", .92))
        val trees = listOf(-3.1 to -1.2, -2.5 to 1.2, -3.3 to 2.1, -1.8 to -3.1, -.6 to -3.5,
            1.0 to -3.3, 2.5 to -2.0, 3.1 to -.6, 2.9 to 1.7, -3.4 to -.4, -1.9 to 2.6, 1.6 to 2.8)
        trees.forEachIndexed { i, (x,z) -> objects.put(objectOf("Tree", "Pine ${i+1}").put("x",x).put("z",z)
            .put("y",height(x,z)).put("scale",.48+(i%3)*.12)) }
        listOf(-1.2 to -.3, .5 to -1.0, 1.5 to .4).forEachIndexed { i,(x,z) ->
            objects.put(objectOf("House", "Cabin ${i+1}").put("x",x).put("z",z).put("y",height(x,z)).put("ry",-20.0+i*35))
        }
        listOf(-4.0 to -2.2, 3.3 to -2.8, 2.0 to 3.6, -2.8 to 3.4, 4.0 to .5).forEachIndexed { i,(x,z) ->
            objects.put(objectOf("Sphere", "Rock ${i+1}").put("x",x).put("z",z).put("y",height(x,z)+.15)
                .put("sx",1.0).put("sy",.55).put("sz",.75).put("r",.65).put("g",.69).put("b",.72))
        }
        return JSONObject().put("version",1).put("name","Emerald Valley").put("objects",objects)
            .put("camera",camera()).put("terrain",heights())
    }
}
