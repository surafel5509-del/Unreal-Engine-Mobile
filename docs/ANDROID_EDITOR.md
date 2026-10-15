# Nova Studio 0.3.0 — Landscape Android editor

## Compatibility policy

| Android | Requested context | Fallback |
| --- | --- | --- |
| 5.0–7.1 / API 21–25 | EGL OpenGL ES 2 | Clear startup error if no usable ES 2 driver |
| 8.0+ / API 26+ | EGL OpenGL ES 3 | Retry with ES 2 config/context |

The APK contains armeabi-v7a, arm64-v8a, x86 and x86_64, uses minimum SDK 21,
and does **not** link libvulkan.so. Native ELF load segments are 16 KiB aligned;
libraries use extracted packaging to avoid older AGP ZIP-mapping restrictions. The GLES backend links only EGL/GLESv2
platform libraries for graphics; the ES 3 context can execute the common ES 2
API and GLSL ES 1.00 shaders. There are no ES 3-only rendering effects yet.

OS version is a preference, not a capability test. The actual EGL config and
context creation decide which version works. Display/driver failures are still
possible. No promise of support for every device, driver, iOS, or Android <5.

## Working tools and workflow

- **Workspace:** sensor-landscape, dark navy/blue vector-icon interface. Drag the
  hierarchy/viewport, viewport/inspector and bottom-dock dividers. Horizontal
  toolbars and mesh cards scroll; hierarchy, inspector and asset folders scroll
  vertically. Settings resets panel sizes. Game mode hides side/bottom panels.
- **World:** native depth-tested geometry, not a Canvas scene or pre-rendered
  backdrop. The default 22-object valley contains a 17×17 terrain, flat water,
  12 procedural trees, three cabins and five scaled rock spheres. It is a small
  low-poly scene, not a streamed/photorealistic open world.
- **Meshes:** Cube, Triangle, Plane, Cone, Sphere, Tree, House and Terrain. The
  asset browser adds these, lists saved scenes/prefabs/scripts, and has no pretend
  texture/model assets. Shapes share a bounded geometry atlas and baked vertex
  shading; there are no dynamic lights or shadows.
- **Selection/transforms:** tap visible mesh triangles (native ray tests), or a
  hierarchy row. Eye toggles visibility. Move/Rotate/Scale drags affect the
  selected object, with Free/X/Y/Z constraint buttons. These are toolbar tools,
  not 3D on-object gizmos/outlines. Inspector position XYZ, Euler degrees XYZ,
  independent scale XYZ (0.05–10), name and material tint have working controls.
  Press Apply after numeric edits. Duplicate/delete and 30 snapshots of undo/redo.
- **Camera:** Orbit drag, pinch zoom, zoom buttons, Frame selection and camera
  reset via the hierarchy's Main Camera entry. Game's virtual pad pans the camera;
  it does not control an animated character or perform collisions.
- **Terrain:** select Terrain in the hierarchy. Raise/Lower edits the center
  heightfield brush; Smooth averages neighboring samples; Reset restores the
  initial heights. All changes support undo/redo. This is not a free-position
  paint/sculpt brush. Terrain objects share one heightfield per scene.
- **Play:** opt-in per-object rotation preview; Stop restores authored transforms.
  No gameplay simulation, animation timeline, skeletal clips or physics.
- **Projects/scenes:** tap the project title or Settings → Project manager. Create
  projects; Assets → Scenes → + creates empty or valley scenes. Selecting a scene
  saves the current scene first. Inspector → Save as prefab makes a reusable
  single-object JSON prefab. Scene budgets: 32 objects, 64 KiB; project budgets:
  16 projects, 16 scenes/project, 32 prefabs/project, 16 scripts/project.
- **Console/scripts:** native console command batches, not Lua/C#/gameplay code.
  Scripts are limited to 8,192 characters and 64 executable commands per run.
  Available commands include `help`, `stats`, `graphics`, `scene`, `play`, `stop`,
  `orbit yaw pitch distance targetX targetY targetZ`, `pick normalizedX normalizedY`,
  `view scene|game`, `log`, `time`. Low-level console camera/view commands do not
  synchronize all authoring metadata; prefer the workspace controls for saved work.
- **Diagnostics:** live FPS, native elapsed frame time, draw/scene statistics,
  console, build-validation report, selectable 30/60 FPS caps. Low-memory callbacks
  trim history/logs and cap at 30 FPS. These are counters, not a GPU capture profiler.

### Persistence, import/export and installation

Save and recovery use `AtomicFile` beneath:

```
files/studio/projects/<uuid>/
  project.json
  scenes/<name>.json
  recovery/<name>.json
  prefabs/<name>.json
  scripts/<name>.json
```

