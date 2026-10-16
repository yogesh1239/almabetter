package com.contra3.recomp;

import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL runs the recompiled program from libmain.so; a touch pad is
 * laid over the picture and hides itself while a gamepad is connected.
 */
public class Contra3Activity extends SDLActivity implements InputManager.InputDeviceListener {
    static native void nativeSetTouchPad(int buttons);

    private TouchPadView mPad;
    private InputManager mInput;

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        return new String[] { "--no-launcher", RomSetupActivity.romFile(this).getAbsolutePath() };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mLayout == null)
            return;
        mPad = new TouchPadView(this);
        mLayout.addView(mPad, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mInput = (InputManager) getSystemService(INPUT_SERVICE);
        if (mInput != null)
            mInput.registerInputDeviceListener(this, new Handler(Looper.getMainLooper()));
        updatePadVisibility();
    }

    @Override
    protected void onDestroy() {
        if (mInput != null)
            mInput.unregisterInputDeviceListener(this);
        super.onDestroy();
    }

    private static boolean gamepadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual())
                continue;
            int s = d.getSources();
            if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                return true;
        }
        return false;
    }

    private void updatePadVisibility() {
        if (mPad == null)
            return;
        boolean show = !gamepadConnected();
        mPad.setVisibility(show ? View.VISIBLE : View.GONE);
        if (!show)
            nativeSetTouchPad(0);
    }

    @Override public void onInputDeviceAdded(int id) { updatePadVisibility(); }
    @Override public void onInputDeviceRemoved(int id) { updatePadVisibility(); }
    @Override public void onInputDeviceChanged(int id) { updatePadVisibility(); }
}
