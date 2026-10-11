package com.nova.engine

import android.content.res.AssetManager
import android.view.Surface

/**
 * JNI facade for the Nova Engine native library (libnova.so).
 *
 * Contract (see app/src/main/cpp/jni_bridge.cpp):
 *  - Keep JNI thin: only primitives, strings, and framework objects cross the
 *    boundary; all engine state lives on the native side behind one mutex.
 *  - [nativeInit] must be called exactly once per process lifetime before any
 *    other entry point, and [nativeShutdown] exactly once during teardown.
 *  - [nativeFrame] may be called from a dedicated render thread; lifecycle and
 *    input calls are made from the main thread. The native engine serializes
 *    both sides internally.
 *
 * Thread-safety: all functions are safe to call from any thread that already
 * has a JNI environment (i.e. any Kotlin thread).
 */
object NativeBridge {
    /** [nativeFrame] rendered and presented a frame. */
    const val FRAME_RENDERED = 0

    /** [nativeFrame] skipped (no surface / swapchain being recreated). */
    const val FRAME_SKIPPED = 1

    init {
        System.loadLibrary("nova")
    }

    /**
     * Initialize the native engine.
     *
     * @param assetManager APK asset manager used to read shaders and assets.
     * @param dataPath     app-private writable directory (logs, caches).
     * @param width        initial surface width in pixels.
     * @param height       initial surface height in pixels.
     * @param enableValidation request the Vulkan validation layer (debug).
     * @return 0 on success, non-zero error code otherwise.
     */
    external fun nativeInit(
        assetManager: AssetManager,
        dataPath: String,
        width: Int,
        height: Int,
        enableValidation: Boolean,
    ): Int

    /** Hand a Surface to the engine (null releases the previous one). */
    external fun nativeSetSurface(surface: Surface?)

    /** Notify the engine of a surface size change (rotation, resize). */
    external fun nativeResize(width: Int, height: Int)

    /**
     * Render one frame.
     *
     * @param dt seconds elapsed since the previous frame (clamped natively).
     * @return [FRAME_RENDERED] or [FRAME_SKIPPED].
     */
    external fun nativeFrame(dt: Float): Int

    /** Forward a touch event (MotionEvent action, pointer id, pixel coords). */
    external fun nativeOnTouch(action: Int, pointerId: Int, x: Float, y: Float)

    /** Forward a key/gamepad button event (Android keycode). */
    external fun nativeOnKey(keyCode: Int, down: Boolean)

    /** Lifecycle: application went to background. */
    external fun nativeOnPause()

    /** Lifecycle: application returned to foreground. */
    external fun nativeOnResume()

    /** Tear down the native engine. Called once, after the render thread stops. */
    external fun nativeShutdown()

    /** Execute an engine console command; returns the command output. */
    external fun nativeExecConsole(cmd: String): String
}
