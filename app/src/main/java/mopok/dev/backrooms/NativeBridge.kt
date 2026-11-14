package mopok.dev.backrooms

import android.app.Activity

/** JNI-мост к libgame.so (реализация: cpp/jni_bridge.cpp). */
object NativeBridge {
    init { System.loadLibrary("game") }

    /** Сообщает C++ об Activity и плотности экрана; вызывать ДО super.onCreate(). */
    external fun nativeInit(activity: Activity, density: Float, sdk: Int)
}
