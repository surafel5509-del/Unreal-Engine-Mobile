# Editor scene document

`nova::editor::parseScene` validates the version-1 JSON scene format without
throwing exceptions or touching live engine state. It is compiled into both
host tests and the Android runtime. Parsing uses local state and is reentrant;
Engine applies a successful result while holding its scene/frame mutex.

Implemented: known primitive types, bounded finite transforms/tint, maximum
32 objects and 64 KiB input. Tests: `engine/tests/test_editor_scene.cpp`.

The Kotlin `EditorView` owns UI, JSON snapshots, undo/redo and AtomicFile
persistence. Future work includes multi-scene project/asset databases,
external import/export and a generalized component editor.
