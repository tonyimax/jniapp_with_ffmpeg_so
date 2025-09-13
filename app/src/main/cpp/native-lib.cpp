#include <jni.h>
#include <string>
#include <android/log.h>
#include <thread>
#include <chrono>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>

    #include <android/native_window.h>
    #include <android/native_window_jni.h>
    #include <media/NdkMediaCodec.h>
    #include <media/NdkMediaFormat.h>

    #define LOG_TAG "JNI_FFMPEG_DEMO"
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOGTAG, __VA_ARGS__)

    enum NALUnitType {
        TRAIL_N = 1, TRAIL_R = 0, IDR_W_RADL = 19, IDR_N_LP = 20, CRA_NUT = 21
    };

    enum SliceType { P_SLICE = 0, B_SLICE = 1, I_SLICE = 2 };

    // 模拟从比特流中读取 Exp-Golomb 编码
    uint32_t readExpGolomb(uint8_t* data, size_t& bitOffset) {
        uint32_t leadingZeros = 0;
        while ((data[bitOffset / 8] & (1 << (7 - (bitOffset % 8)))) == 0) {
            leadingZeros++;
            bitOffset++;
        }
        bitOffset++; // 跳过终止位 '1'
        uint32_t value = (1 << leadingZeros) - 1;
        for (uint32_t i = 0; i < leadingZeros; i++) {
            value |= ((data[bitOffset / 8] >> (7 - (bitOffset % 8))) & 1) << (leadingZeros - 1 - i);
            bitOffset++;
        }
        return value;
    }

    // 解析 Slice Header 中的 slice_type
    uint8_t parseSliceType(uint8_t* nalUnit) {
        size_t bitOffset = 16; // 跳过 2 字节 NAL 头
        // 1. 跳过 first_slice_segment_in_pic_flag（1 bit）
        bitOffset++;
        // 2. 跳过 slice_pic_parameter_set_id（Exp-Golomb）
        readExpGolomb(nalUnit, bitOffset);
        // 3. 读取 slice_type（Exp-Golomb）
        return readExpGolomb(nalUnit, bitOffset); // 返回值 0=P, 1=B, 2=I
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jniapp_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    unsigned version = avcodec_version();
    std::string hello = "FFmpeg version:" + std::to_string(version);
    return env->NewStringUTF(hello.c_str());
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_example_jniapp_MainActivity_nativeInitDecoder(
        JNIEnv *env,
        jobject /* this */,
        jobject surface) {

    // 从 Java Surface 创建 ANativeWindow
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    if (!nativeWindow) {
        LOGE("Failed to create ANativeWindow from Surface");
        return 0;
    }

    // 创建 MediaCodec 解码器
    AMediaCodec *mediaCodec = AMediaCodec_createDecoderByType("video/hevc");
    if (!mediaCodec) {
        LOGE("Failed to create MediaCodec decoder");
        ANativeWindow_release(nativeWindow);
        return 0;
    }

    // 配置 MediaFormat
    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1280);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 720);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, 30);

    // 配置解码器
    media_status_t status = AMediaCodec_configure(
            mediaCodec,
            format,
            nativeWindow, // 关键：使用 nativeWindow 作为输出 Surface
            nullptr,
            0
    );

    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("Failed to configure MediaCodec: %d", status);
        AMediaCodec_delete(mediaCodec);
        ANativeWindow_release(nativeWindow);
        return 0;
    }

    // 启动解码器
    status = AMediaCodec_start(mediaCodec);
    if (status != AMEDIA_OK) {
        LOGE("Failed to start MediaCodec: %d", status);
        AMediaCodec_delete(mediaCodec);
        ANativeWindow_release(nativeWindow);
        return 0;
    }

    // 将指针转换为 long 返回给 Java
    return reinterpret_cast<jlong>(mediaCodec);
}

