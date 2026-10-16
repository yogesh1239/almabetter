package com.contra3.recomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/**
 * Multi-touch SNES pad drawn over the game. Left half: an 8-way D-pad that
 * follows the thumb. Right side: the face buttons in SNES layout (Y fire,
 * B jump, A, X), L/R shoulders on top, Select/Start at the bottom centre.
 * The held buttons go to native code as SNES_PAD_* bits.
 */
public class TouchPadView extends View {
    /* SNES_PAD_* bit order used by the host (snes_overlay_draw.h). */
    static final int B = 1, Y = 1 << 1, SELECT = 1 << 2, START = 1 << 3,
            UP = 1 << 4, DOWN = 1 << 5, LEFT = 1 << 6, RIGHT = 1 << 7,
            A = 1 << 8, X = 1 << 9, L = 1 << 10, R = 1 << 11;

    private static final class Btn {
        final String label; final int bit; final RectF r = new RectF(); final boolean round;
        Btn(String label, int bit, boolean round) { this.label = label; this.bit = bit; this.round = round; }
    }

    private final Btn[] mButtons = {
        new Btn("Y", Y, true), new Btn("B", B, true), new Btn("A", A, true), new Btn("X", X, true),
        new Btn("L", L, false), new Btn("R", R, false),
        new Btn("SELECT", SELECT, false), new Btn("START", START, false),
    };
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float mPadCx, mPadCy, mPadR;
    private int mHeld;

    public TouchPadView(Context c) {
        super(c);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeWidth(3 * getResources().getDisplayMetrics().density);
        mStroke.setColor(Color.argb(150, 255, 255, 255));
        mText.setColor(Color.argb(200, 255, 255, 255));
        mText.setTextAlign(Paint.Align.CENTER);
        setHapticFeedbackEnabled(true);
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float u = Math.min(w, h) / 9f; /* layout unit */
        mPadR = u * 1.7f;
        mPadCx = u * 0.6f + mPadR;
        mPadCy = h - u * 0.5f - mPadR;
        float bx = w - u * 0.6f - u * 2.1f, by = h - u * 0.5f - u * 2.1f, br = u * 0.72f, d = u * 1.25f;
        place(mButtons[0], bx - d, by, br);       /* Y left  */
        place(mButtons[1], bx, by + d, br);       /* B down  */
        place(mButtons[2], bx + d, by, br);       /* A right */
        place(mButtons[3], bx, by - d, br);       /* X up    */
        mButtons[4].r.set(u * 0.3f, u * 0.3f, u * 2.6f, u * 1.2f);          /* L */
        mButtons[5].r.set(w - u * 2.6f, u * 0.3f, w - u * 0.3f, u * 1.2f);  /* R */
        mButtons[6].r.set(w / 2f - u * 2.2f, h - u * 0.9f, w / 2f - u * 0.2f, h - u * 0.25f);
        mButtons[7].r.set(w / 2f + u * 0.2f, h - u * 0.9f, w / 2f + u * 2.2f, h - u * 0.25f);
        mText.setTextSize(u * 0.42f);
    }

    private static void place(Btn b, float cx, float cy, float r) {
        b.r.set(cx - r, cy - r, cx + r, cy + r);
    }

    private int dpadBits(float x, float y) {
        float dx = x - mPadCx, dy = y - mPadCy;
        float dist = (float) Math.hypot(dx, dy);
        if (dist < mPadR * 0.22f || dist > mPadR * 2.2f)
            return 0;
        /* 8 sectors of 45 degrees: diagonals press two directions. */
        double ang = Math.toDegrees(Math.atan2(-dy, dx));
        if (ang < 0) ang += 360;
        int sector = (int) Math.round(ang / 45.0) % 8;
        switch (sector) {
            case 0: return RIGHT;
            case 1: return RIGHT | UP;
            case 2: return UP;
            case 3: return UP | LEFT;
            case 4: return LEFT;
            case 5: return LEFT | DOWN;
            case 6: return DOWN;
            default: return DOWN | RIGHT;
        }
    }

    private int buttonBits(float x, float y) {
        int bits = 0;
        float slop = mPadR * 0.15f;
        for (Btn b : mButtons) {
            RectF r = b.r;
            if (x >= r.left - slop && x <= r.right + slop && y >= r.top - slop && y <= r.bottom + slop)
                bits |= b.bit;
        }
        return bits;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int held = 0;
        int action = e.getActionMasked();
        for (int i = 0; i < e.getPointerCount(); i++) {
            if ((action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_UP)
                    && i == e.getActionIndex())
                continue;
            if (action == MotionEvent.ACTION_CANCEL)
                continue;
            float x = e.getX(i), y = e.getY(i);
            held |= x < getWidth() * 0.45f ? dpadBits(x, y) | buttonBits(x, y) : buttonBits(x, y);
        }
        if (held != mHeld) {
            if ((held & ~mHeld) != 0)
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            mHeld = held;
            Contra3Activity.nativeSetTouchPad(held);
            invalidate();
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas c) {
        /* D-pad */
        mFill.setColor(Color.argb(60, 255, 255, 255));
        c.drawCircle(mPadCx, mPadCy, mPadR, mFill);
        c.drawCircle(mPadCx, mPadCy, mPadR, mStroke);
        float k = mPadR * 0.62f;
        int[][] dirs = { {UP, 0, -1}, {DOWN, 0, 1}, {LEFT, -1, 0}, {RIGHT, 1, 0} };
        for (int[] d : dirs) {
            mFill.setColor((mHeld & d[0]) != 0 ? Color.argb(170, 255, 220, 120) : Color.argb(90, 255, 255, 255));
            c.drawCircle(mPadCx + d[1] * k, mPadCy + d[2] * k, mPadR * 0.25f, mFill);
        }
        /* Buttons */
        for (Btn b : mButtons) {
            boolean on = (mHeld & b.bit) != 0;
            mFill.setColor(on ? Color.argb(170, 255, 220, 120) : Color.argb(60, 255, 255, 255));
            if (b.round) {
                c.drawOval(b.r, mFill);
                c.drawOval(b.r, mStroke);
            } else {
                float rad = b.r.height() / 2;
                c.drawRoundRect(b.r, rad, rad, mFill);
                c.drawRoundRect(b.r, rad, rad, mStroke);
            }
            float ty = b.r.centerY() - (mText.descent() + mText.ascent()) / 2;
            c.drawText(b.label, b.r.centerX(), ty, mText);
        }
    }
}
