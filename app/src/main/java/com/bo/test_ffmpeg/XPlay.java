package com.bo.test_ffmpeg;

import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.content.Context;

//引入SurfaceHolder.Callback主要是为了知道surface什么时候初始化好了
public class XPlay extends GLSurfaceView implements Runnable, SurfaceHolder.Callback {
    public XPlay(Context context, AttributeSet attrs){
        //AttributeSet默认输入
        super(context, attrs);
    }

    @Override
    public void run() {
        //在线程中调用,不阻塞画面的刷新
        Open("/sdcard/out.yuv", getHolder().getSurface());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        new Thread(this).start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        //不注释掉这个会出错, Failed to open /sys/class/kgsl/kgsl-3d0/gpu_model
//        super.surfaceChanged(holder, format, w, h);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
//        super.surfaceDestroyed(holder);
    }

    //NDK函数
    public native void Open(String url, Object surface);
}