JNIEXPORT void JNICALL
Java_com_example_jniapp_MainActivity_nativeDecodeFrame(
        JNIEnv *env,
        jobject /* this */,
        jlong decoderHandle,
        jbyteArray data,
        jint size,
        jlong presentationTimeUs) {

    AMediaCodec *mediaCodec = reinterpret_cast<AMediaCodec *>(decoderHandle);
    if (!mediaCodec) {
        LOGE("MediaCodec is null");
        return;
    }

    // 获取输入缓冲区
    ssize_t inputBufferIndex = AMediaCodec_dequeueInputBuffer(mediaCodec, 10000);
    if (inputBufferIndex >= 0) {
        size_t bufferSize;
        uint8_t *inputBuffer = AMediaCodec_getInputBuffer(
                mediaCodec,
                inputBufferIndex,
                &bufferSize
        );

        if (inputBuffer && bufferSize >= size) {
            // 复制数据到输入缓冲区
            jbyte *javaData = env->GetByteArrayElements(data, nullptr);
            memcpy(inputBuffer, javaData, size);
            env->ReleaseByteArrayElements(data, javaData, 0);

            // 提交输入缓冲区
            AMediaCodec_queueInputBuffer(
                    mediaCodec,
                    inputBufferIndex,
                    0,
                    size,
                    presentationTimeUs,
                    0
            );
        }
    }

    // 处理输出缓冲区
    AMediaCodecBufferInfo info;
    ssize_t outputBufferIndex = AMediaCodec_dequeueOutputBuffer(
            mediaCodec,
            &info,
            10000
    );

    if (outputBufferIndex >= 0) {
        // 渲染到 Surface 并释放缓冲区
        AMediaCodec_releaseOutputBuffer(mediaCodec, outputBufferIndex, true);
    }
}

JNIEXPORT void JNICALL
Java_com_example_jniapp_MainActivity_nativeReleaseDecoder(
        JNIEnv *env,
        jobject /* this */,
        jlong decoderHandle) {

    AMediaCodec *mediaCodec = reinterpret_cast<AMediaCodec *>(decoderHandle);
    if (mediaCodec) {
        AMediaCodec_stop(mediaCodec);
        AMediaCodec_delete(mediaCodec);
    }
}

} // extern "C"

