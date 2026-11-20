#include "level2.h"
#include "util.h"
#include "map_gen.h"
#include "rlgl.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr int MAP_W = 64;
constexpr int MAP_H = 44;
constexpr float CEILING_H = 3.85f;
constexpr float BLUE_ENTRY_X = 43.0f * CELL;
constexpr float FINAL_DOOR_X = 58.0f * CELL;
bool gActive = false;
bool gLoaded = false;
bool gAirlockClosed = false;
bool gAirlockClosing = false;
float gAirlockTimer = 0.0f;
bool gEmergencyDoorOpen = false;
float gEmergencyDoorAnim = 0.0f;
bool gFinalDoorOpening = false;
float gFinalDoorTimer = 0.0f;
bool gEmergencyPromptShown = false;
bool gFinalPromptShown = false;
bool gTransitionSent = false;
Texture2D gEmergencySign{};
Texture2D gReturnSign{};
std::vector<Item> gLevel2Items;
Texture2D gConcrete{};
Texture2D gConcreteAlt{};
Texture2D gMetal{};
Model gGenerator{};

static bool In(int x, int z, int x0, int z0, int x1, int z1) {
    return x >= x0 && x <= x1 && z >= z0 && z <= z1;
}

static bool Open(int x, int z) {
    if (x < 0 || z < 0 || x >= MAP_W || z >= MAP_H) return false;
    // Центральная магистраль: ширина 6 м.
    if (In(x,z,2,20,61,22)) return true;
    // Насосная и северный технический коридор.
    if (In(x,z,4,7,15,15) || In(x,z,9,14,11,20)) return true;
    // Узкий кабельный коридор между насосной и электрощитовой.
    if (In(x,z,10,11,31,11)) return true;
    // Электрощитовая.
    if (In(x,z,25,5,36,17)) return true;
    // Южная сервисная ветка и насосный зал.
    if (In(x,z,20,22,22,33) || In(x,z,16,31,30,39)) return true;
    // Генераторный зал.
    if (In(x,z,38,22,40,29) || In(x,z,33,27,45,38)) return true;
    // Единственная голубая камера.
    if (In(x,z,43,16,58,29)) return true;
    // Аварийный боковой тамбур и выходной коридор.
    // Скрытый одноклеточный сервисный проход за аварийной дверью.
    if (In(x,z,52,5,52,15)) return true;
    if (In(x,z,56,20,61,22)) return true;
    return false;
}

static float FloorAtCell(float wx, float wz) {
    float x = wx / CELL, z = wz / CELL;
    float h = 0.0f;
    // Насосная: пониженный пол, соединённый плавной лестницей-пандусом.
    if (x >= 9.0f && x <= 11.0f && z >= 14.0f && z <= 20.0f)
        h = -0.42f * Clamp((20.0f - z) / 6.0f, 0.0f, 1.0f);
    if (x >= 20.0f && x <= 22.0f && z >= 22.0f && z <= 33.0f)
        h = -0.55f * Clamp((z - 22.0f) / 9.0f, 0.0f, 1.0f);
    if (x >= 16.0f && x <= 30.0f && z >= 31.0f && z <= 39.0f) h = -0.55f;
    // Генераторный зал приподнят относительно магистрали.
    if (x >= 38.0f && x <= 40.0f && z >= 22.0f && z <= 27.0f)
        h = 0.32f * Clamp((z - 22.0f) / 5.0f, 0.0f, 1.0f);
    if (x >= 33.0f && x <= 45.0f && z >= 27.0f && z <= 38.0f) h = 0.32f;
    // Шесть ступеней в голубой комнате ведут к верхней двери.
    if (x >= 48.0f && x < 53.0f && z >= 19.0f && z <= 23.0f)
        h = 0.21f * floorf(x - 48.0f);
    if (x > 53.0f && x <= 61.0f && z >= 19.0f && z <= 23.0f) h = 1.05f;
    // Секретный проход: двенадцать невысоких ступеней поднимаются к выходной двери.
    if (x >= 52.0f && x < 53.0f && z >= 5.0f && z < 15.0f) {
        const float step = Clamp(15.0f - floorf(z), 0.0f, 10.0f);
        h = 0.12f * step;
    }
    return h;
}