Backgrounding saves recovery; dirty scenes also checkpoint every 30 seconds.
On startup the newest recovery is tried, with the explicit save as fallback.
If both fail, originals are preserved and a warning explains the temporary scene;
explicit Save creates a separate recovery scene instead of overwriting originals.
Version 0.2 scene/recovery files are migrated without deleting the old files.

**Build → Export project JSON** uses Android's document picker; no broad storage
permission is required. Export first saves the active scene, then bundles saved
scenes, prefabs and scripts. **Import** accepts a Nova project bundle or a single
version-1 scene. Size limit: 512 KiB. Imported names/objects are validated before
staging and installing a new UUID project; arbitrary filesystem paths are rejected.
This does not import glTF, OBJ, FBX, images or audio.

**JSON export is not game APK export.** The Build menu offers a validation report
and a link to the repository's GitHub Gradle/NDK workflow. It neither submits a
build automatically nor compiles your exported scene into a standalone game.
There is no Android compiler/SDK bundled on the phone.

Uninstall/clear-data removes app-private projects. Export backups first. Debug
signing keys can change on fresh CI runners, requiring uninstall before an update.
The CI APK is not a production-signed store release. An older 0.2 debug build can
be backed up before migration/uninstall with:

```bash
adb exec-out run-as com.nova.engine cat files/editor/scene.json > scene.json
```

### Modular layout

- `EditorDocument`: authored JSON, validation/application, selection and history.
- `ProjectStore`: bounded atomic storage, migration, project bundles and staging.
- `SceneTemplates`: small procedural project/scene defaults.
- `EditorUi` / `SplitWorkspace`: vector chrome and persistent-surface panel layout.
- `EditorView`: connects controls, document operations and camera/touch gestures.
- `MainActivity`: Android lifecycle and document-provider IO.
- Native `SceneDocument`, `BuiltinGeometry`, `Picking`, `Renderer`, RHI: validation,
  meshes, triangle picking, scene draw lists and graphics implementation.

## Native/Android contract

`nativeSetScene` accepts version-1 JSON, at most 64 KiB. The native parser
validates every object, known mesh type, numeric value/range and object count
**before** acquiring the engine mutex and replacing the scene. Malformed
input cannot partially destroy the previous scene. `nativeValidateScene` performs the same pure checks without altering the live scene.
Kotlin retains the editable
JSON and undo snapshots; validated transforms/tints become native ECS components.

The `Engine` mutex serializes the UI control calls and render frames. EGL is
made current at beginFrame and released at endFrame, so the UI thread can
safely attach/detach windows or tear down after the render thread stops. The
JNI layer owns the ANativeWindow reference. Surface recreation retains CPU
resources. Context loss drops GL names and lazily recompiles programs from
retained sources. Android pause parks the render loop; native destruction
happens only after thread join.

The renderer keeps the existing Vulkan clip convention for culling. The GLES
vertex shader unflips Y and maps Z from [0, w] to [-w, w]. GLES emulates the
small frame uniform slices using ordinary uniforms; no ES 3 UBO requirement.
The background uses an explicit fullscreen triangle VBO instead of gl_VertexID.
The built-in atlas is retained in a static GLES VBO and reuploaded only after
terrain changes or context loss, rather than streamed separately for every object.
The same SurfaceView remains attached during panel resizing and ordinary window
configuration changes; Android surface size callbacks update native projection.

## Build and tests

```bash
./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug
cmake -S . -B build/host -G Ninja -DNOVA_BUILD_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 -m unittest discover -s tools/tests -v
```

GitHub Actions builds all four ABIs, checks ELF architecture and absence of
Vulkan linkage, asserts SDK 21, verifies signing/alignment, then runs actual
installation and instrumentation on API 21/x86 and API 26/x86_64 emulators.
Tests require visible colored world pixels in a SurfaceView screenshot, advancing
native frames, the expected GLES context, landscape configuration, working
add/transform/duplicate/delete/undo/redo/terrain/game/play/save controls, project
bundle round-trip and traversal rejection, prefab/script persistence, invalid-scene
rejection, background/resume and Activity recreation with restored objects. Shader/link/GL errors fail CI.
The workflow uploads test output, logcat and a real editor screenshot. Publication
is gated on both emulator jobs. These software-rendered tests do not replace
ARM/OEM device testing, context-loss fault injection or GPU performance testing.

## Not implemented

No glTF/OBJ/FBX import, textures/PBR, editable lights/shadows, skeletal animation,
rigid-body physics/collision simulation, character controller, audio, particles,
UI designer, gameplay scripting, networking, arbitrary component inspector,
streaming open-world system, or standalone-game/on-device APK export.
The optional Vulkan backend is source-only in this release, not a selectable APK
renderer. A camera pad is not a character controller and rotation preview is not
a general animation/gameplay system. This is a small engine/editor foundation,
not a complete Unity/Godot-equivalent product.
