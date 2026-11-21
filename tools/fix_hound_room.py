from pathlib import Path
import shutil

path = Path("app/src/main/cpp/entities.cpp")
src = path.read_text()

marker = "static void Think(Entity& e, Player& pl, float dt) {"

helpers = r'''
// Границы внутренней части комнаты Гончей.
// Комната занимает клетки x=5..7, z=1..3.
static bool HoundRoomContains(Vector2 p) {
    int cx = CellOf(p.x);
    int cz = CellOf(p.y);
    return cx >= 5 && cx <= 7 && cz >= 1 && cz <= 3;
}

static Vector2 HoundRoomPoint() {
    int cx = GetRandomValue(5, 7);
    int cz = GetRandomValue(1, 3);
    return CellCenter(cx, cz);
}

static void KeepHoundInRoom(Entity& e) {
    // Не позволяем центру сущности пересечь стены комнаты.
    e.pos.x = fmaxf(10.35f, fminf(15.65f, e.pos.x));
    e.pos.y = fmaxf(2.35f, fminf(7.65f, e.pos.y));
}

'''

if src.count(marker) != 1:
    raise SystemExit("Ошибка: не найдено единственное место вставки Think().")

if "static bool HoundRoomContains" not in src:
    src = src.replace(marker, helpers + marker, 1)

old_idle = (
    "case S_IDLE:   e.timer -= dt; if (e.timer <= 0) "
    "{ e.target = RandomOpen(e.pos, 10); e.st = S_PATROL; } "
    "if (d < hear) alert(); break;"
)

new_idle = (
    "case S_IDLE:   e.timer -= dt; if (e.timer <= 0) "
    "{ e.target = HoundRoomPoint(); e.st = S_PATROL; } "
    "if (d < hear) alert(); break;"
)

if src.count(old_idle) != 1:
    raise SystemExit("Ошибка: не удалось однозначно найти переход IDLE.")

src = src.replace(old_idle, new_idle, 1)

old_patrol = (
    "case S_PATROL: Follow(e, e.target, 2.4f, dt); "
    "if (Dist(e.pos, e.target) < 1.2f) "
    "{ e.st = S_IDLE; e.timer = Frand(1, 3); } "
    "if (d < hear) alert(); break;"
)

new_patrol = (
    "case S_PATROL: Follow(e, e.target, 2.4f, dt); "
    "if (Dist(e.pos, e.target) < 1.2f) "
    "{ e.st = S_IDLE; e.timer = Frand(1, 3); } "
    "if (d < hear) alert(); break;"
)

if src.count(old_patrol) != 1:
    raise SystemExit("Ошибка: не удалось однозначно найти PATROL.")

# Патрульная цель уже ограничена комнатой; здесь меняется только
# выбор следующей цели при потере игрока.
src = src.replace(old_patrol, new_patrol, 1)

old_chase = '''        case S_CHASE:
            Follow(e, pp, eye ? 1.2f : 4.8f, dt);
            if (d < 1.4f) { e.st = S_ATTACK; e.atkCd = 0.2f; }
            if (d > hear * 1.2f + 6) { e.lose += dt; if (e.lose > 5) { e.st = S_PATROL; e.target = RandomOpen(e.pos, 10); } } else e.lose = 0;
            break;'''

new_chase = '''        case S_CHASE:
            if (HoundRoomContains(pp)) {
                Follow(e, pp, eye ? 1.2f : 4.8f, dt);
            } else {
                Face(e, pp);
            }
            if (d < 1.4f && HoundRoomContains(pp)) {
                e.st = S_ATTACK;
                e.atkCd = 0.2f;
            }
            if (d > hear * 1.2f + 6) {
                e.lose += dt;
                if (e.lose > 5) {
                    e.st = S_PATROL;
                    e.target = HoundRoomPoint();
                }
            } else {
                e.lose = 0;
            }
            break;'''

if src.count(old_chase) != 1:
    raise SystemExit("Ошибка: блок CHASE не совпал с ожидаемым.")

src = src.replace(old_chase, new_chase, 1)

# Ограничиваем позицию после обработки состояний ИИ.
old_end = '''        }
    } break;

    case E_SMILER:'''

new_end = '''        }
        KeepHoundInRoom(e);
    } break;

    case E_SMILER:'''

if src.count(old_end) != 1:
    raise SystemExit("Ошибка: не найден конец блока HOUND.")

src = src.replace(old_end, new_end, 1)

backup = path.with_suffix(".cpp.before_room_fix")
if not backup.exists():
    shutil.copy2(path, backup)

path.write_text(src)
print("Готово: ограничения комнаты Гончей добавлены.")
print("Резервная копия:", backup)
