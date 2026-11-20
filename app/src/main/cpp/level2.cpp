#include "level2.h"
#include "util.h"
#include "map_gen.h"
#include "rlgl.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
bool gTrapStarted = false;
bool gTransitionSent = false;
float gTrapTimer = 0.0f;
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
    if (In(x,z,10,10,31,12)) return true;
    // Электрощитовая.
    if (In(x,z,25,5,36,17)) return true;
    // Южная сервисная ветка и насосный зал.
    if (In(x,z,20,22,22,33) || In(x,z,16,31,30,39)) return true;
    // Генераторный зал.
    if (In(x,z,38,22,40,29) || In(x,z,33,27,45,38)) return true;
    // Единственная голубая камера.
    if (In(x,z,43,14,58,29)) return true;
    // Аварийный боковой тамбур и выходной коридор.
    if (In(x,z,50,8,55,15)) return true;
    if (In(x,z,56,20,61,22)) return true;
    return false;
}

static float FloorAtCell(float wx, float wz) {
    float x = wx / CELL, z = wz / CELL;
    float h = 0.0f;
    // Насосная: пониженный пол, соединённый плавной лестницей-пандусом.
    if (x >= 9.0f && x <= 11.0f && z >= 13.0f && z <= 20.0f) {
        if (z < 14.0f)
            h = -0.42f * Clamp(z - 13.0f, 0.0f, 1.0f);
        else
            h = -0.42f * Clamp((20.0f - z) / 6.0f, 0.0f, 1.0f);
    }
    if (x >= 20.0f && x <= 22.0f && z >= 22.0f && z <= 33.0f)
        h = -0.55f * Clamp((z - 22.0f) / 9.0f, 0.0f, 1.0f);
    if (x >= 16.0f && x <= 30.0f && z >= 31.0f && z <= 39.0f) h = -0.55f;
    // Генераторный зал приподнят относительно магистрали.
    if (x >= 38.0f && x <= 40.0f && z >= 22.0f && z <= 27.0f)
        h = 0.32f * Clamp((z - 22.0f) / 5.0f, 0.0f, 1.0f);
    if (x >= 33.0f && x <= 45.0f && z >= 27.0f && z <= 38.0f) h = 0.32f;
    // Шесть ступеней в голубой комнате ведут к верхней двери.
    if (x >= 48.0f && x < 54.0f && z >= 19.0f && z <= 23.0f)
        h = 0.21f * floorf(x - 48.0f);
    if (x >= 54.0f && x <= 61.0f && z >= 19.0f && z <= 23.0f)
        h = 1.05f;
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
    }
}
static float CellLight(int x,int z) {
    // Заранее заданные тёмные зоны для появления Физички.
    if (In(x,z,4,7,15,15) || In(x,z,25,5,36,17) ||
        In(x,z,16,31,30,39) || In(x,z,33,27,45,38)) return 0.045f;
    if (In(x,z,43,14,58,29)) return 0.18f;
    if (In(x,z,2,20,61,22)) return 0.24f;
    return 0.13f;
}
}

