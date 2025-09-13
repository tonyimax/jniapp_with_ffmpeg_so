package com.example.jniapp;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.WindowCompat;

import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowInsetsController;
import android.widget.TextView;
import android.widget.Toast;

import com.example.jniapp.databinding.ActivityMainBinding;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

public class MainActivity extends AppCompatActivity implements SurfaceHolder.Callback {

    // Used to load the 'jniapp' library on application startup.
    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swscale");
        System.loadLibrary("jniapp");
    }

    private ActivityMainBinding binding;
    private Surface mDecoderSurface;
    private SurfaceView surfaceView;
    private SurfaceHolder surfaceHolder;

    private static final String TAG = "H265Decoder";
    private static final String MIME_TYPE = "video/hevc";
    private static final int VIDEO_WIDTH = 1280;
    private static final int VIDEO_HEIGHT = 720;
    private static final int FRAME_RATE = 60;

    private MediaCodec mMediaCodec;
    private DecoderThread mDecoderThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        surfaceView = binding.surfaceView;
        surfaceHolder = surfaceView.getHolder();
        surfaceView.getHolder().addCallback(this);

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(stringFromJNI());
        enableImmersiveMode();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);//强制横屏


        if (isH265Supported()){
            new Thread(() -> {
                System.out.println("===>独立的视频解码线程===");
                //getVideoBuffer();
            }).start();
        }

    }

    // 检查设备是否支持 H.265 解码
    private boolean isH265Supported() {
        try {
            MediaCodec codec = MediaCodec.createDecoderByType(MIME_TYPE);
            if (codec != null) {
                codec.release();
                Log.d(TAG,"===>设备支持H.265硬件解码");
                return true;
            }
        } catch (IOException e) {
            Log.w(TAG, "===>设备不支持H.265硬件解码，原因:" + e.getMessage());
        }
        return false;
    }

    private void enableImmersiveMode() {
        //API 30 TO CALL
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = null;
            controller = getWindow().getDecorView().getWindowInsetsController();
            if (controller != null) {
                // 1. 隐藏系统栏（状态栏和导航栏）
                controller.hide(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);

                // 2. 设置系统栏的行为模式
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }

            // 确保内容延伸到系统栏后面
            WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        }
    }

    /**
     * A native method that is implemented by the 'jniapp' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native void getVideoBuffer();

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        System.out.println("===>surfaceChanged");
        Log.d(TAG, "Surface changed: " + width + "x" + height);
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        System.out.println("===>surfaceCreated");
        mDecoderSurface = holder.getSurface();
        initMediaCodec();
        startDecoding();
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        System.out.println("===>surfaceDestroyed");
        releaseMediaCodec();
        mDecoderSurface = null;
    }

    // 回调方法，用于显示帧数据
    public void onFrameDecoded(byte[] frameData, int width, int height) {
        new Thread(()->{
            Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(frameData));
            System.out.println(String.format("===>onFrameDecoded %d ,%d",width,height));
            if (!surfaceHolder.getSurface().isValid()) {
                System.out.println("===>Surface 无效，直接返回:");
                return; // Surface 无效，直接返回
            }

            Canvas canvas = surfaceHolder.lockCanvas();
            if (canvas != null) {
                canvas.drawBitmap(bitmap, 0, 0, null);
                surfaceHolder.unlockCanvasAndPost(canvas);
            }
        }).start();
    }

    private void initMediaCodec() {
        try {
            // 创建 H.265 解码器
            mMediaCodec = MediaCodec.createDecoderByType(MIME_TYPE);

            // 配置 MediaFormat
            MediaFormat format = MediaFormat.createVideoFormat(MIME_TYPE, VIDEO_WIDTH, VIDEO_HEIGHT);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 8000000);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);

            // 配置解码器使用 Surface
            mMediaCodec.configure(format, mDecoderSurface, null, 0);
            mMediaCodec.start();

            Log.d(TAG, "H.265 MediaCodec initialized successfully");

        } catch (IOException e) {
            Log.e(TAG, "Failed to initialize H.265 MediaCodec", e);
            mMediaCodec = null;
            Toast.makeText(this, "初始化 H.265 解码器失败", Toast.LENGTH_SHORT).show();
        }
    }

    private void startDecoding() {
        if (mMediaCodec == null || mDecoderSurface == null) {
            Log.e(TAG, "MediaCodec or Surface not ready");
            return;
        }

        if (mDecoderThread != null && mDecoderThread.isAlive()) {
            mDecoderThread.interrupt();
        }

        mDecoderThread = new DecoderThread(mMediaCodec);
        mDecoderThread.start();
        Toast.makeText(this, "开始 H.265 解码", Toast.LENGTH_SHORT).show();
    }

    private void releaseMediaCodec() {
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            mDecoderThread = null;
        }

        if (mMediaCodec != null) {
            try {
                mMediaCodec.stop();
                mMediaCodec.release();
            } catch (Exception e) {
                Log.e(TAG, "Error releasing MediaCodec", e);
            }
            mMediaCodec = null;
        }
    }

    private void stopDecoding() {
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            try {
                mDecoderThread.join(1000);
            } catch (InterruptedException e) {
                Log.w(TAG, "Interrupted while stopping decoder thread");
            }
            mDecoderThread = null;
        }

        if (mMediaCodec != null) {
            try {
                mMediaCodec.stop();
                mMediaCodec.release();
            } catch (Exception e) {
                Log.e(TAG, "Error releasing MediaCodec", e);
            }
            mMediaCodec = null;
        }
        Log.d(TAG, "H.265 decoding stopped");
    }


    @Override
    protected void onPause() {
        super.onPause();
        stopDecoding();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopDecoding();
    }

    // H.265 解码线程
    private class DecoderThread extends Thread {
        private final MediaCodec mMediaCodec;
        private final InputStream mH265Stream;

        public DecoderThread(MediaCodec mediaCodec) {
            this.mMediaCodec = mediaCodec;
            // 从 assets 读取 H.265 裸流文件
            this.mH265Stream = getResources().openRawResource(R.raw.test_720p);
        }

        @Override
        public void run() {
            try {
                decodeH265Stream();
            } catch (IOException e) {
                Log.e(TAG, "H.265 decoding failed", e);
            } finally {
                try {
                    mH265Stream.close();
                } catch (IOException e) {
                    Log.e(TAG, "Failed to close H.265 stream", e);
                }
            }
        }

        private void decodeH265Stream() throws IOException {
            byte[] chunk = new byte[1024 * 128]; // 128KB 缓冲区
            boolean sawInputEOS = false;
            boolean sawOutputEOS = false;
            long startTime = System.currentTimeMillis();
            int frameCount = 0;

            while (!sawOutputEOS && !isInterrupted()) {
                if (!sawInputEOS) {
                    int inputBufferIndex = mMediaCodec.dequeueInputBuffer(10000);
                    if (inputBufferIndex >= 0) {
                        ByteBuffer inputBuffer = mMediaCodec.getInputBuffer(inputBufferIndex);
                        if (inputBuffer != null) {
                            int bytesRead = mH265Stream.read(chunk);
                            if (bytesRead == -1) {
                                sawInputEOS = true;
                                mMediaCodec.queueInputBuffer(
                                        inputBufferIndex,
                                        0,
                                        0,
                                        0,
                                        MediaCodec.BUFFER_FLAG_END_OF_STREAM
                                );
                                Log.d(TAG, "Input EOS reached");
                            } else {
                                inputBuffer.clear();
                                inputBuffer.put(chunk, 0, bytesRead);

                                long presentationTimeUs = computePresentationTime(frameCount);

                                mMediaCodec.queueInputBuffer(
                                        inputBufferIndex,
                                        0,
                                        bytesRead,
                                        presentationTimeUs,
                                        0
                                );
                                frameCount++;

                                if (frameCount % 30 == 0) {
                                    Log.d(TAG, "Decoded " + frameCount + " frames");
                                }
                            }
                        }
                    }
                }

                MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
                int outputBufferIndex = mMediaCodec.dequeueOutputBuffer(bufferInfo, 10000);

                switch (outputBufferIndex) {
                    case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                        MediaFormat newFormat = mMediaCodec.getOutputFormat();
                        Log.d(TAG, "Output format changed: " + newFormat);
                        break;

                    case MediaCodec.INFO_TRY_AGAIN_LATER:
                        // 输出缓冲区暂时不可用
                        break;

                    case MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED:
                        Log.d(TAG, "Output buffers changed");
                        break;

                    default:
                        if (outputBufferIndex >= 0) {
                            if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                                sawOutputEOS = true;
                                Log.d(TAG, "Output EOS reached");
                            }

                            // 渲染到 Surface
                            mMediaCodec.releaseOutputBuffer(outputBufferIndex, true);

                            // 控制播放速率
                            sleepForFrameRate(bufferInfo, startTime);
                        }
                        break;
                }
            }
            Log.d(TAG, "H.265 decoding completed. Total frames: " + frameCount);
        }

        private long computePresentationTime(int frameIndex) {
            return frameIndex * 1000000L / FRAME_RATE;
        }

        private void sleepForFrameRate(MediaCodec.BufferInfo bufferInfo, long startTime) {
            long now = System.currentTimeMillis();
            long expectedTime = startTime + (bufferInfo.presentationTimeUs / 1000);
            long sleepTime = expectedTime - now;

            if (sleepTime > 0) {
                try {
                    Thread.sleep(sleepTime);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
        }
    }

}