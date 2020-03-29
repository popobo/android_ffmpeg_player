package com.bo.test_ffmpeg;

import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.content.Context;

public class XPlay extends GLSurfaceView implements Runnable, SurfaceHolder.Callback {
    public XPlay(Context context, AttributeSet attrs){
        //AttributeSet默认输入
        super(context, attrs);
    }

    @Override
    public void run() {
        //在线程中调用,不阻塞画面的刷新
        Open("/sdcard/test.mp4", getHolder().getSurface());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        new Thread(this).start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        super.surfaceChanged(holder, format, w, h);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        super.surfaceDestroyed(holder);
    }

    public native void Open(String url, Object surface);
}