extern "C"
JNIEXPORT void JNICALL
Java_com_example_jniapp_MainActivity_getVideoBuffer(JNIEnv *env, jobject thiz) {
    /*if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return;
    }*/

    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, "rtsp://192.168.16.115:8554/live", nullptr, nullptr) != 0) {
        LOGI("===>Could not open input file\n");
        return;
    }
    LOGI("===>avformat_open_input ok");
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        LOGI("===>Could not find stream information\n");
        return;
    }
    LOGI("===>avformat_find_stream_info ok");
    int video_stream_index = -1;
    AVCodecParameters* codec_params = nullptr;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            codec_params = format_ctx->streams[i]->codecpar;
            break;
        }
    }

    if (video_stream_index == -1) {
        LOGI("===>Could not find video stream\n");
        return;
    }
    auto codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        LOGI("===>Unsupported codec\n");
        return;
    }
    LOGI("===>avcodec_find_decoder ok");


    AVCodecContext* pCodecCtx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(pCodecCtx, codec_params) < 0) {
        LOGI("===>Could not copy codec parameters to context\n");
        return;
    }
    LOGI("===>avcodec_alloc_context3 and avcodec_parameters_to_context ok");

    if (avcodec_open2(pCodecCtx, codec, nullptr) < 0) {
        LOGI("===>Could not open codec\n");
        return;
    }
    LOGI("===>avcodec_open2 ok");

    // 分配帧结构
    AVFrame* pFrameYUV = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    // 创建SWS上下文用于颜色空间转换
    SwsContext* sws_ctx = sws_getContext(
            pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
            pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);


    //SDL_Event event;
    // 读取并解码帧
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            LOGI("===>%d %d %d %d %d %d %d %d %d %d\n",
                     static_cast<int>(packet->data[0]),
                     static_cast<int>(packet->data[1]),
                     static_cast<int>(packet->data[2]),
                     static_cast<int>(packet->data[3]),
                     static_cast<int>(packet->data[4]),
                     static_cast<int>(packet->data[5]),
                     static_cast<int>(packet->data[6]),
                     static_cast<int>(packet->data[7]),
                     static_cast<int>(packet->data[8]),
                     static_cast<int>(packet->data[9])
            );
            uint8_t nal_type = (packet->data[4] >> 1) & 0x3F;
            // 如果是切片数据，解析slice_type
            if (nal_type == TRAIL_N || nal_type == TRAIL_R ||
                nal_type == IDR_W_RADL || nal_type == IDR_N_LP) {
                uint8_t sliceType=parseSliceType((packet->data+4));
                LOGI("===>帧类型:(0=P, 1=B, 2=I) -> %d\n",static_cast<int>(sliceType));

            }

            // 发送数据包给解码器
            int ret = avcodec_send_packet(pCodecCtx, packet);
            if (ret < 0) {
                LOGI("Error sending packet for decoding\n");
                continue;
            }

            // 接收解码后的帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(pCodecCtx, pFrameYUV);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOGI("Error during decoding\n");
                    break;
                }

                switch (static_cast<int>(pFrameYUV->pict_type)) {
                    case 1:
                        LOGI("===>AV_PICTURE_TYPE_I : %d\n",AV_PICTURE_TYPE_I);
                        break;
                    case 2:
                        LOGI("===>AV_PICTURE_TYPE_P : %d\n",AV_PICTURE_TYPE_P);
                        break;
                    case 3:
                        LOGI("===>AV_PICTURE_TYPE_B : %d\n",AV_PICTURE_TYPE_B);
                        break;
                }
                // 这里可以处理解码后的帧数据
                LOGI("Decoded frame pts:%ld width:%d height:%d\n",
                     pFrameYUV->pts,pFrameYUV->width,pFrameYUV->height);

                // 转换为RGB格式示例
                uint8_t* rgb_data[1] = { new uint8_t[pFrameYUV->width * pFrameYUV->height * 3] };
                int rgb_linesize[1] = { pFrameYUV->width * 3 };

                sws_scale(sws_ctx, pFrameYUV->data, pFrameYUV->linesize, 0,
                          pFrameYUV->height, rgb_data, rgb_linesize);

                // 使用rgb_data...
                // 更新SDL纹理
                /*SDL_UpdateYUVTexture(texture, NULL,
                                     pFrameYUV->data[0], pFrameYUV->linesize[0],
                                     pFrameYUV->data[1], pFrameYUV->linesize[1],
                                     pFrameYUV->data[2], pFrameYUV->linesize[2]);

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);*/

                // 控制帧率 for local 265 file open it
                //SDL_Delay(40); // 约25fps

                //call surface view draw image
                // 将RGB帧数据传递给Java层显示
                /*int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height, 1);
                jclass clazz = env->GetObjectClass(thiz);
                jmethodID methodID = env->GetMethodID(clazz, "onFrameDecoded", "([BII)V");
                jbyteArray byteArray = env->NewByteArray(numBytes);
                env->SetByteArrayRegion(byteArray, 0, numBytes, (jbyte *) pFrameYUV->data[0]);
                env->CallVoidMethod(thiz, methodID, byteArray, pCodecCtx->width, pCodecCtx->height);
                env->DeleteLocalRef(byteArray);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));*/


                delete[] rgb_data[0];
            }
        }
        av_packet_unref(packet);

        // 处理事件
        /*SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                goto end;
            default:
                break;
        }*/
    }

    // 清理资源
    av_frame_free(&pFrameYUV);
    av_packet_free(&packet);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&format_ctx);
    sws_freeContext(sws_ctx);
}