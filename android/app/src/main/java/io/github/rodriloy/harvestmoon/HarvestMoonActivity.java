/*
 * HarvestMoonActivity — the game on Android: SDL's activity (SDL3 and the
 * game, libmain.so: native/), plus a second screen when the device has one
 * (the AYN Thor's lower screen, or any other display): the pause menu,
 * live, drawn by the game (native/second.c) and used by touch.
 *
 * The second screen is a Presentation on the other display. It never takes
 * the keys' focus, so the device's buttons keep playing the game; a key
 * that reaches it anyway is handed back to the game.
 */
package io.github.rodriloy.harvestmoon;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

import org.libsdl.app.SDLActivity;

public class HarvestMoonActivity extends SDLActivity {
    private static final String TAG = "HarvestMoon";
    private static volatile HarvestMoonActivity sSelf;
    private static volatile boolean sWanted = true;      /* the option: Controls > Touch > Second screen */

    private DisplayManager mDisplays;
    private SecondScreen mSecond;
    private final DisplayManager.DisplayListener mListener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) { updateSecond(); }
        @Override public void onDisplayRemoved(int id) { updateSecond(); }
        @Override public void onDisplayChanged(int id) { updateSecond(); }
    };

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sSelf = this;
        mDisplays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        if (mDisplays != null) mDisplays.registerDisplayListener(mListener, new Handler(Looper.getMainLooper()));
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateSecond();
    }

    @Override
    protected void onDestroy() {
        if (mDisplays != null) mDisplays.unregisterDisplayListener(mListener);
        closeSecond();
        sSelf = null;
        super.onDestroy();
    }

    /* the display that is not ours: a presentation display first, else any
     * other real one (not a private or virtual one, such as a recording) */
    private Display otherDisplay() {
        if (mDisplays == null) return null;
        Display mine = getWindowManager().getDefaultDisplay();
        int me = mine != null ? mine.getDisplayId() : Display.DEFAULT_DISPLAY;
        for (Display d : mDisplays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION))
            if (d.getDisplayId() != me && d.isValid()) return d;
        for (Display d : mDisplays.getDisplays())
            if (d.getDisplayId() != me && d.isValid() && (d.getFlags() & Display.FLAG_PRIVATE) == 0) return d;
        return null;
    }

    private void updateSecond() {
        if (isFinishing()) return;
        Display other = otherDisplay();
        try {
            nativeSecondAvailable(other != null);      /* the option shows only then */
        } catch (UnsatisfiedLinkError e) {
            /* the game's library is not loaded (yet) */
        }
        Display d = sWanted ? other : null;
        if (mSecond != null && (d == null || mSecond.getDisplay().getDisplayId() != d.getDisplayId())) closeSecond();
        if (d != null && mSecond == null) {
            try {
                SecondScreen s = new SecondScreen(this, d);
                s.show();
                mSecond = s;
            } catch (RuntimeException e) {          /* WindowManager.InvalidDisplayException among them */
                Log.w(TAG, "no second screen on display " + d.getDisplayId(), e);
                mSecond = null;
            }
        }
        try {
            nativeSecondShown(mSecond != null);
        } catch (UnsatisfiedLinkError e) {
            /* the game's library is not loaded (yet) */
        }
    }

    private void closeSecond() {
        if (mSecond == null) return;
        try { mSecond.dismiss(); } catch (RuntimeException e) { /* gone already */ }
        mSecond = null;
    }

    /* ── called by the game (native/second.c), on its own thread ── */
    public static void secondEnable(boolean on) {
        sWanted = on;
        final HarvestMoonActivity a = sSelf;
        if (a != null) a.runOnUiThread(new Runnable() { @Override public void run() { a.updateSecond(); } });
    }

    public static void secondFrame(int[] pixels) {
        HarvestMoonActivity a = sSelf;
        SecondScreen s = a != null ? a.mSecond : null;
        if (s != null) s.frame(pixels);
    }

    /* ── the game's side ── */
    static native void nativeSecondTouch(int action, int x, int y);
    static native void nativeSecondShown(boolean on);
    static native void nativeSecondAvailable(boolean on);
}
