// jni_bridge.cpp - мост Kotlin <-> C++.
//  * JNI_OnLoad       - запоминаем JavaVM при System.loadLibrary("game").
//  * nativeInit       - вызывается из MainActivity до старта нативного потока: сохраняет Activity и параметры экрана.
//  * Haptic(ms)       - C++ -> Kotlin: вибрация при уроне (MainActivity.vibrate).
// Цикл кадра (nativeStep) и касания (nativeOnTouch) здесь не нужны: raylib на Android владеет нативным потоком
// и сам читает мультитач из native_app_glue (GetTouchPointCount/GetTouchPosition).
#include <jni.h>
#include <cstdlib>
#include "util.h"

static JavaVM* gVM = nullptr;
static jobject gAct = nullptr;
static jmethodID gVib = nullptr;
static jmethodID gFontData = nullptr;
static jmethodID gLoadSetting = nullptr;
static jmethodID gSaveSetting = nullptr;
float gDensity = 2.0f;
int   gSdk = 0;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    gVM = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_mopok_dev_backrooms_NativeBridge_nativeInit(JNIEnv* env, jobject, jobject activity, jfloat density, jint sdk) {
    gDensity = density; gSdk = sdk;
    if (gAct) env->DeleteGlobalRef(gAct);
    gAct = env->NewGlobalRef(activity);
    jclass c = env->GetObjectClass(activity);
    gVib = env->GetMethodID(c, "vibrate", "(I)V");
    gFontData = env->GetMethodID(c, "loadUiFontData", "()[B");
    gLoadSetting = env->GetMethodID(c, "loadSettingValue", "(I)I");
    gSaveSetting = env->GetMethodID(c, "saveSettingValue", "(II)V");
    env->DeleteLocalRef(c);
}

unsigned char* LoadSystemFontData(int* size) {
    if (size) *size = 0;
    if (!gVM || !gAct || !gFontData) return nullptr;
    JNIEnv* env = nullptr; bool attached = false;
    if (gVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
#if defined(__ANDROID__)
        if (gVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
#else
        if (gVM->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) return nullptr;
#endif
        attached = true;
    }
    jbyteArray bytes = (jbyteArray)env->CallObjectMethod(gAct, gFontData);
    if (env->ExceptionCheck()) { env->ExceptionClear(); bytes = nullptr; }
    unsigned char* result = nullptr;
    if (bytes) {
        jsize length = env->GetArrayLength(bytes);
        if (length > 0) {
            result = (unsigned char*)malloc((size_t)length);
            if (result) {
                env->GetByteArrayRegion(bytes, 0, length, (jbyte*)result);
                if (env->ExceptionCheck()) { env->ExceptionClear(); free(result); result = nullptr; }
                else if (size) *size = (int)length;
            }
        }
        env->DeleteLocalRef(bytes);
    }
    if (attached) gVM->DetachCurrentThread();
    return result;
}

int LoadSettingValue(int index) {
    if (!gVM || !gAct || !gLoadSetting) return 0;

    JNIEnv* env = nullptr;
    bool attached = false;

    if (gVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
        attached = true;
    }

    jint result = env->CallIntMethod(gAct, gLoadSetting, (jint)index);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = 0;
    }

    if (attached) gVM->DetachCurrentThread();
    return (int)result;
}

void SaveSettingValue(int index, int value) {
    if (!gVM || !gAct || !gSaveSetting) return;

    JNIEnv* env = nullptr;
    bool attached = false;

    if (gVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }

    env->CallVoidMethod(gAct, gSaveSetting, (jint)index, (jint)value);

    if (env->ExceptionCheck()) env->ExceptionClear();

    if (attached) gVM->DetachCurrentThread();
}

void Haptic(int ms) {
    if (!gVM || !gAct || !gVib) return;
    JNIEnv* env = nullptr; bool attached = false;
    if (gVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
#if defined(__ANDROID__)
        if (gVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
#else
        if (gVM->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) return;
#endif
        attached = true;
    }
    env->CallVoidMethod(gAct, gVib, (jint)ms);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) gVM->DetachCurrentThread();
}
