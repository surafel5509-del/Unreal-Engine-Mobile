# Nova Studio 0.2.0 — Android editor foundation

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

## Working tools

- Native ECS + scene hierarchy/culling/draw list, not a Canvas imitation.
- Cube and triangle primitives, with depth testing and vertex colors/tint.
- Hierarchy selection, position XYZ, Euler rotation XYZ (degrees), uniform
  scale 0.05–10, five tint presets. Press **Apply** after editing numbers.
- Drag the selected object in viewport XY; camera zoom uses +/−.
- Add, duplicate and delete; 30-entry undo/redo history.
- Play/Stop previews rotation; stopping restores authored transforms.
- Inspector/hierarchy beside the viewport on wide screens; tabbed bottom dock
  on phones. Assets lists only the two meshes that are actually available.
- Console: `help`, `stats`, `graphics`, `camera <1.5..20>`, `play`, `stop`,
  `time`, `log`. GLES pacing stays FIFO; the legacy `vsync` command cannot
  disable this backend's pacing.
- Explicit Save/Load (`files/editor/scene.json`) and background recovery save
  (`files/editor/recovery.json`). Both use Android AtomicFile.

Scenes currently have a 32-object limit. Saved files are app-private and are
removed by uninstall/clear-data. There is no file picker or external export UI
in this release. Developers can back up a debug build's scene using:

```bash
adb exec-out run-as com.nova.engine cat files/editor/scene.json > scene.json
```

Debug keys can change between fresh CI runners. An incompatible-signature
update requires uninstalling the previous build; back up before doing so.
Do not treat the CI debug APK as a production-signed release.

## Native/Android contract

`nativeSetScene` accepts version-1 JSON, at most 64 KiB. The native parser
validates every object, known mesh type, numeric value/range and object count
**before** acquiring the engine mutex and replacing the scene. Malformed
input cannot partially destroy the previous scene. Kotlin retains the editable
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
Tests require visible colored cube pixels in a SurfaceView screenshot, advancing native frames, the expected GLES context, working
add/transform/duplicate/delete/undo/redo/play/save controls, invalid-scene rejection, background/resume,
and Activity recreation with restored objects. Shader/link/GL errors fail CI.
The workflow uploads test output, logcat and a real editor screenshot. Publication
is gated on both emulator jobs. These software-rendered tests do not replace
ARM/OEM device testing, context-loss fault injection or GPU performance testing.

## Not implemented

No glTF/FBX import, textures, PBR lighting, shadows, skeletal animation, physics,
audio, gameplay scripting, networking, arbitrary component inspector, asset
browser/import pipeline, multi-scene projects, external scene export, or game
APK export/build tools. Play is a rotating-scene preview, not a gameplay runtime.
This is a small engine/editor foundation, not a Unity-equivalent product.