static Color Lit(Color c, float l) {
    l = Clamp(l, 0.0f, 1.0f);
    c.a = (unsigned char)(l * 255.0f);
    return c;
}
static Vector3 V(float x, float y, float z) { return {x - gOrg.x, y, z - gOrg.z}; }
static void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color,
                 unsigned int texture, float u1=1.0f, float v1=1.0f) {
    rlSetTexture(texture);
    rlCheckRenderBatchLimit(4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r,color.g,color.b,color.a); rlTexCoord2f(0,0); rlVertex3f(a.x,a.y,a.z);
    rlColor4ub(color.r,color.g,color.b,color.a); rlTexCoord2f(u1,0); rlVertex3f(b.x,b.y,b.z);
    rlColor4ub(color.r,color.g,color.b,color.a); rlTexCoord2f(u1,v1); rlVertex3f(c.x,c.y,c.z);
    rlColor4ub(color.r,color.g,color.b,color.a); rlTexCoord2f(0,v1); rlVertex3f(d.x,d.y,d.z);
    rlEnd();
}
static void FloorQuad(float x0,float z0,float x1,float z1, Color c, unsigned int tex) {
    // Чуть отступаем внутрь тайла: ступени остаются плоскими, а пандусы сохраняют уклон.
    Quad(V(x0,FloorAtCell(x0+0.01f,z0+0.01f)+0.005f,z0),
         V(x1,FloorAtCell(x1-0.01f,z0+0.01f)+0.005f,z0),
         V(x1,FloorAtCell(x1-0.01f,z1-0.01f)+0.005f,z1),
         V(x0,FloorAtCell(x0+0.01f,z1-0.01f)+0.005f,z1), c, tex, 2, 2);
}
static void WallFace(float x0,float z0,float x1,float z1,float y0,float y1,Color c,unsigned int tex) {
    Quad(V(x0,y0,z0),V(x1,y0,z1),V(x1,y1,z1),V(x0,y1,z0),c,tex,1.0f,2.0f);
}
static void Box(float x,float y,float z,float sx,float sy,float sz,Color c,unsigned int tex=0) {
    float x0=x-sx*.5f,x1=x+sx*.5f,z0=z-sz*.5f,z1=z+sz*.5f,y0=y-sy*.5f,y1=y+sy*.5f;
    Quad(V(x0,y1,z0),V(x1,y1,z0),V(x1,y1,z1),V(x0,y1,z1),c,tex,1,1);
    Quad(V(x0,y0,z1),V(x1,y0,z1),V(x1,y0,z0),V(x0,y0,z0),c,tex,1,1);
    Quad(V(x0,y0,z0),V(x1,y0,z0),V(x1,y1,z0),V(x0,y1,z0),c,tex,1,1);
    Quad(V(x1,y0,z1),V(x0,y0,z1),V(x0,y1,z1),V(x1,y1,z1),c,tex,1,1);
    Quad(V(x0,y0,z1),V(x0,y0,z0),V(x0,y1,z0),V(x0,y1,z1),c,tex,1,1);
    Quad(V(x1,y0,z0),V(x1,y0,z1),V(x1,y1,z1),V(x1,y1,z0),c,tex,1,1);
}
static void Triangle(Vector3 a, Vector3 b, Vector3 c, Color color) {
    rlSetTexture(0);
    rlCheckRenderBatchLimit(3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r,color.g,color.b,color.a); rlVertex3f(a.x,a.y,a.z);
    rlColor4ub(color.r,color.g,color.b,color.a); rlVertex3f(b.x,b.y,b.z);
    rlColor4ub(color.r,color.g,color.b,color.a); rlVertex3f(c.x,c.y,c.z);
    rlEnd();
}
static void Pipe(Vector3 a, Vector3 b, float radius, Color c) {
    Vector3 axis={b.x-a.x,b.y-a.y,b.z-a.z};
    float len=sqrtf(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
    if (len<0.001f) return;
    axis.x/=len; axis.y/=len; axis.z/=len;
    Vector3 ref = fabsf(axis.y)<0.85f ? Vector3{0,1,0} : Vector3{1,0,0};
    Vector3 u={axis.y*ref.z-axis.z*ref.y,axis.z*ref.x-axis.x*ref.z,axis.x*ref.y-axis.y*ref.x};
    float ul=sqrtf(u.x*u.x+u.y*u.y+u.z*u.z); u.x/=ul;u.y/=ul;u.z/=ul;
    Vector3 v={axis.y*u.z-axis.z*u.y,axis.z*u.x-axis.x*u.z,axis.x*u.y-axis.y*u.x};
    for(int i=0;i<8;i++) {
        float p0=2*PI*i/8.0f,p1=2*PI*(i+1)/8.0f;
        Vector3 o0={radius*(u.x*cosf(p0)+v.x*sinf(p0)),radius*(u.y*cosf(p0)+v.y*sinf(p0)),radius*(u.z*cosf(p0)+v.z*sinf(p0))};
        Vector3 o1={radius*(u.x*cosf(p1)+v.x*sinf(p1)),radius*(u.y*cosf(p1)+v.y*sinf(p1)),radius*(u.z*cosf(p1)+v.z*sinf(p1))};
        Quad(V(a.x+o0.x,a.y+o0.y,a.z+o0.z),V(a.x+o1.x,a.y+o1.y,a.z+o1.z),
             V(b.x+o1.x,b.y+o1.y,b.z+o1.z),V(b.x+o0.x,b.y+o0.y,b.z+o0.z),c,0,1,1);
        // Тонкое соединительное кольцо у начала и конца каждого пролёта.
        if (i == 0 || i == 4) {
            const float k = i == 0 ? 0.055f : 0.945f;
            Vector3 c0={a.x+axis.x*len*k,a.y+axis.y*len*k,a.z+axis.z*len*k};
            Vector3 c1={c0.x+axis.x*0.075f,c0.y+axis.y*0.075f,c0.z+axis.z*0.075f};
            float rr=radius*1.17f;
            Vector3 r0={rr*(u.x*cosf(p0)+v.x*sinf(p0)),rr*(u.y*cosf(p0)+v.y*sinf(p0)),rr*(u.z*cosf(p0)+v.z*sinf(p0))};
            Vector3 r1={rr*(u.x*cosf(p1)+v.x*sinf(p1)),rr*(u.y*cosf(p1)+v.y*sinf(p1)),rr*(u.z*cosf(p1)+v.z*sinf(p1))};
            Quad(V(c0.x+r0.x,c0.y+r0.y,c0.z+r0.z),V(c0.x+r1.x,c0.y+r1.y,c0.z+r1.z),
                 V(c1.x+r1.x,c1.y+r1.y,c1.z+r1.z),V(c1.x+r0.x,c1.y+r0.y,c1.z+r0.z),Lit(Color{170,178,181,255},0.8f),0);
        }
    }
    // Закрытые торцы: трубы больше не выглядят полыми оболочками.
    for (int end=0; end<2; ++end) {
        Vector3 center=end?b:a;
        for(int i=0;i<8;i++) {
            float p0=2*PI*i/8.0f,p1=2*PI*(i+1)/8.0f;
            Vector3 q0={radius*(u.x*cosf(p0)+v.x*sinf(p0)),radius*(u.y*cosf(p0)+v.y*sinf(p0)),radius*(u.z*cosf(p0)+v.z*sinf(p0))};
            Vector3 q1={radius*(u.x*cosf(p1)+v.x*sinf(p1)),radius*(u.y*cosf(p1)+v.y*sinf(p1)),radius*(u.z*cosf(p1)+v.z*sinf(p1))};
            Vector3 ctr=V(center.x,center.y,center.z);
            Triangle(ctr,V(center.x+q0.x,center.y+q0.y,center.z+q0.z),V(center.x+q1.x,center.y+q1.y,center.z+q1.z),Lit(Color{83,94,101,255},0.78f));
        }
    }
}
static float CellLight(int x,int z) {
    // Заранее заданные тёмные зоны для появления Физички.
    if ((In(x,z,4,7,15,15) || In(x,z,25,5,36,17) ||
         In(x,z,16,31,30,39) || In(x,z,33,27,45,38)) || In(x,z,52,5,52,15)) return 0.045f;
    if (In(x,z,43,16,58,29)) return 0.18f;
    if (In(x,z,2,20,61,22)) return 0.24f;
    return 0.13f;
}
}

