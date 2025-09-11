#include <jni.h>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jniapp_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    unsigned version = avcodec_version();
    std::string hello = "FFmpeg version:" + std::to_string(version);
    return env->NewStringUTF(hello.c_str());
}