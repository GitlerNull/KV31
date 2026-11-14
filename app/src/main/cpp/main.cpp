// main.cpp - инициализация Raylib. На Android raylib сам реализует android_main() (native_app_glue)
// и вызывает этот main() из потока Activity; окно создаётся по размеру экрана (0,0).
#include "raylib.h"
#include "game.h"

int main(int, char**) {
    InitWindow(0, 0, "Backrooms");
    GameRun();
    CloseWindow();
    return 0;
}