void Level2SetActive(bool active) { gActive=active; }
bool Level2IsActive() { return gActive; }
void Level2Reset() {
    gAirlockClosed=false; gAirlockClosing=false; gAirlockTimer=0.0f;
    gEmergencyDoorOpen=false; gEmergencyDoorAnim=0.0f;
    gFinalDoorOpening=false; gFinalDoorTimer=0.0f;
    gEmergencyPromptShown=false; gFinalPromptShown=false; gTransitionSent=false;
    gLevel2Items = {
        {7,21,IT_WATER,false}, {16,21,IT_BATTERY,false},
        {29,11,IT_MEDKIT,false}, {34,20,IT_BATTERY,false},
        {40,34,IT_MEDKIT,false}, {47,25,IT_WATER,false},
        {52,10,IT_BATTERY,false}, {22,35,IT_MEDKIT,false}
    };
}
void Level2Load() {
    if (gLoaded) return;
    gConcrete=LoadTexture("level2/textures/Concrete046_Color.jpg");
    gConcreteAlt=LoadTexture("level2/textures/Concrete048_Color.jpg");
    gMetal=LoadTexture("level2/textures/Metal024_Color.jpg");
    gGenerator=LoadModel("level2/models/portable_generator/portable_generator_1k.gltf");
    // Указатели надписей создаются один раз из системного raylib-шрифта.
    Image emergency = GenImageColor(640, 180, Color{28,34,38,255});
    Font signFont = GetFontDefault();
    ImageDrawTextEx(&emergency, signFont, "FIRE EXIT", Vector2{30,12}, 58, 2, Color{238,225,190,255});
    ImageDrawTextEx(&emergency, signFont, "KEEP CLEAR", Vector2{30,94}, 42, 2, Color{238,110,86,255});
    gEmergencySign = LoadTextureFromImage(emergency); UnloadImage(emergency);
    Image back = GenImageColor(512, 150, Color{24,38,50,255});
    ImageDrawTextEx(&back, signFont, "LEVEL 0  /  EXIT", Vector2{18,48}, 42, 2, Color{135,205,255,255});
    gReturnSign = LoadTextureFromImage(back); UnloadImage(back);
    Texture2D* ts[]={&gConcrete,&gConcreteAlt,&gMetal};
    for(Texture2D* t:ts) if(t->id) { SetTextureWrap(*t,TEXTURE_WRAP_REPEAT); SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR); }
    gLoaded=true;
}
void Level2Unload() {
    if(gGenerator.meshCount>0) UnloadModel(gGenerator);
    gGenerator={};
    if(gConcrete.id) UnloadTexture(gConcrete);
    if(gConcreteAlt.id) UnloadTexture(gConcreteAlt);
    if(gMetal.id) UnloadTexture(gMetal);
    if(gEmergencySign.id) UnloadTexture(gEmergencySign);
    if(gReturnSign.id) UnloadTexture(gReturnSign);
    gConcrete={};gConcreteAlt={};gMetal={};gEmergencySign={};gReturnSign={};gLoaded=false;
}

