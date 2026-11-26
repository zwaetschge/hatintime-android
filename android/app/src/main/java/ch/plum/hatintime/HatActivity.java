package ch.plum.hatintime;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

/**
 * Runs the recompiled game: SDL3 hosts the window, input and audio; libmain.so is the game
 * (Win64 runtime, lifted HatinTimeGame.exe, Direct3D 9 via DXVK).
 */
public class HatActivity extends SDLActivity {
    /** Longest side of the render surface unless the player asked for full resolution. */
    static final int RENDER_MAX = 1280;

    @Override protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override protected String[] getArguments() {
        return new String[] { Launcher.gameDir(this).getAbsolutePath(), Launcher.userDir(this).getAbsolutePath() };
    }

    static native void nativeSetPad(int buttons, int lt, int rt, int lx, int ly, int rx, int ry);
    static native void nativeTap(float x, float y);
    static native void nativeLook(float dx, float dy);
    static native boolean nativeCursorShown();

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (mSurface == null) return;
        if (mLayout != null)
            mLayout.addView(new PadView(this), new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        SharedPreferences prefs = getSharedPreferences(Launcher.PREFS, MODE_PRIVATE);
        if (prefs.getBoolean(Launcher.PREF_FULL_RES, false)) return;
        DisplayMetrics dm = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(dm);
        int w = Math.max(dm.widthPixels, dm.heightPixels), h = Math.min(dm.widthPixels, dm.heightPixels);
        if (w <= RENDER_MAX) return;
        int sw = RENDER_MAX, sh = Math.round((float) h * RENDER_MAX / w / 16f) * 16;
        Log.i("HatInTime", "render surface " + sw + "x" + sh + " (display " + w + "x" + h + ")");
        mSurface.setLayoutParams(new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mSurface.getHolder().setFixedSize(sw, sh);
        nativeSetenv("W64_RENDER_SIZE", sw + "x" + sh);
    }
}