void Level2SetActive(bool active) { gActive=active; }
bool Level2IsActive() { return gActive; }
void Level2Reset() {
    gAirlockClosed=false; gAirlockClosing=false; gAirlockTimer=0.0f;
    gTrapStarted=false; gTransitionSent=false; gTrapTimer=0.0f;
}
void Level2Load() {
    if (gLoaded) return;
    gConcrete=LoadTexture("level2/textures/Concrete046_Color.jpg");
    gConcreteAlt=LoadTexture("level2/textures/Concrete048_Color.jpg");
    gMetal=LoadTexture("level2/textures/Metal024_Color.jpg");
    gGenerator=LoadModel("level2/models/portable_generator/portable_generator_1k.gltf");
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
    gConcrete={};gConcreteAlt={};gMetal={};gLoaded=false;
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
    // Аварийная дверь бокового тамбура заперта: видимое полотно действительно блокирует проход.
    if(x>99.7f && x<112.3f && fabsf(z-31.70f)<r+0.10f) return true;
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
    if(In(x,z,43,14,58,29)) l=fmaxf(l,0.14f);
    return Clamp(l,0.035f,0.48f);
}
bool Level2IsDark(float wx,float wz) { return Level2LightAt(wx,wz)<0.12f; }
Vector2 Level2PatrolPoint(int index) {
    static const Vector2 points[]={{7,10},{30,15},{22,35},{39,35},{7,12},{33,14}};
    Vector2 c=points[(index<0?-index:index)%6];
    return {(c.x+0.5f)*CELL,(c.y+0.5f)*CELL};
}
bool Level2Update(Player& p,float dt) {
    if(!gActive || gTransitionSent) return false;
    const bool enteredBlue = p.pos.x>BLUE_ENTRY_X+2.0f && p.pos.x<FINAL_DOOR_X && p.pos.z>38.0f && p.pos.z<60.0f;
    if(enteredBlue && !gAirlockClosed && !gAirlockClosing) {
        gAirlockClosing=true;
        gAirlockTimer=0.85f;
        Say(T("AIRLOCK CLOSING...", "ШЛЮЗ ЗАКРЫВАЕТСЯ..."),2.5f);
        Haptic(90);
    }
    if(gAirlockClosing && !gAirlockClosed) {
        gAirlockTimer-=dt;
        if(gAirlockTimer<=0.0f) {
            gAirlockClosed=true;
            gAirlockClosing=false;
            Say(T("AIRLOCK SEALED. The pressure behind you has changed.","ШЛЮЗ ЗАКРЫТ. Давление за спиной изменилось."),4.0f);
        }
    }
    // Переход через металлический порог верхней двери запускает ловушку.
    if(p.pos.x>FINAL_DOOR_X+0.2f && p.pos.z>38.0f && p.pos.z<48.0f && !gTrapStarted) {
        gTrapStarted=true; gTrapTimer=1.05f;
        Say(T("ACCESS GRANTED. MOVEMENT DETECTED BEHIND YOU.","ДОСТУП ОТКРЫТ. ПОЗАДИ ОБНАРУЖЕНО ДВИЖЕНИЕ."),3.5f);
        Haptic(220);
    }
    if(gTrapStarted) {
        gTrapTimer-=dt;
        if(gTrapTimer<=0.0f) {
            gTransitionSent=true;
            return true;
        }
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
        const bool blue=In(x,z,43,14,58,29);
        const float light=Level2LightAt(wx+CELL*.5f,wz+CELL*.5f);
        Color floorC=blue?Color{80,132,194,255}:Color{145,151,153,255};
        Color ceilC=blue?Color{80,126,186,255}:Color{124,130,132,255};
        if(Level2IsSolid(x,z)) continue;
        FloorQuad(wx,wz,wx1,wz1,Lit(floorC,fmaxf(.045f,light)),blue?alt:concrete);
        Quad(V(wx,CEILING_H,wz),V(wx1,CEILING_H,wz),V(wx1,CEILING_H,wz1),V(wx,CEILING_H,wz1),Lit(ceilC,fmaxf(.04f,light*.9f)),alt,1,1);
        const float y0=FloorAtCell(wx+0.01f,wz+0.01f);
        const float y1=y0+CEILING_H;
        const float stepY=FloorAtCell(wx+CELL*.5f,wz+CELL*.5f);
        Color wall=blue?Color{70,116,174,255}:Color{135,139,140,255};
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
        for(int k=0;k<4;k++) Box((x+k)*CELL,3.55f,z*CELL,0.10f,0.55f,0.10f,Lit(Color{83,91,96,255},0.65f),metal);
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
        // Шесть ступеней и площадка под верхней дверью.
        for(int i=0;i<6;i++) {
            float x=(48.5f+i)*CELL;
            float top=0.21f*i;
            float blockH=fmaxf(0.07f,top);
            Box(x,top-blockH*.5f,42.0f,2.02f,blockH,7.0f,Lit(Color{89,100,108,255},0.72f),metal);
            Box(x,top-0.0375f,42.0f,2.01f,0.075f,7.0f,Lit(Color{160,173,179,255},0.72f),metal);
        }
        // Открытая верхняя дверь с коробкой и створкой, оставляющей проход.
        Box(56.8f*CELL,1.80f,39.25f,0.20f,3.55f,0.28f,door,metal);
        Box(56.8f*CELL,1.80f,44.75f,0.20f,3.55f,0.28f,door,metal);
        Box(56.8f*CELL,3.52f,42.0f,0.20f,0.22f,5.55f,door,metal);
        Box(56.1f*CELL,1.80f,39.55f,1.20f,3.35f,0.14f,door,metal);
        // Порог — заметный металлический профиль непосредственно перед дверью.
        Box(56.1f*CELL,1.12f,42.0f,0.34f,0.18f,5.3f,Lit(Color{202,176,91,255},0.92f),metal);
        // Аварийная дверь бокового тамбура: коробка, полотно и сигнальная панель.
        Box(51.5f*CELL,1.78f,15.85f*CELL,5.95f,3.55f,0.16f,door,metal);
        Box(54.5f*CELL,1.78f,15.85f*CELL,5.95f,3.55f,0.16f,door,metal);
        Box(53.0f*CELL,3.05f,15.70f*CELL,12.2f,0.18f,0.35f,Lit(Color{90,101,108,255},0.78f),metal);
        Box(52.2f*CELL,2.86f,15.55f*CELL,1.7f,0.16f,0.12f,Lit(Color{224,54,42,255},0.95f));
        Box(55.25f*CELL,1.65f,15.55f*CELL,0.06f,0.48f,0.06f,Lit(Color{204,185,122,255},0.9f),metal);
        Box(53.0f*CELL,0.08f,15.85f*CELL,12.2f,0.16f,0.34f,Lit(Color{192,165,91,255},0.92f),metal);
    }
    rlSetTexture(0);
}

void Level2DrawModels() {
    if(!gActive || gGenerator.meshCount<=0) return;
    // Отдельный стандартный raylib-проход: генератор использует собственные PBR-текстуры glTF.
    Vector3 position={70.0f-gOrg.x, Level2FloorAt(70.0f,68.0f)+0.04f, 68.0f-gOrg.z};
    DrawModelEx(gGenerator,position,Vector3{0,1,0},90.0f,Vector3{2.15f,2.15f,2.15f},WHITE);
}