bool Level2IsSolid(int cx,int cz) {
    if(!Open(cx,cz)) return true;
    // Герметичный вход в голубую камеру закрывается после прохода игрока.
    if(gAirlockClosed && cx==42 && cz>=20 && cz<=22) return true;
    return false;
}
bool Level2CircleHits(float x,float z,float r) {
    int x0=CellOf(x-r),x1=CellOf(x+r),z0=CellOf(z-r),z1=CellOf(z+r);
    for(int cz=z0;cz<=z1;cz++) for(int cx=x0;cx<=x1;cx++) {
        if(!Level2IsSolid(cx,cz)) continue;
        float bx=cx*CELL,bz=cz*CELL;
        float qx=Clamp(x,bx,bx+CELL),qz=Clamp(z,bz,bz+CELL);
        float dx=x-qx,dz=z-qz;
        if(dx*dx+dz*dz<r*r) return true;
    }
    // Закрытая створка перекрывает проход по плоскости X=86 м.
    if(gAirlockClosed && fabsf(x-BLUE_ENTRY_X)<r+0.08f && z>40.0f && z<46.0f) return true;
    // Сплошная поперечная стена голубой камеры. Проход существует только за узкой аварийной дверью.
    if (x > 86.0f-r && x < 116.0f+r && fabsf(z-31.70f) < r+0.08f) {
        const bool doorway = gEmergencyDoorOpen && x > 104.05f-r && x < 105.95f+r;
        if (!doorway) return true;
    }
    // Конечная дверь в торце секретного коридора; открывается только вручную.
    if (!gFinalDoorOpening && x > 104.0f-r && x < 106.0f+r && fabsf(z-10.0f) < r+0.10f) return true;
    // Низкие вертикальные трубы насосной имеют физический объём, а не только рисунок.
    for (int i=0;i<3;i++) {
        const float px=(6.0f+i*3.0f)*CELL-0.65f, pz=10.5f*CELL;
        const float dx=x-px, dz=z-pz;
        if (dx*dx+dz*dz < (r+0.15f)*(r+0.15f)) return true;
    }
    return false;
}
float Level2FloorAt(float wx,float wz) { return FloorAtCell(wx,wz); }
bool Level2CanMoveTo(float x,float z,float radius,float currentFloor,float* newFloor) {
    if(Level2CircleHits(x,z,radius)) return false;
    float h=Level2FloorAt(x,z);
    if(fabsf(h-currentFloor)>0.24f) return false;
    if(newFloor) *newFloor=h;
    return true;
}
float Level2LightAt(float wx,float wz) {
    int x=CellOf(wx),z=CellOf(wz);
    float l=CellLight(x,z);
    // Локальные технические светильники дают мягкие зоны света.
    const Vector2 lamps[]={{20,21},{37,21},{51,17},{55,25},{53,11}};
    for(const Vector2& p:lamps) {
        float dx=wx/CELL-(p.x+0.5f),dz=wz/CELL-(p.y+0.5f);
        float d=sqrtf(dx*dx+dz*dz);
        l=fmaxf(l,0.055f+0.30f*Clamp(1.0f-d/5.0f,0.0f,1.0f));
    }
    if(In(x,z,43,16,58,29)) l=fmaxf(l,0.14f);
    // Голубая лампа секретного прохода: локальный радиус с плавным спадом.
    const float bdx=wx-105.0f, bdz=wz-20.0f;
    const float bd=sqrtf(bdx*bdx+bdz*bdz);
    const float blue=0.045f+0.66f*Clamp(1.0f-bd/13.5f,0.0f,1.0f);
    if (In(x,z,52,5,52,15)) l=fmaxf(l,blue);
    return Clamp(l,0.035f,0.72f);
}
bool Level2IsDark(float wx,float wz) { return Level2LightAt(wx,wz)<0.12f; }
Vector2 Level2PatrolPoint(int index) {
    static const Vector2 points[]={{7,10},{30,15},{22,35},{39,35},{7,12},{33,14}};
    Vector2 c=points[(index<0?-index:index)%6];
    return {(c.x+0.5f)*CELL,(c.y+0.5f)*CELL};
}
Item* Level2ItemNear(float wx,float wz,float radius) {
    for (Item& item : gLevel2Items) {
        if (item.taken) continue;
        const float ix=(item.cx+0.5f)*CELL, iz=(item.cz+0.5f)*CELL;
        if (hypotf(ix-wx,iz-wz) < radius) return &item;
    }
    return nullptr;
}
void Level2TakeItem(Item* item) { if (item) item->taken=true; }
const std::vector<Item>& Level2GetItems() { return gLevel2Items; }

