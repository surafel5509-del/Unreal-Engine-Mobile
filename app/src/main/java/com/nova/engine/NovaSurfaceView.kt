package com.nova.engine

import android.content.Context
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * SurfaceView hosting the Vulkan swapchain.
 *
 * The view is intentionally dumb: it forwards surface lifecycle and touch
 * input through lambdas so the [NovaRuntime] (frame pump) and [InputManager]
 * stay independent of the view itself.
 *
 * Frame pacing is handled on a dedicated render thread owned by the runtime;
 * the UI thread never blocks on present.
 */
class NovaSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs) {

    /** Called when the underlying surface is created (hand it to the engine). */
    var onSurfaceReady: ((Surface) -> Unit)? = null

    /** Called on every size/format change (rotation, fold, resize). */
    var onSurfaceSizeChanged: ((Int, Int) -> Unit)? = null

    /** Called when the surface is going away (must stop rendering first). */
    var onSurfaceLost: (() -> Unit)? = null

    /** Touch sink; returns true when the event was consumed. */
    var touchSink: ((MotionEvent) -> Boolean)? = null

    // Declared before init{}: holder.addCallback references it during
    // construction (Kotlin initializes properties in declaration order).
    private val surfaceCallback = object : SurfaceHolder.Callback {
        override fun surfaceCreated(holder: SurfaceHolder) {
            onSurfaceReady?.invoke(holder.surface)
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            onSurfaceSizeChanged?.invoke(width, height)
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            onSurfaceLost?.invoke()
        }
    }

    init {
        holder.addCallback(surfaceCallback)
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        return touchSink?.invoke(event) ?: super.onTouchEvent(event)
    }
}
