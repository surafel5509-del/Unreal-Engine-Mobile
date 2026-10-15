package com.nova.engine

import android.content.res.Configuration
import com.nova.engine.editor.EditorView
import android.app.Activity
import android.app.AlertDialog
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.util.DisplayMetrics
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.EditText
import android.view.WindowManager
import com.nova.engine.runtime.NovaRuntime

/** Android 5+ host for the GLES scene editor; native calls are serialized by Engine. */
class MainActivity : Activity() {

    companion object {
        private const val TAG = "NovaMain"
    }

    private lateinit var editor: EditorView
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
            Build.VERSION.SDK_INT,
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
            tag = "viewport"
            onSurfaceReady = { surface -> runtime.onSurfaceReady(surface) }
            onSurfaceSizeChanged = { w, h -> runtime.onSurfaceSizeChanged(w, h) }
            onSurfaceLost = { handleSurfaceLost() }
            touchSink = { event -> inputManager.onTouchEvent(event) }
        }
        editor = EditorView(this, surfaceView)
        setContentView(editor)
        runtime.start()
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (!nativeAlive) return
        NativeBridge.nativeOnResume()
        runtime.resume()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (::editor.isInitialized && nativeAlive) editor.rebuild()
    }

    override fun onPause() {
        if (nativeAlive) {
            editor.saveRecovery()
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
        if (nativeAlive && !event.isSystem && currentFocus !is EditText && inputManager.onKey(event)) return true
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (nativeAlive && !event.isSystem && currentFocus !is EditText && inputManager.onKey(event)) return true
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
            // Android 5-10: the insets-controller API does not exist yet; the
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

    /** Report a driver/init error without assuming a Vulkan requirement. */
    private fun showInitFailure(code: Int) {
        val detail = when (code) {
            2 -> "An OpenGL ES 2 or 3 context could not be created. " +
                "Restart the phone and check for system updates. Vulkan is not required."
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