bool Level2Interact(Player& p) {
    if (!gActive || gTransitionSent) return false;
    const float doorX=105.0f, emergencyZ=31.70f;
    if (!gEmergencyDoorOpen && hypotf(p.pos.x-doorX,p.pos.z-emergencyZ) < 2.15f) {
        gEmergencyDoorOpen=true;
        gEmergencyDoorAnim=0.0f;
        Say(T("FIRE EXIT RELEASED. THE PASSAGE IS OPEN.","АВАРИЙНЫЙ ВЫХОД ОТКРЫТ. ПРОХОД СВОБОДЕН."),4.0f);
        Haptic(180);
        return true;
    }
    if (gEmergencyDoorOpen && !gFinalDoorOpening &&
        hypotf(p.pos.x-doorX,p.pos.z-10.0f) < 2.35f) {
        gFinalDoorOpening=true;
        gFinalDoorTimer=0.85f;
        Say(T("MANUAL EXIT OPENING...", "РУЧНОЕ ОТКРЫТИЕ ВЫХОДА..."),3.0f);
        Haptic(220);
        return true;
    }
    return false;
}

bool Level2Update(Player& p,float dt) {
    if(!gActive || gTransitionSent) return false;
    if (!gEmergencyDoorOpen && !gEmergencyPromptShown && hypotf(p.pos.x-105.0f,p.pos.z-31.70f)<3.2f) {
        gEmergencyPromptShown=true;
        Say(T("RECESSED FIRE EXIT. PRESS THROW TO OPEN.","СКРЫТАЯ АВАРИЙНАЯ ДВЕРЬ. НАЖМИТЕ «БРОСОК», ЧТОБЫ ОТКРЫТЬ."),4.0f);
    }
    if (gEmergencyDoorOpen && !gFinalPromptShown && hypotf(p.pos.x-105.0f,p.pos.z-10.0f)<3.6f) {
        gFinalPromptShown=true;
        Say(T("MANUAL EXIT DOOR. PRESS THROW TO RETURN.","РУЧНАЯ ВЫХОДНАЯ ДВЕРЬ. НАЖМИТЕ «БРОСОК», ЧТОБЫ ВЕРНУТЬСЯ."),4.0f);
    }
    const bool enteredBlue = p.pos.x>BLUE_ENTRY_X+2.0f && p.pos.x<FINAL_DOOR_X && p.pos.z>38.0f && p.pos.z<60.0f;
    if(enteredBlue && !gAirlockClosed && !gAirlockClosing) {
        gAirlockClosing=true; gAirlockTimer=0.85f;
        Say(T("AIRLOCK CLOSING...", "ШЛЮЗ ЗАКРЫВАЕТСЯ..."),2.5f);
        Haptic(90);
    }
    if(gAirlockClosing && !gAirlockClosed) {
        gAirlockTimer-=dt;
        if(gAirlockTimer<=0.0f) {
            gAirlockClosed=true; gAirlockClosing=false;
            Say(T("AIRLOCK SEALED. The pressure behind you has changed.","ШЛЮЗ ЗАКРЫТ. ДАВЛЕНИЕ ЗА СПИНОЙ ИЗМЕНИЛОСЬ."),4.0f);
        }
    }
    if (gEmergencyDoorOpen) gEmergencyDoorAnim=Clamp(gEmergencyDoorAnim+dt*1.8f,0.0f,1.0f);
    if (gFinalDoorOpening) {
        gFinalDoorTimer-=dt;
        if (gFinalDoorTimer<=0.0f) { gTransitionSent=true; return true; }
    }
    return false;
}

