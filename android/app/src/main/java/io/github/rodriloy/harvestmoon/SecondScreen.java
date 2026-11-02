/*
 * SecondScreen — the pause menu on the other display (HarvestMoonActivity):
 * the game's 256 x 224 picture of it, enlarged with sharp pixels, and the
 * touches on it sent back to the game in the menu's own pixels.
 */
package io.github.rodriloy.harvestmoon;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

class SecondScreen extends Presentation {
    private final Activity mOwner;
    private MenuView mView;

    SecondScreen(Activity owner, Display display) {
        super(owner, display);
        mOwner = owner;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window w = getWindow();
        if (w != null) {
            /* the keys stay with the game; the screen stays on */
            w.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                     | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                     | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
        mView = new MenuView(getContext());
        setContentView(mView);
    }

    void frame(int[] pixels) {
        MenuView v = mView;
        if (v != null) v.frame(pixels);
    }

    /* a key or a stick that lands here anyway: the game's */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        return mOwner.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        return mOwner.dispatchGenericMotionEvent(event);
    }

    static class MenuView extends View {
        static final int W = 256, H = 224;
        private final int[] mPixels = new int[W * H];
        private final Bitmap mBitmap = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
        private final Paint mPaint = new Paint();
        private final Rect mSrc = new Rect(0, 0, W, H);
        private final Rect mDst = new Rect();
        private boolean mHave;

        MenuView(Context c) {
            super(c);
            mPaint.setFilterBitmap(false);           /* sharp pixels, as the game's */
            mPaint.setAntiAlias(false);
            mPaint.setDither(false);
            setKeepScreenOn(true);
        }

        void frame(int[] pixels) {
            synchronized (mPixels) {
                System.arraycopy(pixels, 0, mPixels, 0, W * H);
                mHave = true;
            }
            postInvalidate();
        }

        private void fit() {
            int vw = getWidth(), vh = getHeight();
            float s = Math.min(vw / (float) W, vh / (float) H);
            int dw = Math.round(W * s), dh = Math.round(H * s);
            mDst.set((vw - dw) / 2, (vh - dh) / 2, (vw - dw) / 2 + dw, (vh - dh) / 2 + dh);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            canvas.drawColor(0xFF3A1E0C);            /* the frames' dark wood */
            synchronized (mPixels) {
                if (!mHave) return;
                mBitmap.setPixels(mPixels, 0, W, 0, 0, W, H);
            }
            fit();
            canvas.drawBitmap(mBitmap, mSrc, mDst, mPaint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int a;
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: a = 0; break;
                case MotionEvent.ACTION_MOVE: a = 1; break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL: a = 2; break;
                default: return true;
            }
            fit();
            if (mDst.width() <= 0 || mDst.height() <= 0) return true;
            int x = (int) ((e.getX() - mDst.left) * W / mDst.width());
            int y = (int) ((e.getY() - mDst.top) * H / mDst.height());
            try {
                HarvestMoonActivity.nativeSecondTouch(a, x, y);
            } catch (UnsatisfiedLinkError err) {
                /* the game is not loaded */
            }
            if (a == 2) performClick();
            return true;
        }

        @Override
        public boolean performClick() {
            return super.performClick();
        }
    }
}
