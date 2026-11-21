// Минимальная заглушка raylib для проверки компиляции и логики Level 2 вне Android (НЕ часть игры).
#pragma once
#include <cstdint>
#include <cstddef>
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Color { unsigned char r, g, b, a; };
struct Rectangle { float x, y, width, height; };
struct Texture2D { unsigned int id; int width, height, mipmaps, format; };
typedef Texture2D Texture;
struct Image { void* data; int width, height, mipmaps, format; };
struct Mesh { int vertexCount; };
struct Model { int meshCount; int materialCount; };
#define WHITE Color{255,255,255,255}
enum { PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 = 7 };
enum { TEXTURE_FILTER_BILINEAR = 1, TEXTURE_FILTER_TRILINEAR = 2 };
enum { TEXTURE_WRAP_REPEAT = 0, TEXTURE_WRAP_CLAMP = 1 };
Texture2D LoadTexture(const char*);
void UnloadTexture(Texture2D);
Image LoadImage(const char*);
void UnloadImage(Image);
void ImageFormat(Image*, int);
void ImageResize(Image*, int, int);
Texture2D LoadTextureFromImage(Image);
void SetTextureWrap(Texture2D, int);
void SetTextureFilter(Texture2D, int);
Model LoadModel(const char*);
void UnloadModel(Model);
void DrawModelEx(Model, Vector3, Vector3, float, Vector3, Color);
float GetRandomValueF();