void Level2Draw(const Player& p,float time,float viewDistance) {
    (void)time;
    const int pcx=CellOf(p.pos.x),pcz=CellOf(p.pos.z);
    const int range=(int)ceilf(viewDistance/CELL)+2;
    const int x0=std::max(0,pcx-range),x1=std::min(MAP_W-1,pcx+range);
    const int z0=std::max(0,pcz-range),z1=std::min(MAP_H-1,pcz+range);
    const unsigned int concrete=gConcrete.id, alt=gConcreteAlt.id, metal=gMetal.id;
    rlSetTexture(0);
    for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++) {
        if(!Open(x,z)) continue;
        const float wx=x*CELL,wz=z*CELL,wx1=wx+CELL,wz1=wz+CELL;
        const bool blue=In(x,z,43,16,58,29);
        const bool secret=In(x,z,52,5,52,15);
        const float light=Level2LightAt(wx+CELL*.5f,wz+CELL*.5f);
        Color floorC=secret?Color{64,112,150,255}:(blue?Color{80,132,194,255}:Color{145,151,153,255});
        Color ceilC=secret?Color{55,101,142,255}:(blue?Color{80,126,186,255}:Color{124,130,132,255});
        if(Level2IsSolid(x,z)) continue;
        FloorQuad(wx,wz,wx1,wz1,Lit(floorC,fmaxf(.045f,light)),blue?alt:concrete);
        Quad(V(wx,CEILING_H,wz),V(wx1,CEILING_H,wz),V(wx1,CEILING_H,wz1),V(wx,CEILING_H,wz1),Lit(ceilC,fmaxf(.04f,light*.9f)),alt,1,1);
        const float y0=FloorAtCell(wx+0.01f,wz+0.01f);
        const float y1=y0+CEILING_H;
        const float stepY=FloorAtCell(wx+CELL*.5f,wz+CELL*.5f);
        Color wall=secret?Color{61,104,139,255}:(blue?Color{70,116,174,255}:Color{135,139,140,255});
        if(Level2IsSolid(x,z-1)) WallFace(wx,wz,wx1,wz,y0,y1,Lit(wall,light),blue?alt:concrete);
        else { float n=FloorAtCell(wx+CELL*.5f,wz-CELL*.5f); if(stepY>n+0.18f) WallFace(wx,wz,wx1,wz,n+0.005f,stepY+0.005f,Lit(Color{118,128,132,255},light),metal); }
        if(Level2IsSolid(x,z+1)) WallFace(wx1,wz1,wx,wz1,y0,y1,Lit(wall,light*.92f),blue?alt:concrete);
        else { float n=FloorAtCell(wx+CELL*.5f,wz1+CELL*.5f); if(stepY>n+0.18f) WallFace(wx1,wz1,wx,wz1,n+0.005f,stepY+0.005f,Lit(Color{118,128,132,255},light),metal); }
        if(Level2IsSolid(x-1,z)) WallFace(wx,wz1,wx,wz,y0,y1,Lit(wall,light*.86f),blue?alt:concrete);
        else { float n=FloorAtCell(wx-CELL*.5f,wz+CELL*.5f); if(stepY>n+0.18f) WallFace(wx,wz1,wx,wz,n+0.005f,stepY+0.005f,Lit(Color{118,128,132,255},light),metal); }
        if(Level2IsSolid(x+1,z)) WallFace(wx1,wz,wx1,wz1,y0,y1,Lit(wall,light*.82f),blue?alt:concrete);
        else { float n=FloorAtCell(wx1+CELL*.5f,wz+CELL*.5f); if(stepY>n+0.18f) WallFace(wx1,wz,wx1,wz1,n+0.005f,stepY+0.005f,Lit(Color{118,128,132,255},light),metal); }
    }
    // Открытая инженерная разводка: магистральные трубы и крепления.
    for(int x=5;x<43;x+=4) {
        float z=20.4f;
        Pipe({x*CELL,3.05f,z*CELL},{(x+3)*CELL,3.05f,z*CELL},0.085f,Lit(Color{120,132,139,255},0.72f));
        Pipe({x*CELL,3.28f,(z+0.38f)*CELL},{(x+3)*CELL,3.28f,(z+0.38f)*CELL},0.045f,Lit(Color{174,88,55,255},0.65f));
        for(int k=0;k<4;k++) {
            Box((x+k)*CELL,3.55f,z*CELL,0.10f,0.55f,0.10f,Lit(Color{83,91,96,255},0.65f),metal);
            Box((x+k)*CELL,3.82f,z*CELL,0.42f,0.06f,0.28f,Lit(Color{100,108,112,255},0.72f),metal);
        }
    }
    // Насосный зал: насосные корпуса, трубные колена и маховики.
    for(int i=0;i<3;i++) {
        float x=(6+i*3)*CELL,z=10.5f*CELL;
        Box(x,0.62f,z,1.55f,1.10f,1.35f,Lit(Color{67,78,84,255},0.42f),metal);
        Box(x,1.22f,z,0.85f,0.15f,0.85f,Lit(Color{143,150,151,255},0.55f),metal);
        Pipe({x-0.65f,1.10f,z},{x-0.65f,2.60f,z},0.12f,Lit(Color{118,130,136,255},0.52f));
        Pipe({x-0.65f,2.60f,z},{x+0.65f,2.60f,z},0.12f,Lit(Color{118,130,136,255},0.52f));
        Box(x+0.78f,1.45f,z,0.10f,0.55f,0.10f,Lit(Color{196,66,43,255},0.8f),metal);
    }
    // Электрощитовая: шкафы, дверцы и индикаторы.
    for(int i=0;i<5;i++) {
        float x=(26+i*1.65f)*CELL,z=7.2f*CELL;
        Box(x,1.35f,z,1.15f,2.55f,0.62f,Lit(Color{67,76,79,255},0.55f),metal);
        Box(x,1.45f,z-0.34f,0.88f,2.10f,0.035f,Lit(Color{94,103,105,255},0.58f),metal);
        Box(x-0.31f,2.1f,z-0.37f,0.10f,0.055f,0.035f,Lit(Color{226,88,47,255},0.85f));
        Box(x-0.31f,1.9f,z-0.37f,0.10f,0.055f,0.035f,Lit(Color{78,210,139,255},0.85f));
    }
    // Генераторный зал: тяжёлые агрегаты, воздуховоды и кабельные лотки.
    for(int i=0;i<2;i++) {
        float x=(35+i*4)*CELL,z=32.0f*CELL;
        float floorY=Level2FloorAt(x,z);
        Box(x,floorY+0.95f,z,3.0f,1.75f,1.65f,Lit(Color{60,69,73,255},0.55f),metal);
        for(int k=0;k<6;k++) Box(x-1.12f+k*0.45f,floorY+0.98f,z-0.84f,0.06f,1.25f,0.06f,Lit(Color{138,147,149,255},0.5f),metal);
        Pipe({x,floorY+1.85f,z},{x,floorY+3.15f,z},0.16f,Lit(Color{122,130,135,255},0.62f));
    }
    // Голубая камера: холодные световые панели, силовой шлюз и верхняя площадка.
    {
        Color blueGlow=Lit(Color{66,159,255,255},0.96f);
        for(int x=45;x<=55;x+=5) {
            Box(x*CELL,3.52f,21*CELL,2.4f,0.10f,0.32f,blueGlow);
            Box(x*CELL,1.75f,14.4f*CELL,0.07f,2.3f,0.07f,blueGlow);
        }
        // Тяжёлые створки западного шлюза; закрытое состояние фиксируется триггером.
        Color door=Lit(Color{65,75,82,255},0.72f);
        const float closeT=gAirlockClosed?1.0f:(gAirlockClosing?Clamp(1.0f-gAirlockTimer/0.85f,0.0f,1.0f):0.0f);
        const float panelA=39.8f+(41.0f-39.8f)*closeT;
        const float panelB=46.2f+(45.0f-46.2f)*closeT;
        Box(43.0f*CELL,1.85f,panelA,0.22f,3.55f,2.8f,door,metal);
        Box(43.0f*CELL,1.85f,panelB,0.22f,3.55f,2.8f,door,metal);
        // Пять ступеней и площадка под верхней дверью.
        for(int i=0;i<5;i++) {
            float x=(48.5f+i)*CELL;
            float top=0.21f*i;
            float blockH=fmaxf(0.07f,top);
            Box(x,top-blockH*.5f,42.0f,2.02f,blockH,7.0f,Lit(Color{89,100,108,255},0.72f),metal);
            Box(x,top+0.045f,42.0f,2.01f,0.075f,7.0f,Lit(Color{160,173,179,255},0.72f),metal);
        }
        // Открытая верхняя дверь с коробкой и створкой, оставляющей проход.
        Box(56.8f*CELL,1.80f,39.25f,0.20f,3.55f,0.28f,door,metal);
        Box(56.8f*CELL,1.80f,44.75f,0.20f,3.55f,0.28f,door,metal);
        Box(56.8f*CELL,3.52f,42.0f,0.20f,0.22f,5.55f,door,metal);
        Box(56.1f*CELL,1.80f,39.55f,1.20f,3.35f,0.14f,door,metal);
        // Порог — заметный металлический профиль непосредственно перед дверью.
        Box(56.1f*CELL,1.12f,42.0f,0.34f,0.18f,5.3f,Lit(Color{202,176,91,255},0.92f),metal);
        // Скрытый аварийный проход: бетонная перегородка не даёт увидеть коридор заранее.
        const float wallZ=31.70f, wallLeft=86.0f, wallRight=116.0f, doorX=105.0f;
        Color partition=Lit(Color{112,120,123,255},0.78f);
        WallFace(wallLeft,wallZ,doorX-0.82f,wallZ,0.0f,CEILING_H,partition,concrete);
        WallFace(doorX+0.82f,wallZ,wallRight,wallZ,0.0f,CEILING_H,partition,concrete);
        WallFace(doorX-0.82f,wallZ,doorX+0.82f,wallZ,2.42f,CEILING_H,partition,concrete);
        // Глубокая коробка и узкое полотно со сдвигом в боковой карман.
        Box(doorX-0.82f,1.24f,wallZ-0.13f,0.16f,2.48f,0.30f,door,metal);
        Box(doorX+0.82f,1.24f,wallZ-0.13f,0.16f,2.48f,0.30f,door,metal);
        Box(doorX,2.48f,wallZ-0.13f,1.72f,0.16f,0.30f,door,metal);
        const float slide=1.85f*gEmergencyDoorAnim;
        Box(doorX-slide,1.18f,wallZ-0.22f,1.42f,2.30f,0.12f,Lit(Color{65,76,82,255},0.92f),metal);
        Box(doorX,0.09f,wallZ+0.03f,1.78f,0.18f,0.50f,Lit(Color{198,166,91,255},0.95f),metal);
        if(gEmergencySign.id) {
            rlSetTexture(gEmergencySign.id);
            const float sx=doorX-0.65f, ex=doorX+0.65f, sy=2.55f, ey=3.17f, sz=wallZ+0.045f;
            Quad(V(sx,sy,sz),V(ex,sy,sz),V(ex,ey,sz),V(sx,ey,sz),WHITE,gEmergencySign.id,1,1);
            rlSetTexture(0);
        }
        // Следы на полу ведут к створке, без прямой видимости в секретный коридор.
        for(int i=0;i<5;i++) {
            float zz=33.0f+i*0.78f, xx=doorX+((i&1)?0.24f:-0.22f);
            Box(xx,0.012f,zz,0.22f,0.012f,0.39f,Lit(Color{102,24,28,255},0.52f));
            Box(xx+0.06f,0.014f,zz-0.17f,0.12f,0.012f,0.12f,Lit(Color{122,31,35,255},0.46f));
        }
        // Небольшие стрелки на стенах. Две намеренно указывают не к выходу.
        for(int i=0;i<4;i++) {
            const float sx=22.0f+i*22.0f;
            const bool east=(i==0 || i==3);
            const float faceZ=40.0f;
            Box(sx,2.42f,faceZ,0.92f,0.36f,0.045f,Lit(Color{48,58,57,255},0.90f),metal);
            Color arrow=Lit((i==1 || i==2)?Color{220,174,77,255}:Color{167,198,150,255},0.95f);
            Box(sx,2.42f,faceZ+0.045f,0.34f,0.045f,0.025f,arrow);
            const float tip=east?sx+0.22f:sx-0.22f;
            Triangle(V(tip,2.42f,faceZ+0.055f),V(east?sx+0.05f:sx-0.05f,2.54f,faceZ+0.055f),V(east?sx+0.05f:sx-0.05f,2.30f,faceZ+0.055f),arrow);
        }
        // Секретный коридор: двенадцать коротких ступеней, узкие стены и голубая лампа.
        for(int z=5;z<15;z++) {
            float top=0.12f*(15-z);
            Box(105.0f,top*0.5f,(z+0.5f)*CELL,1.94f,fmaxf(0.08f,top),1.98f,Lit(Color{78,105,119,255},0.86f),metal);
            Box(105.0f,top+0.018f,(z+0.5f)*CELL,1.94f,0.035f,1.98f,Lit(Color{143,166,177,255},0.88f),metal);
        }
        Box(105.0f,3.55f,20.0f,0.42f,0.16f,0.72f,Lit(Color{67,153,255,255},1.0f),metal);
        Box(105.0f,3.38f,20.0f,1.12f,0.045f,1.32f,Lit(Color{55,135,245,255},0.58f));
        Box(104.10f,1.85f,20.0f,0.045f,1.7f,2.2f,Lit(Color{66,139,207,255},0.55f),metal);
        Box(105.90f,1.85f,20.0f,0.045f,1.7f,2.2f,Lit(Color{66,139,207,255},0.55f),metal);
        // Конечная дверь Level 0 — отдельная, ручная, за последней ступенью.
        const float endZ=10.0f, endFloor=1.20f;
        Box(104.12f,endFloor+1.18f,endZ+0.14f,0.16f,2.36f,0.30f,door,metal);
        Box(105.88f,endFloor+1.18f,endZ+0.14f,0.16f,2.36f,0.30f,door,metal);
        Box(105.0f,endFloor+2.36f,endZ+0.14f,1.92f,0.16f,0.30f,door,metal);
        const float finalSlide=gFinalDoorOpening?1.65f*Clamp(1.0f-gFinalDoorTimer/0.85f,0.0f,1.0f):0.0f;
        Box(105.0f-finalSlide,endFloor+1.14f,endZ+0.22f,1.55f,2.28f,0.12f,Lit(Color{56,75,91,255},0.96f),metal);
        Box(105.0f,endFloor+0.08f,endZ+0.22f,1.94f,0.16f,0.44f,Lit(Color{197,174,106,255},0.95f),metal);
        if(gReturnSign.id) {
            rlSetTexture(gReturnSign.id);
            Quad(V(104.18f,endFloor+2.55f,endZ+0.31f),V(105.82f,endFloor+2.55f,endZ+0.31f),
                 V(105.82f,endFloor+2.98f,endZ+0.31f),V(104.18f,endFloor+2.98f,endZ+0.31f),WHITE,gReturnSign.id,1,1);
            rlSetTexture(0);
        }
    }
    rlSetTexture(0);
}

void Level2DrawModels() {
    if(!gActive || gGenerator.meshCount<=0) return;
    // Отдельный стандартный raylib-проход: генератор использует собственные PBR-текстуры glTF.
    Vector3 position={70.0f-gOrg.x, Level2FloorAt(70.0f,68.0f)+0.04f, 68.0f-gOrg.z};
    DrawModelEx(gGenerator,position,Vector3{0,1,0},90.0f,Vector3{2.15f,2.15f,2.15f},WHITE);
}
