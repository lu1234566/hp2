package com.hp2.mobile

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

/**
 * Controller-only input manager.
 * Maps Android gamepad events to UE1 native input codes.
 */
class ControllerManager(context: Context) : InputManager.InputDeviceListener {

    private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager
    private val handler = Handler(Looper.getMainLooper())

    init {
        inputManager.registerInputDeviceListener(this, handler)
    }

    fun onKeyEvent(event: KeyEvent): Boolean {
        if (!isGamepadEvent(event)) return false
        val ueKey = mapAndroidKeyToUE1(event.keyCode)
        val pressed = event.action == KeyEvent.ACTION_DOWN
        return if (ueKey >= 0) {
            nativeSendKeyEvent(ueKey, pressed)
            true
        } else false
    }

    fun onMotionEvent(event: MotionEvent): Boolean {
        if (!isGamepadEvent(event)) return false
        val lx = event.getAxisValue(MotionEvent.AXIS_X)
        val ly = event.getAxisValue(MotionEvent.AXIS_Y)
        val rx = event.getAxisValue(MotionEvent.AXIS_Z)
        val ry = event.getAxisValue(MotionEvent.AXIS_RZ)
        val lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER)
        val rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER)
        val dpadX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val dpadY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        nativeSendAnalogEvent(lx, ly, rx, ry, lt, rt, dpadX, dpadY)
        return true
    }

    private fun isGamepadEvent(event: KeyEvent): Boolean {
        val source = event.source
        return (source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
               (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    private fun isGamepadEvent(event: MotionEvent): Boolean {
        return (event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    override fun onInputDeviceAdded(deviceId: Int) {}
    override fun onInputDeviceRemoved(deviceId: Int) {}
    override fun onInputDeviceChanged(deviceId: Int) {}

    private fun mapAndroidKeyToUE1(key: Int): Int = when (key) {
        KeyEvent.KEYCODE_BUTTON_A -> 0x100
        KeyEvent.KEYCODE_BUTTON_B -> 0x101
        KeyEvent.KEYCODE_BUTTON_X -> 0x102
        KeyEvent.KEYCODE_BUTTON_Y -> 0x103
        KeyEvent.KEYCODE_BUTTON_L1 -> 0x104
        KeyEvent.KEYCODE_BUTTON_R1 -> 0x105
        KeyEvent.KEYCODE_BUTTON_L2 -> 0x106
        KeyEvent.KEYCODE_BUTTON_R2 -> 0x107
        KeyEvent.KEYCODE_BUTTON_START -> 0x108
        KeyEvent.KEYCODE_BUTTON_SELECT -> 0x109
        KeyEvent.KEYCODE_BUTTON_THUMBL -> 0x10A
        KeyEvent.KEYCODE_BUTTON_THUMBR -> 0x10B
        KeyEvent.KEYCODE_DPAD_UP -> 0x10C
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x10D
        KeyEvent.KEYCODE_DPAD_LEFT -> 0x10E
        KeyEvent.KEYCODE_DPAD_RIGHT -> 0x10F
        else -> -1
    }

    private external fun nativeSendKeyEvent(key: Int, pressed: Boolean)
    private external fun nativeSendAnalogEvent(
        lx: Float, ly: Float, rx: Float, ry: Float,
        lt: Float, rt: Float, dpadX: Float, dpadY: Float
    )
}
