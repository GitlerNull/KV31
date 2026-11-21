package dev.morok.backrooms

import android.app.NativeActivity
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import java.io.File

/**
 * Точка входа. NativeActivity загружает libgame.so (meta-data android.app.lib_name) и запускает raylib android_main -> main().
 * (Android-бэкенд raylib построен на NativeActivity, поэтому GameActivity здесь не используется.)
 */
class MainActivity : NativeActivity() {
    companion object { init { System.loadLibrary("game") } }

    override fun onCreate(savedInstanceState: Bundle?) {
        NativeBridge.nativeInit(this, resources.displayMetrics.density, Build.VERSION.SDK_INT)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        super.onCreate(savedInstanceState)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    @Suppress("DEPRECATION")
    private fun hideSystemUi() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
    }

    /** Системный Roboto читается из /system/fonts и используется для русских надписей. */
    fun loadUiFontData(): ByteArray? {
        val candidates = arrayOf(
            "/system/fonts/Roboto-Regular.ttf",
            "/system/fonts/NotoSans-Regular.ttf"
        )
        for (path in candidates) {
            try {
                val file = File(path)
                if (file.isFile && file.canRead()) return file.readBytes()
            } catch (_: Exception) { }
        }
        return null
    }

    private val settingsPrefs by lazy {
        getSharedPreferences("KV31Settings", MODE_PRIVATE)
    }

    /** Чтение настройки из постоянного хранилища Android. */
    fun loadSettingValue(index: Int): Int {
        return settingsPrefs.getInt("value_$index", 0)
    }

    /** Сохранение настройки в постоянное хранилище Android. */
    fun saveSettingValue(index: Int, value: Int) {
        settingsPrefs.edit().putInt("value_$index", value).apply()
    }

    /** Вызывается из C++ (Haptic) через JNI. */
    @Suppress("DEPRECATION")
    fun vibrate(ms: Int) {
        val v: Vibrator = if (Build.VERSION.SDK_INT >= 31)
            (getSystemService(VIBRATOR_MANAGER_SERVICE) as VibratorManager).defaultVibrator
        else getSystemService(VIBRATOR_SERVICE) as Vibrator
        if (Build.VERSION.SDK_INT >= 26)
            v.vibrate(VibrationEffect.createOneShot(ms.toLong(), VibrationEffect.DEFAULT_AMPLITUDE))
        else v.vibrate(ms.toLong())
    }
}
