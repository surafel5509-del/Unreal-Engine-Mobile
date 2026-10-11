package com.nova.engine

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import com.nova.engine.runtime.NovaRuntime

/**
 * Nova Engine host activity.
 *
 * Responsibilities:
 *  - configure immersive fullscreen + keep-screen-on,
 *  - materialize the writable asset tree and initialize the native engine,
 *  - wire [NovaSurfaceView] lifecycle to [NovaRuntime] and the native layer,
 *  - forward key/gamepad input to [InputManager].
 *
 * No runtime permissions are requested: the engine only reads its own APK
 * assets and writes to app-private storage (both permission-free on API 31+).
 * See docs/ARCHITECTURE.md for the threading model.
 */
class MainActivity : Activity() {

    companion object {
        private const val TAG = "NovaMain"
    }

    private lateinit var surfaceView: NovaSurfaceView
    private lateinit var runtime: NovaRuntime
    private lateinit var inputManager: InputManager

    /** Guards native calls after onDestroy has completed teardown. */
    @Volatile
    private var nativeAlive = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        hideSystemBars()

        val dataPath = NovaAssetManager.prepare(this)
        val bounds = windowManager.currentWindowMetrics.bounds
        val initResult = NativeBridge.nativeInit(
            assets,
            dataPath.absolutePath,
            bounds.width(),
            bounds.height(),
            BuildConfig.NOVA_VALIDATION,
        )
        if (initResult != 0) {
            Log.e(TAG, "Native engine init failed with code $initResult")
            finish()
            return
        }
        nativeAlive = true

        inputManager = InputManager()
        runtime = NovaRuntime()

        surfaceView = NovaSurfaceView(this).apply {
            onSurfaceReady = { surface -> runtime.onSurfaceReady(surface) }
            onSurfaceSizeChanged = { w, h -> runtime.onSurfaceSizeChanged(w, h) }
            onSurfaceLost = { handleSurfaceLost() }
            touchSink = { event -> inputManager.onTouchEvent(event) }
        }
        setContentView(surfaceView)
        runtime.start()
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (!nativeAlive) return
        NativeBridge.nativeOnResume()
        runtime.resume()
    }

    override fun onPause() {
        if (nativeAlive) {
            runtime.pause()
            NativeBridge.nativeOnPause()
        }
        super.onPause()
    }

    override fun onDestroy() {
        if (nativeAlive) {
            runtime.stop()
            NativeBridge.nativeShutdown()
            nativeAlive = false
        }
        super.onDestroy()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (nativeAlive && inputManager.onKey(event)) return true
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (nativeAlive && inputManager.onKey(event)) return true
        return super.onKeyUp(keyCode, event)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    /** Surface-lost path: wait for the frame pump, then release natively. */
    private fun handleSurfaceLost() {
        if (!nativeAlive) return
        runtime.onSurfaceLost()
    }

    /** Edge-to-edge immersive mode with swipe-in transient bars. */
    private fun hideSystemBars() {
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.let { controller ->
            controller.hide(WindowInsets.Type.systemBars())
            controller.systemBarsBehavior =
                WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }
}
