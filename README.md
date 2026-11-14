# Закулисье (Backrooms) - Android, C++ (Raylib) + Kotlin
applicationId: `mopok.dev.backrooms`

## Сборка APK без ПК (с телефона, через GitHub Actions)
1. Зарегистрируйся на github.com (в браузере телефона, режим "Версия для ПК").
2. Создай новый репозиторий (New repository, Public).
3. Распакуй ZIP проекта на телефоне и загрузи ВСЕ файлы и папки в репозиторий:
   Add file -> Upload files. ВАЖНО: должна загрузиться и скрытая папка `.github` (в ней `workflows/build.yml`).
   Если телефон не даёт загрузить скрытую папку: Add file -> Create new file, имя `.github/workflows/build.yml`, вставь содержимое из ZIP.
4. Вкладка Actions -> "Build APK" -> Run workflow (или сборка стартует сама после загрузки). Ждать ~8-12 минут.
5. Открой завершённый запуск -> внизу Artifacts -> `backrooms-apk` -> скачай ZIP, внутри `app-debug.apk`.
6. Установи APK (разреши "установку из неизвестных источников" для браузера/файлового менеджера).

## Сборка на ПК
Android Studio (NDK 26, CMake 3.22.1) -> открыть папку -> `gradle assembleDebug`
(или `gradle wrapper` один раз, затем `./gradlew assembleDebug`). APK: `app/build/outputs/apk/debug/app-debug.apk`.
Raylib 5.5 скачивается сам при сборке (FetchContent); можно положить свою копию в `app/src/main/cpp/raylib/`.

## Русский интерфейс
Положи любой TTF с кириллицей в `app/src/main/assets/font.ttf` (например Roboto). Без него интерфейс английский.
