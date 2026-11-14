// jni_bridge.cpp - мост Kotlin <-> C++.
//  * JNI_OnLoad       - запоминаем JavaVM при System.loadLibrary("game").
//  * nativeInit       - вызывается из MainActivity до старта нативного потока: сохраняет Activity и параметры экрана.
//  * Haptic(ms)       - C++ -> Kotlin: вибрация при уроне (MainActivity.vibrate).
// Цикл кадра (nativeStep) и касания (nativeOnTouch) здесь не нужны: raylib на Android владеет нативным потоком
// и сам читает мультитач из native_app_glue (GetTouchPointCount/GetTouchPosition).
#include <jni.h>
#include "util.h"

static JavaVM* gVM = nullptr;
static jobject gAct = nullptr;
static jmethodID gVib = nullptr;
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
    env->DeleteLocalRef(c);
}

void Haptic(int ms) {
    if (!gVM || !gAct || !gVib) return;
    JNIEnv* env = nullptr; bool attached = false;
    if (gVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    env->CallVoidMethod(gAct, gVib, (jint)ms);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) gVM->DetachCurrentThread();
}
