package com.nova.engine

import android.view.KeyEvent
import android.view.MotionEvent

/**
 * Unified input front-end.
 *
 * Android input sources are translated into the engine's platform-independent
 * event stream (nova::platform::InputQueue on the native side):
 *  - Touch (single + multi-touch): MotionEvent actions are forwarded per
 *    pointer so the engine sees individual contacts.
 *  - Keys / gamepad buttons: dispatched through [onKey] from the Activity's
 *    key callbacks (covers Bluetooth controllers, keyboards, and the on-screen
 *    D-pad of the platform).
 *
 * Pixel coordinates are forwarded as-is; the engine converts to its virtual
 * coordinate space using the current surface size.
 *
 * Thread-safety: called from the UI thread only (MotionEvent/KeyEvent
 * dispatch), which matches the native queue's expectations.
 */
class InputManager {

    /**
     * Feed a MotionEvent. Returns true when at least one engine event was
     * produced (so callers can stop propagation).
     */
    fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_POINTER_DOWN,
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_POINTER_UP -> {
                val index = event.actionIndex
                NativeBridge.nativeOnTouch(
                    event.actionMasked,
                    event.getPointerId(index),
                    event.getX(index),
                    event.getY(index),
                )
            }
            MotionEvent.ACTION_MOVE -> {
                // Emit one event per active pointer; the engine tracks each
                // contact independently (virtual joysticks, gestures).
                for (i in 0 until event.pointerCount) {
                    NativeBridge.nativeOnTouch(
                        MotionEvent.ACTION_MOVE,
                        event.getPointerId(i),
                        event.getX(i),
                        event.getY(i),
                    )
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (i in 0 until event.pointerCount) {
                    NativeBridge.nativeOnTouch(
                        MotionEvent.ACTION_CANCEL,
                        event.getPointerId(i),
                        event.getX(i),
                        event.getY(i),
                    )
                }
            }
            else -> return false
        }
        return true
    }

    /**
     * Feed a key/button event. Works for KEYCODE_BUTTON_* (gamepad),
     * KEYCODE_DPAD_* and regular keys.
     *
     * @return true when the event was consumed by the engine.
     */
    fun onKey(event: KeyEvent): Boolean {
        // Ignore repeats: the engine maintains its own down/up state.
        if (event.repeatCount > 0 && event.action == KeyEvent.ACTION_DOWN) {
            return true
        }
        NativeBridge.nativeOnKey(event.keyCode, event.action == KeyEvent.ACTION_DOWN)
        return true
    }
}
