package com.nova.engine

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.util.DisplayMetrics
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.View
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
 * assets and writes to app-private storage (both permission-free on API 26+).
 * See docs/ARCHITECTURE.md for the threading model.
 *
 * `minSdk` is 26 (Android 8.0). Every API added after that which this activity
 * uses (`WindowInsetsController`, `setDecorFitsSystemWindows`,
 * `getCurrentWindowMetrics`) is gated behind [Build.VERSION] checks with a
 * pre-API-30 fallback, and a device whose driver lacks Vulkan 1.3 gets an
 * explanatory dialog from [showInitFailure] instead of a silent exit.
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
        val bounds = windowBounds()
        val initResult = NativeBridge.nativeInit(
            assets,
            dataPath.absolutePath,
            bounds.width(),
            bounds.height(),
            BuildConfig.NOVA_VALIDATION,
        )
        if (initResult != 0) {
            Log.e(TAG, "Native engine init failed with code $initResult")
            showInitFailure(initResult)
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
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { controller ->
                controller.hide(WindowInsets.Type.systemBars())
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            // Android 8-10: the insets-controller API does not exist yet; the
            // deprecated visibility flags are the only immersive mode there.
            // IMMERSIVE_STICKY (transient bars on swipe) is the pre-30
            // equivalent of BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE; the
            // *_SWIPE variant was removed from the SDK in API 34.
            @Suppress("DEPRECATION")
            run {
                window.decorView.systemUiVisibility =
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                        View.SYSTEM_UI_FLAG_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            }
        }
    }

    /** Window size in pixels; [WindowManager.getCurrentWindowMetrics] is API 30+. */
    private fun windowBounds(): Rect =
        if (Build.VERSION.SDK_INT >= 30) {
            windowManager.currentWindowMetrics.bounds
        } else {
            val metrics = DisplayMetrics()
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.getRealMetrics(metrics)
            Rect(0, 0, metrics.widthPixels, metrics.heightPixels)
        }

    /**
     * Explain a failed native init on screen instead of closing silently: with
     * `minSdk` 26 the common cause is a device whose driver offers no Vulkan 1.3
     * renderer, which is a device capability, not a crash.
     */
    private fun showInitFailure(code: Int) {
        val detail = when (code) {
            2 -> "This device offers no Vulkan 1.3 renderer. The engine needs a " +
                "Vulkan 1.3 driver (Android 12 or newer with up-to-date GPU " +
                "drivers); the GLES 3.2 fallback backend for older devices " +
                "arrives in a later milestone."
            else -> "Check logcat (tags NovaMain / nova) for the detailed reason."
        }
        AlertDialog.Builder(this)
            .setTitle("Nova Engine cannot start")
            .setMessage("Engine initialization failed (error $code). $detail")
            .setPositiveButton("OK") { dialog, _ -> dialog.dismiss() }
            .setOnDismissListener { finish() }
            .show()
    }
}
