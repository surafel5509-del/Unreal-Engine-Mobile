package com.nova.engine.runtime

import android.util.Log
import android.view.Surface
import com.nova.engine.NativeBridge
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * Render-thread frame pump.
 *
 * Owns a dedicated Java thread that drives [NativeBridge.nativeFrame] so the
 * UI thread never blocks on GPU present (FIFO swapchain pacing happens inside
 * the native frame call). Synchronization rules:
 *
 *  - All state transitions take [lock]; conditions are awaited on [idle].
 *  - `inFrame` is true exactly while the thread is inside `nativeFrame`;
 *    pause/surface-lost wait on it before proceeding, which guarantees the
 *    native window is never released under a running frame.
 *  - The render thread parks whenever `paused || !surfaceReady`.
 *
 * Lifecycle: [start] -> ([resume]/[pause])* -> [stop]. [stop] joins the
 * render thread; after it returns it is safe to call [NativeBridge.nativeShutdown].
 */
class NovaRuntime {

    companion object {
        @Volatile var targetFps = 60

        private const val TAG = "NovaRuntime"

        /** Longest dt we accept before treating it as a resume stall. */
        private const val MAX_FRAME_SECONDS = 0.25f

        /** Retry cadence when the native side skips a frame. */
        private const val SKIP_SLEEP_MS = 16L
    }

    private val lock = ReentrantLock()
    private val idle = lock.newCondition()

    private var renderThread: Thread? = null
    private var running = false
    private var paused = false
    private var surfaceReady = false
    private var inFrame = false

    /** Start the render thread. Idempotent. */
    fun start() {
        lock.withLock {
            if (running) return
            running = true
            paused = false
            val thread = Thread(::renderLoop, "nova-render")
            thread.isDaemon = true
            renderThread = thread
            thread.start()
            idle.signalAll()
        }
        Log.i(TAG, "Render thread started")
    }

    /** Resume rendering (Activity onResume). */
    fun resume() {
        lock.withLock {
            paused = false
            idle.signalAll()
        }
    }

    /**
     * Pause rendering (Activity onPause). Blocks until the in-flight frame
     * completes so no GPU work overlaps the background transition.
     */
    fun pause() {
        lock.withLock {
            paused = true
            while (inFrame) {
                idle.await()
            }
        }
    }

    /** Hand a new Surface to the native engine. */
    fun onSurfaceReady(surface: Surface) {
        NativeBridge.nativeSetSurface(surface)
        lock.withLock {
            surfaceReady = true
            idle.signalAll()
        }
    }

    /** Surface size changed (rotation/fold); forwarded to the engine. */
    fun onSurfaceSizeChanged(width: Int, height: Int) {
        if (width > 0 && height > 0) {
            NativeBridge.nativeResize(width, height)
        }
    }

    /**
     * The surface is going away. Blocks until the render thread leaves
     * `nativeFrame`, then releases the native window.
     */
    fun onSurfaceLost() {
        lock.withLock {
            surfaceReady = false
            while (inFrame) {
                idle.await()
            }
        }
        NativeBridge.nativeSetSurface(null)
    }

    /** Stop the render thread. After this returns, call nativeShutdown. */
    fun stop() {
        val thread: Thread?
        lock.withLock {
            if (!running && renderThread == null) return
            running = false
            paused = true
            idle.signalAll()
            thread = renderThread
            renderThread = null
        }
        thread?.let {
            try {
                it.join() // never destroy native state while a frame still uses it
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        Log.i(TAG, "Render thread stopped")
    }

    private fun renderLoop() {
        var lastFrameNanos = System.nanoTime()
        while (true) {
            // Park until we are allowed to render; claim the frame slot while
            // holding the lock so pause/lost cannot miss an in-flight frame.
            lock.withLock {
                while (running && (paused || !surfaceReady)) {
                    idle.await()
                }
                if (!running) return
                inFrame = true
            }

            val now = System.nanoTime()
            var dt = (now - lastFrameNanos) / 1_000_000_000.0f
            lastFrameNanos = now
            if (dt < 0.0f || dt > MAX_FRAME_SECONDS) {
                dt = 1.0f / 60.0f
            }

            val status = try {
                NativeBridge.nativeFrame(dt)
            } finally {
                lock.withLock {
                    inFrame = false
                    idle.signalAll()
                }
            }
            // Some drivers/emulators do not block in swap even with interval 1.
            // Bound idle editor rendering to ~60 FPS rather than burning CPU.
            val remaining = 1_000_000_000L / targetFps.coerceIn(15, 60) - (System.nanoTime() - now)
            if (remaining > 0 && status != NativeBridge.FRAME_SKIPPED) {
                java.util.concurrent.locks.LockSupport.parkNanos(remaining)
            }

            if (status == NativeBridge.FRAME_SKIPPED) {
                // Swapchain missing or being recreated: back off instead of spinning.
                try {
                    Thread.sleep(SKIP_SLEEP_MS)
                } catch (e: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return
                }
            }
        }
    }
}
