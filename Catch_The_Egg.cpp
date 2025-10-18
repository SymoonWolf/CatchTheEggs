#include <windows.h>
#include <GL/glew.h> 
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <GL/freeglut.h>
#include <GL/glu.h>
#include <GL/gl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <fstream>
#include <algorithm>
#include <iostream>

using namespace std::chrono;
using namespace std;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------- STB_IMAGE (single-header) ----------------
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // place stb_image.h in same folder

// ---------- Constants ----------
const int WINDOW_W = 800;
const int WINDOW_H = 600;
const float GROUND_Y = 80.0f;
float CHICKEN_Y = 480.0f;
const float CHICKEN_X_MIN = 120.0f;
const float CHICKEN_X_MAX = WINDOW_W - 120.0f;
const float BASKET_Y = GROUND_Y + 20.0f;

float basketX = WINDOW_W / 2.0f;
float basketBaseWidth = 120.0f;
float basketHeight = 28.0f;
const int START_TIME_SECONDS = 60;

const float SPAWN_INTERVAL_MIN = 0.5f;
const float SPAWN_INTERVAL_MAX = 1.2f;
const float EGG_MIN_SPEED = 110.0f;
const float EGG_MAX_SPEED = 180.0f;
const float ENLARGE_DURATION = 8.0f;
const float SLOW_DURATION = 8.0f;

const int SCORE_NORMAL = 1;
const int SCORE_BLUE = 5;
const int SCORE_GOLD = 10;
const int SCORE_POOP_PENALTY = -10;
GLuint texPerkEnlarge = 0, texPerkSlow = 0, texPerkTime = 0, texPerkExtra = 0;
int selectedCharacter = 0; // 0=Hen, 1=Duck, 2=Pigeon
GLuint texHen, texDuck, texPigeon;
bool selectingCharacter = true;


const char* HIGHSCORE_FILE = "highscore.txt";

// ---------- Timing & RNG ----------
high_resolution_clock::time_point lastFrameTime;
std::mt19937 rng((unsigned)std::time(nullptr));
float randFloat(float a, float b) { std::uniform_real_distribution<float> dist(a, b); return dist(rng); }
int randInt(int a, int b) { std::uniform_int_distribution<int> dist(a, b); return dist(rng); }

// ---------- Game State ----------
enum GameState { MENU,CHARACTER_SELECT, PLAYING, PAUSED, GAMEOVER, HIGHSCORE };
GameState gameState = MENU;

float spawnTimer = 0.0f;
float nextSpawnIn = 0.0f;
int score = 0;
int highScore = 0;
int hoveredButton = -1;  // -1 = none, 0 = Start, 1 = High Score, 2 = Exit
int timeLeft = START_TIME_SECONDS;
high_resolution_clock::time_point lastSecondTick;

bool slowActive = false;
bool enlargeActive = false;
float slowRemaining = 0.0f;
float enlargeRemaining = 0.0f;

// ---------- Items ----------
enum ItemType { EGG_NORMAL, EGG_BLUE, EGG_GOLD, POOP, PERK_ENLARGE, PERK_SLOW, PERK_TIME };
struct Item {
    ItemType type;
    float x, y;
    float vx;            // horizontal velocity (affected by wind)
    float speed;         // falling speed
    float radius;
    float rotation;
    float rotationSpeed;
};
std::vector<Item> items;

// ---------- Broken egg (ground) ----------
struct BrokenEgg {
    ItemType type;
    float x, y;
    float timer; // remaining time to show broken state
    float rotation;
};
std::vector<BrokenEgg> brokenEggs;
const float BROKEN_DISPLAY_TIME = 0.5f; // seconds

// ---------- Clouds ----------
struct Cloud { float x,y,scale,speed; };
std::vector<Cloud> clouds;

// ---------- Chicken (hen) motion (teleporting) ----------
float chickenPhase = 0.0f;          // used for wing animation only
float chickenXGlobal = WINDOW_W/2.0f;
float henAppearTimer = 0.0f;
float henNextAppearIn = 2.0f;       // randomized interval between teleports

// ---------- Wind System ----------
float wind = 0.0f;                  // current wind strength (positive = right, negative = left)
float windTimer = 0.0f;
float windChangeInterval = 4.0f;    // change wind every N seconds (randomized a bit)
bool windEnabled = true;            // player can toggle wind with 'w'
const float WIND_MAX = 1.8f;        // max wind strength (tweakable)

// ---------- Textures ----------
GLuint texBasket = 0, texBlue = 0, texBlack = 0, texWhite = 0, texGolden = 0, texPerk = 0;

// ---------- File I/O ----------
void loadHighScore() {
    std::ifstream in(HIGHSCORE_FILE);
    if (in.is_open()) { in >> highScore; in.close(); } else highScore = 0;
}
void saveHighScore() {
    if (score > highScore) {
        highScore = score;
        std::ofstream out(HIGHSCORE_FILE);
        if (out.is_open()) { out << highScore; out.close(); }
    }
}

// ---------- Sound helpers ----------
inline void playSoundAsync(const char* filename) {
    PlaySoundA(filename, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

// ---------- Drawing Helpers ----------
void drawText(float x, float y, const std::string &s, void* font = GLUT_BITMAP_HELVETICA_18) {
    glRasterPos2f(x, y);
    for (char c : s) glutBitmapCharacter(font, c);
}
void drawFilledCircle(float cx, float cy, float r, int segments = 24) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; i++) {
        float a = 2.0f * M_PI * i / segments;
        glVertex2f(cx + cosf(a)*r, cy + sinf(a)*r);
    }
    glEnd();
}
void drawEllipse(float cx, float cy, float rx, float ry, int seg = 36) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= seg; ++i) {
        float a = 2.0f * M_PI * i / seg;
        glVertex2f(cx + cosf(a) * rx, cy + sinf(a) * ry);
    }
    glEnd();
}

// ---------- Texture utilities using stb_image ----------
GLuint loadTextureFromFile(const char* path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char *data = stbi_load(path, &width, &height, &channels, 4);
    if (!data) {
        printf("Failed to load texture: %s\n", path);
        return 0;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // upload
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    // filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // clamp to edge
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void initTextures() {
    texHen    = loadTextureFromFile("hen.png");
    texDuck = loadTextureFromFile("duck.png");
    texPigeon = loadTextureFromFile("pigeon.png");
    texBasket = loadTextureFromFile("basket.png");
    texBlue   = loadTextureFromFile("blue.png");
    texBlack  = loadTextureFromFile("black.png");
    texWhite  = loadTextureFromFile("white.png");
    texGolden = loadTextureFromFile("golden.png");
    texPerkEnlarge  = loadTextureFromFile("pink.png");     // enlarge
    texPerkSlow     = loadTextureFromFile("green.png");    // slow
    texPerkTime     = loadTextureFromFile("red.png");      // extra time
    texPerkExtra    = loadTextureFromFile("sky_blue.png"); // optional extra perk
}
    // fallback for perk/unknown
    //texPerk   = texWhite ? texWhite : 0;


// draw textured quad centered at (x,y)
void drawTexturedQuadCentered(GLuint tex, float x, float y, float w, float h, float rotation = 0.0f, float alpha = 1.0f) {
    if (!tex) {
        // fallback: draw a simple ellipse
        glColor4f(1,1,1,alpha);
        drawEllipse(x, y, w*0.5f, h*0.5f, 24);
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glRotatef(rotation, 0, 0, 1);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-w/2, -h/2);
        glTexCoord2f(1, 0); glVertex2f(w/2, -h/2);
        glTexCoord2f(1, 1); glVertex2f(w/2, h/2);
        glTexCoord2f(0, 1); glVertex2f(-w/2, h/2);
    glEnd();
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ---------- Spawning ----------
void spawnItem() {
    Item it;

    float henWidth = 110.0f;  // same as drawHen width
    float henHeight = 80.0f;  // same as drawHen height

    // Spawn from hen’s “rear center”
    it.x = chickenXGlobal;              // aligned with hen’s center
    it.y = CHICKEN_Y - henHeight * 0.25f;  // slightly below hen’s top (rear/back area)

    it.vx = 0.0f; // wind will affect horizontal movement
    it.speed = randFloat(EGG_MIN_SPEED, EGG_MAX_SPEED);
    it.radius = 12.0f;
    it.rotation = randFloat(0.0f, 360.0f);
    it.rotationSpeed = randFloat(-120.0f, 120.0f);

    // Random egg type
    int pick = randInt(1, 100);
    if (pick <= 3) it.type = EGG_GOLD;
    else if (pick <= 15) it.type = EGG_BLUE;
    else if (pick <= 75) it.type = EGG_NORMAL;
    else if (pick <= 85) it.type = POOP;
    else {
        int p = randInt(0, 2);
        it.type = (p==0) ? PERK_ENLARGE : (p==1) ? PERK_SLOW : PERK_TIME;
    }

    items.push_back(it);
}


// ---------- Collision ----------
bool checkCatch(const Item &it) {
    float halfW = (enlargeActive ? basketBaseWidth * 0.9f : basketBaseWidth / 2.0f);
    float dx = fabs(it.x - basketX);
    if (it.y <= BASKET_Y + basketHeight/2.0f + it.radius && dx <= halfW + it.radius) return true;
    return false;
}

void applyItemEffect(const Item &it) {
    switch (it.type) {
        case EGG_NORMAL: score += SCORE_NORMAL; playSoundAsync("catch.wav"); break;
        case EGG_BLUE: score += SCORE_BLUE; playSoundAsync("catch.wav"); break;
        case EGG_GOLD: score += SCORE_GOLD; playSoundAsync("catch.wav"); break;
        case POOP: score += SCORE_POOP_PENALTY; if (score < 0) score = 0; playSoundAsync("poop.wav"); break;
        case PERK_ENLARGE: enlargeActive = true; enlargeRemaining = ENLARGE_DURATION; playSoundAsync("perk.wav"); break;
        case PERK_SLOW: slowActive = true; slowRemaining = SLOW_DURATION; playSoundAsync("perk.wav"); break;
        case PERK_TIME: timeLeft += 10; playSoundAsync("perk.wav"); break;
    }
    // basket tilt remains removed (basket stays upright)
}

// ---------- Helper: reset/start game ----------
void resetGameStart() {
    items.clear();
    brokenEggs.clear();
    score = 0;
    timeLeft = START_TIME_SECONDS;
    lastSecondTick = high_resolution_clock::now();
    spawnTimer = 0.0f;
    nextSpawnIn = randFloat(SPAWN_INTERVAL_MIN, SPAWN_INTERVAL_MAX);
    slowActive = enlargeActive = false;
    gameState = PLAYING;
    lastFrameTime = high_resolution_clock::now();

    // reset hen teleport timers & position
    chickenXGlobal = randFloat(50.0f, WINDOW_W - 50.0f); // anywhere horizontally
    CHICKEN_Y = WINDOW_H - 150.0f; // lower than before, ~150 px from top
    henAppearTimer = 0.0f;
    henNextAppearIn = 1.0f + randFloat(0.0f, 3.0f);
    // reset wind
    wind = 0.0f;
    windTimer = 0.0f;
    windChangeInterval = 3.0f + randFloat(0.0f, 3.0f);
    windEnabled = true;
}

// ---------- Input ----------
void keyboard(unsigned char key, int x, int y) {
    if (key == 27) { saveHighScore(); exit(0); } // ESC
    if (key == 'p' || key == 'P') {
        if (gameState == PLAYING) {
            gameState = PAUSED;
        } else if (gameState == PAUSED) {
            gameState = PLAYING;
            lastFrameTime = high_resolution_clock::now();
        }
    }
    if (key == 'w' || key == 'W') {
        windEnabled = !windEnabled;
        if (!windEnabled) wind = 0.0f;
    }
    if (key == '\r' || key == '\n') {
        if (gameState == MENU || gameState == GAMEOVER) {
            resetGameStart();
        } else if (gameState == HIGHSCORE) gameState = MENU;
    }
    if (key == 'h' || key == 'H') if (gameState == MENU) gameState = HIGHSCORE;
}

void specialKeys(int key, int x, int y) {
    if (gameState == PLAYING) {
        const float moveStep = 12.0f;
        if (key == GLUT_KEY_LEFT) basketX -= moveStep;
        if (key == GLUT_KEY_RIGHT) basketX += moveStep;
        float halfW = enlargeActive ? basketBaseWidth * 0.9f : basketBaseWidth / 2.0f;
        basketX = std::clamp(basketX, halfW, (float)WINDOW_W - halfW);
    }
}

// ---------- Update ----------
void updateGame(float dt) {
    if (gameState != PLAYING) return;

    // chicken bob (subtle)
    chickenPhase += 2.0f * dt;

    // hen teleport timing:
    henAppearTimer += dt;
    if (henAppearTimer >= henNextAppearIn) {
        chickenXGlobal = randFloat(CHICKEN_X_MIN, CHICKEN_X_MAX);
        henAppearTimer = 0.0f;
        henNextAppearIn = 1.5f + randFloat(0.0f, 2.5f);
    }

    // wind update (change occasionally)
    if (windEnabled) {
        windTimer += dt;
        if (windTimer >= windChangeInterval) {
            // new wind: smooth random in [-WIND_MAX, WIND_MAX]
            wind = randFloat(-WIND_MAX, WIND_MAX);
            // slightly randomize next change interval
            windTimer = 0.0f;
            windChangeInterval = 2.0f + randFloat(0.0f, 4.0f);
        }
    } else {
        wind = 0.0f;
    }

    // spawn
    spawnTimer += dt;
    if (spawnTimer >= nextSpawnIn) { spawnItem(); spawnTimer = 0; nextSpawnIn = randFloat(SPAWN_INTERVAL_MIN, SPAWN_INTERVAL_MAX); }
    float speedMod = slowActive ? 0.45f : 1.0f;

    // items movement + catch detection + ground handling
    vector<Item> remaining;
    // Grass break threshold
    const float GRASS_BREAK_THRESHOLD = GROUND_Y - 8.0f; // eggs break when y <= this
    const float BROKEN_EGG_Y = GROUND_Y - 12.0f;        // broken egg draw y

    for (auto &it : items) {
        // Apply falling
        it.y -= it.speed * speedMod * dt;
        it.rotation += it.rotationSpeed * dt;

        float typeFactor = 1.0f;
        if (it.type == EGG_GOLD) typeFactor = 0.5f;
        else if (it.type == EGG_BLUE) typeFactor = 0.85f;
        // gradually change vx toward wind (simulate air drag)
        float vxTarget = wind * 40.0f * typeFactor; // wind scaled to px/sec
        // simple approach: lerp current vx to target
        it.vx += (vxTarget - it.vx) * std::clamp(dt * 1.8f, 0.0f, 1.0f);
        it.x += it.vx * dt;

        // keep items within screen horizontally
        if (it.x < 10.0f) it.x = 10.0f;
        if (it.x > WINDOW_W - 10.0f) it.x = WINDOW_W - 10.0f;

        if (checkCatch(it)) {
            applyItemEffect(it);
            // caught -> drop it (don't push to remaining)
        }
        else if (it.y <= GRASS_BREAK_THRESHOLD) {
            // Reached grass area -> break or drop
            if (it.type == EGG_NORMAL || it.type == EGG_BLUE || it.type == EGG_GOLD) {
                BrokenEgg be;
                be.type = it.type;
                be.x = it.x;
                be.y = BROKEN_EGG_Y;
                be.timer = BROKEN_DISPLAY_TIME;
                be.rotation = it.rotation;
                brokenEggs.push_back(be);
                playSoundAsync("break.wav");
            } else {
                // poop/perks
                if (it.type == POOP) playSoundAsync("poop.wav");
            }
            // item removed
        }
        else {
            remaining.push_back(it);
        }
    }
    items.swap(remaining);

    // broken eggs timers
    vector<BrokenEgg> remBroken;
    for (auto &be : brokenEggs) {
        be.timer -= dt;
        if (be.timer > 0) remBroken.push_back(be);
    }
    brokenEggs.swap(remBroken);

    // perks timers
    if (slowActive) { slowRemaining -= dt; if (slowRemaining <= 0) slowActive = false; }
    if (enlargeActive) { enlargeRemaining -= dt; if (enlargeRemaining <= 0) enlargeActive = false; }

    // clouds
    for (auto &c : clouds) {
        c.x -= c.speed * dt;
        if (c.x + 200.0f * c.scale < 0) c.x = WINDOW_W + randFloat(20.0f, 260.0f);
    }

    // seconds tick (timer)
    auto now = high_resolution_clock::now();
    duration<double> sinceLast = now - lastSecondTick;
    if (sinceLast.count() >= 1.0) {
        int passed = (int)sinceLast.count();
        timeLeft -= passed;
        lastSecondTick += seconds(passed);
        if (timeLeft <= 0) { timeLeft = 0; saveHighScore(); gameState = GAMEOVER; }
    }
}

// ---------- Draw: UI & Scene ----------
void drawSkyGradient() {
    glBegin(GL_QUADS);
    glColor3f(0.74f, 0.92f, 0.99f); glVertex2f(0, 0);
    glVertex2f(WINDOW_W, 0);
    glColor3f(0.45f, 0.82f, 0.99f); glVertex2f(WINDOW_W, WINDOW_H);
    glVertex2f(0, WINDOW_H);
    glEnd();
}
void drawCloud(float cx, float cy, float scale) {
    glColor3f(1.0f, 1.0f, 1.0f);
    drawFilledCircle(cx - 30*scale, cy, 20*scale, 24);
    drawFilledCircle(cx, cy + 6*scale, 28*scale, 28);
    drawFilledCircle(cx + 34*scale, cy, 22*scale, 24);
    glColor4f(0.85f, 0.85f, 0.85f, 0.8f);
    drawFilledCircle(cx, cy - 6*scale, 28*scale, 20);
}
void drawGround() {
    // brown dirt base
    glColor3f(0.35f, 0.2f, 0.05f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(WINDOW_W, 0);
    glVertex2f(WINDOW_W, GROUND_Y - 18.0f); glVertex2f(0, GROUND_Y - 18.0f);
    glEnd();
    // grass top
    glColor3f(0.14f, 0.65f, 0.12f);
    glBegin(GL_POLYGON);
    float step = 20.0f;
    glVertex2f(0, GROUND_Y - 18.0f);
    for (float x = 0; x <= WINDOW_W; x += step) {
        float h = 10.0f + ( (int)x % 40 == 0 ? 8.0f : 0.0f );
        glVertex2f(x + step*0.5f, GROUND_Y - 18.0f + h);
        glVertex2f(x + step, GROUND_Y - 18.0f);
    }
    glVertex2f(WINDOW_W, GROUND_Y - 18.0f);
    glEnd();
}

// Draw textured hen (replaces drawChicken)
void drawHen(float x, float y) {
    float bob = sinf(chickenPhase * 2.0f) * 4.0f; // subtle bob
    float w = 110.0f;
    float h = 80.0f;

    // Choose texture based on player selection
    GLuint tex;
    if (selectedCharacter == 0) tex = texHen;       // Hen
    else if (selectedCharacter == 1) tex = texDuck; // Duck
    else tex = texPigeon;                           // Pigeon

    drawTexturedQuadCentered(tex, x, y + bob, w, h, 0.0f, 1.0f);
}


// Draw textured basket (replaces drawBasket)
void drawBasket() {
    float halfW = (enlargeActive ? basketBaseWidth * 0.9f : basketBaseWidth / 2.0f);
    float w = halfW * 2.0f;
    float h = 60.0f;
    // shadow
    glColor4f(0,0,0,0.18f);
    drawFilledCircle(basketX, BASKET_Y - 6.0f, w * 0.36f, 28);
    drawTexturedQuadCentered(texBasket, basketX, BASKET_Y + 6.0f, w, h, 0.0f, 1.0f);
}

// Draw eggs using textures (replaces drawItem)
void drawItemTextured(const Item &it) {
    GLuint tex = 0;
    float w = it.radius * 2.2f;
    float h = it.radius * 2.8f;
    switch (it.type) {
        case EGG_NORMAL: tex = texWhite; break;
        case EGG_BLUE:   tex = texBlue; break;
        case EGG_GOLD:   tex = texGolden; break;
        case POOP:       tex = texBlack; break;
        case PERK_ENLARGE: tex = texPerkEnlarge; break;
        case PERK_SLOW:    tex = texPerkSlow; break;
        case PERK_TIME:    tex = texPerkTime; break;
        // if you want the fourth perk type:
        // case PERK_EXTRA: tex = texPerkExtra; break;
    }
    drawTexturedQuadCentered(tex, it.x, it.y, w, h, it.rotation, 1.0f);
}


// Draw broken egg (keep simple textured / painted splat)
void drawBrokenEgg(const BrokenEgg &be) {
    // small colored smear to match egg color
    if (be.type == EGG_BLUE) glColor3f(0.6f, 0.82f, 1.0f);
    else if (be.type == EGG_GOLD) glColor3f(1.0f, 0.86f, 0.25f);
    else glColor3f(1.0f, 0.98f, 0.92f);

    glPushMatrix();
    glTranslatef(be.x, be.y, 0);
    glRotatef(be.rotation * 0.25f, 0, 0, 1);
    drawEllipse(0, 0, 18.0f, 10.0f, 20);
    glColor3f(0.12f, 0.08f, 0.05f);
    glBegin(GL_TRIANGLES);
        glVertex2f(-8, -2); glVertex2f(-4, -6); glVertex2f(-3, -2);
        glVertex2f(8, -3); glVertex2f(10, -7); glVertex2f(6, -5);
    glEnd();
    glPopMatrix();
}

// ---------- Menu (animated, modern look; mouse-enabled) ----------
void drawMenuOverlay() {
    static float pulse = 0.0f;
    pulse += 0.03f;

    // blurred dark panel
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.06f, 0.06f, 0.06f, 0.65f);
    float mw = 540, mh = 320;
    float mx = WINDOW_W/2 - mw/2, my = WINDOW_H/2 - mh/2;
    glBegin(GL_QUADS);
        glVertex2f(mx, my);
        glVertex2f(mx + mw, my);
        glVertex2f(mx + mw, my + mh);
        glVertex2f(mx, my + mh);
    glEnd();

    // subtle gradient header
    glBegin(GL_QUADS);
        glColor4f(0.98f, 0.78f, 0.18f, 1.0f);
        glVertex2f(mx, my + mh - 72);
        glVertex2f(mx + mw, my + mh - 72);
        glColor4f(0.82f, 0.56f, 0.08f, 1.0f);
        glVertex2f(mx + mw, my + mh - 40);
        glVertex2f(mx, my + mh - 40);
    glEnd();

    // title
    const char* title = "CATCH THE EGG";
    int titleWidth = glutBitmapLength(GLUT_BITMAP_TIMES_ROMAN_24, (const unsigned char*)title);
    float titleX = WINDOW_W/2 - titleWidth/2;
    float titleY = my + mh - 66;
    // shadowed title
    for (int dx=-1; dx<=1; ++dx)
        for (int dy=-1; dy<=1; ++dy)
            drawText(titleX + dx, titleY + dy, title, GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(0.06f,0.06f,0.06f);
    drawText(titleX, titleY, title, GLUT_BITMAP_TIMES_ROMAN_24);

    // buttons
    auto drawButton = [&](float cy, const char* label, int index) {
        float bw = 280, bh = 48;
        bool hovered = (hoveredButton == index);
        float bx = WINDOW_W/2 - bw/2;
        float by = cy - bh/2;
        if (hovered) {
            glColor4f(0.98f, 0.92f, 0.75f, 1.0f);
        } else {
            glColor4f(0.97f, 0.97f, 0.99f, 1.0f);
        }
        // rounded-ish rectangle approximation
        glBegin(GL_QUADS);
          glVertex2f(bx, by);
          glVertex2f(bx + bw, by);
          glVertex2f(bx + bw, by + bh);
          glVertex2f(bx, by + bh);
        glEnd();
        glColor3f(0.45f, 0.45f, 0.45f);
        glBegin(GL_LINE_LOOP);
          glVertex2f(bx, by);
          glVertex2f(bx + bw, by);
          glVertex2f(bx + bw, by + bh);
          glVertex2f(bx, by + bh);
        glEnd();
        glColor3f(0.08f, 0.08f, 0.08f);
        int labelWidth = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)label);
        drawText(WINDOW_W/2 - labelWidth/2, cy - 6, label, GLUT_BITMAP_HELVETICA_18);
    };

    drawButton(WINDOW_H/2 + 44, "START GAME", 0);
    drawButton(WINDOW_H/2, "HIGH SCORE", 1);
    drawButton(WINDOW_H/2 - 44, "EXIT", 2);

    // footer
    glColor3f(0.85f, 0.85f, 0.85f);
    drawText(WINDOW_W/2 - 110, my + 16, "Use ENTER or Click to Start, ESC to exit", GLUT_BITMAP_HELVETICA_12);

    glDisable(GL_BLEND);
}

// // ---------- Wind indicator ----------
// void drawWindIndicator() {
//     // small panel in top-right
//     float px = WINDOW_W - 160, py = WINDOW_H - 80;
//     // panel
//     glEnable(GL_BLEND);
//     glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//     glColor4f(0.05f, 0.05f, 0.06f, 0.5f);
//     glBegin(GL_QUADS);
//         glVertex2f(px, py);
//         glVertex2f(px + 150, py);
//         glVertex2f(px + 150, py + 68);
//         glVertex2f(px, py + 68);
//     glEnd();

//     // title
//     glColor3f(0.9f, 0.9f, 0.9f);
//     drawText(px + 10, py + 50, "WIND");

//     // arrow center
//     float cx = px + 120, cy = py + 34;
//     // arrow direction: right when wind>0
//     float maxLen = 36.0f;
//     float len = fabs(wind) / WIND_MAX * maxLen;
//     if (len < 2.0f) len = 2.0f;

//     // arrow shaft
//     glColor3f(1.0f, 1.0f, 1.0f);
//     glLineWidth(3.0f);
//     glBegin(GL_LINES);
//         if (wind >= 0) glVertex2f(cx - len, cy), glVertex2f(cx + len, cy);
//         else glVertex2f(cx + len, cy), glVertex2f(cx - len, cy);
//     glEnd();
//     glLineWidth(1.0f);

//     // arrow head
//     glBegin(GL_TRIANGLES);
//         if (wind >= 0) {
//             glVertex2f(cx + len + 6, cy);
//             glVertex2f(cx + len - 2, cy + 6);
//             glVertex2f(cx + len - 2, cy - 6);
//         } else {
//             glVertex2f(cx - len - 6, cy);
//             glVertex2f(cx - len + 2, cy + 6);
//             glVertex2f(cx - len + 2, cy - 6);
//         }
//     glEnd();

//     // numeric strength
//     char buf[64];
//     sprintf(buf, "%.2f", wind);
//     drawText(px + 10, py + 20, buf);

//     glDisable(GL_BLEND);
// }

// ---------- Display ----------
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawSkyGradient();
    for (auto &c : clouds) drawCloud(c.x, c.y, c.scale);
    drawGround();

    drawHen(chickenXGlobal, CHICKEN_Y);

    for (auto &it : items) drawItemTextured(it);
    drawBasket();

    for (auto &be : brokenEggs) drawBrokenEgg(be);

    // UI: Score & Time
    char buf[128];
    sprintf(buf,"Score:%d",score);
    glColor3f(0,0,0); drawText(11, WINDOW_H - 23, buf);
    glColor3f(1,1,1); drawText(10, WINDOW_H - 22, buf);
    sprintf(buf,"Time:%d",timeLeft);
    glColor3f(0,0,0); drawText(WINDOW_W - 111, WINDOW_H - 23, buf);
    glColor3f(1,1,1); drawText(WINDOW_W - 110, WINDOW_H - 22, buf);

    // Pause button (moved lower & smaller)
    float pbX = WINDOW_W - 40.0f;   // left offset
    float pbY = WINDOW_H - 70.0f;   // moved lower
    float pbW = 30.0f, pbH = 30.0f; // smaller

    if (gameState == PLAYING) glColor3f(0.97f, 0.97f, 0.97f);
    else if (gameState == PAUSED) glColor3f(0.85f, 0.95f, 0.85f);
    else glColor3f(0.9f, 0.9f, 0.9f);

    glBegin(GL_QUADS);
        glVertex2f(pbX, pbY);
        glVertex2f(pbX + pbW, pbY);
        glVertex2f(pbX + pbW, pbY + pbH);
        glVertex2f(pbX, pbY + pbH);
    glEnd();
    glColor3f(0.45f, 0.45f, 0.45f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(pbX, pbY);
        glVertex2f(pbX + pbW, pbY);
        glVertex2f(pbX + pbW, pbY + pbH);
        glVertex2f(pbX, pbY + pbH);
    glEnd();

    // draw pause/play icon inside button
    if (gameState == PLAYING) {
        glColor3f(0.08f, 0.08f, 0.08f);
        float barW = 4.0f, gap = 6.0f;
        glBegin(GL_QUADS);
            glVertex2f(pbX + 8, pbY + 7); glVertex2f(pbX + 8 + barW, pbY + 7);
            glVertex2f(pbX + 8 + barW, pbY + pbH - 7); glVertex2f(pbX + 8, pbY + pbH - 7);
        glEnd();
        glBegin(GL_QUADS);
            glVertex2f(pbX + 8 + barW + gap, pbY + 7); glVertex2f(pbX + 8 + barW + gap + barW, pbY + 7);
            glVertex2f(pbX + 8 + barW + gap + barW, pbY + pbH - 7); glVertex2f(pbX + 8 + barW + gap, pbY + pbH - 7);
        glEnd();
    } else if (gameState == PAUSED) {
        glColor3f(0.08f, 0.08f, 0.08f);
        glBegin(GL_TRIANGLES);
            glVertex2f(pbX + 10, pbY + 7);
            glVertex2f(pbX + 10, pbY + pbH - 7);
            glVertex2f(pbX + pbW - 8, pbY + pbH/2.0f);
        glEnd();
    }

    // overlays (menu/pause/gameover/highscore)
    if (gameState != PLAYING) {

        // ✅ NEW: Character selection screen
        if (gameState == CHARACTER_SELECT) {
            glClear(GL_COLOR_BUFFER_BIT);
            glLoadIdentity();

            glColor3f(1, 1, 1);
            drawText(WINDOW_W/2 - 120, WINDOW_H - 100, "Select Your Character:");

            float startX = WINDOW_W/2 - 280;
            float y = WINDOW_H/2 - 70;
            float size = 150;

            glEnable(GL_TEXTURE_2D);

            // Hen
            glBindTexture(GL_TEXTURE_2D, texHen);
            glBegin(GL_QUADS);
                glTexCoord2f(0,0); glVertex2f(startX, y);
                glTexCoord2f(1,0); glVertex2f(startX + size, y);
                glTexCoord2f(1,1); glVertex2f(startX + size, y + size);
                glTexCoord2f(0,1); glVertex2f(startX, y + size);
            glEnd();

            // Duck
            glBindTexture(GL_TEXTURE_2D, texDuck);
            glBegin(GL_QUADS);
                glTexCoord2f(0,0); glVertex2f(startX + 200, y);
                glTexCoord2f(1,0); glVertex2f(startX + 200 + size, y);
                glTexCoord2f(1,1); glVertex2f(startX + 200 + size, y + size);
                glTexCoord2f(0,1); glVertex2f(startX + 200, y + size);
            glEnd();

            // Pigeon
            glBindTexture(GL_TEXTURE_2D, texPigeon);
            glBegin(GL_QUADS);
                glTexCoord2f(0,0); glVertex2f(startX + 400, y);
                glTexCoord2f(1,0); glVertex2f(startX + 400 + size, y);
                glTexCoord2f(1,1); glVertex2f(startX + 400 + size, y + size);
                glTexCoord2f(0,1); glVertex2f(startX + 400, y + size);
            glEnd();

            glDisable(GL_TEXTURE_2D);

            drawText(startX + 40, y - 30, "Hen");
            drawText(startX + 240, y - 30, "Duck");
            drawText(startX + 440, y - 30, "Pigeon");

            drawText(WINDOW_W/2 - 100, y - 80, "Click to choose");
            glutSwapBuffers();
            return;
        }

        // ✅ Existing overlay handling remains unchanged
        if (gameState == MENU) drawMenuOverlay();
        else {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0,0,0,0.45f); glBegin(GL_QUADS);
              glVertex2f(0,0); glVertex2f(WINDOW_W,0); glVertex2f(WINDOW_W,WINDOW_H); glVertex2f(0,WINDOW_H);
            glEnd();
            glDisable(GL_BLEND);
            glColor3f(1,1,1);
            if (gameState == PAUSED) drawText(WINDOW_W/2-40, WINDOW_H/2, "PAUSED");
            else if (gameState == GAMEOVER) {
                drawText(WINDOW_W/2-60, WINDOW_H/2+30, "GAME OVER");
                sprintf(buf,"Your Score: %d",score); drawText(WINDOW_W/2-60, WINDOW_H/2, buf);
                sprintf(buf,"High Score: %d",highScore); drawText(WINDOW_W/2-60, WINDOW_H/2-20, buf);
                drawText(WINDOW_W/2-90, WINDOW_H/2-40, "Press Enter to restart");
            } else if (gameState == HIGHSCORE) {
                drawText(WINDOW_W/2-70, WINDOW_H/2+30, "HIGH SCORE");
                sprintf(buf,"High Score: %d",highScore); drawText(WINDOW_W/2-70, WINDOW_H/2, buf);
                drawText(WINDOW_W/2-120, WINDOW_H/2-30, "Press Enter to return to Menu");
            }
        }
    }

    glutSwapBuffers();
}


// ---------- Mouse handling (menu & pause button) ----------
void handleMenuMouse(int button, int state, int x, int y) {
    float glX = (float)x;
    float glY = WINDOW_H - (float)y; // invert Y

    // --- MAIN MENU HANDLING ---
    if (gameState == MENU) {
        struct Btn { float cy; } btns[3] = {
            { WINDOW_H / 2 + 24 },
            { WINDOW_H / 2 - 24 },
            { WINDOW_H / 2 - 72 }
        };

        float bw = 260, bh = 42;
        hoveredButton = -1;

        for (int i = 0; i < 3; i++) {
            float bx1 = WINDOW_W / 2 - bw / 2, bx2 = WINDOW_W / 2 + bw / 2;
            float by1 = btns[i].cy - bh / 2, by2 = btns[i].cy + bh / 2;

            if (glX >= bx1 && glX <= bx2 && glY >= by1 && glY <= by2) {
                hoveredButton = i;

                if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
                    if (i == 0) { 
                        // Instead of starting game immediately, go to character select
                        gameState = CHARACTER_SELECT;
                    }
                    else if (i == 1) { gameState = HIGHSCORE; }
                    else if (i == 2) { saveHighScore(); exit(0); }
                }
            }
        }
        glutPostRedisplay();
        return;
    }

    // --- CHARACTER SELECTION HANDLING ---
    if (gameState == CHARACTER_SELECT && button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        float startX = WINDOW_W/2 - 200;
        float yTop = WINDOW_H/2 - 150;
        float size = 150;

        if (glX >= startX && glX <= startX + size && glY >= yTop && glY <= yTop + size) {
            selectedCharacter = 0; // Hen
            resetGameStart();
            gameState = PLAYING;
        }
        else if (glX >= startX + 200 && glX <= startX + 200 + size && glY >= yTop && glY <= yTop + size) {
            selectedCharacter = 1; // Duck
            resetGameStart();
            gameState = PLAYING;
        }
        else if (glX >= startX + 400 && glX <= startX + 400 + size && glY >= yTop && glY <= yTop + size) {
            selectedCharacter = 2; // Pigeon
            resetGameStart();
            gameState = PLAYING;
        }
        glutPostRedisplay();
        return;
    }

    // --- PAUSE BUTTON HANDLING ---
    if (gameState == PLAYING || gameState == PAUSED) {
        float pbX = WINDOW_W - 40.0f;
        float pbY = WINDOW_H - 70.0f;
        float pbW = 30.0f, pbH = 30.0f;

        if (glX >= pbX && glX <= pbX + pbW && glY >= pbY && glY <= pbY + pbH) {
            if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
                if (gameState == PLAYING) {
                    gameState = PAUSED;
                } else if (gameState == PAUSED) {
                    gameState = PLAYING;
                    lastFrameTime = high_resolution_clock::now();
                }
            }
            glutPostRedisplay();
            return;
        }
    }

    // else ignore clicks in other states (ENTER restarts)
}

void handleMenuMotion(int x, int y) {
    float glX = (float)x;
    float glY = WINDOW_H - (float)y; // invert Y

    hoveredButton = -1; // reset hover

    if (gameState == MENU) {
        // Menu buttons
        struct Btn { float cy; } btns[3] = {
            { WINDOW_H / 2 + 24 },
            { WINDOW_H / 2 - 24 },
            { WINDOW_H / 2 - 72 }
        };
        float bw = 260, bh = 42;

        for (int i = 0; i < 3; i++) {
            float bx1 = WINDOW_W / 2 - bw / 2, bx2 = WINDOW_W / 2 + bw / 2;
            float by1 = btns[i].cy - bh / 2, by2 = btns[i].cy + bh / 2;

            if (glX >= bx1 && glX <= bx2 && glY >= by1 && glY <= by2) {
                hoveredButton = i;
                break;
            }
        }
    } 
    else if (gameState == PLAYING || gameState == PAUSED) {
        // Pause button hover
        float pbX = WINDOW_W - 40.0f;
        float pbY = WINDOW_H - 70.0f;
        float pbW = 30.0f, pbH = 30.0f;

        if (glX >= pbX && glX <= pbX + pbW && glY >= pbY && glY <= pbY + pbH) {
            hoveredButton = 0; // 0 = pause button hover (optional)
        }
    }

    glutPostRedisplay();
}
// ---------- Mouse motion: basket + menu/pause ----------
void handleMouseMotion(int x, int y) {
    float glX = (float)x;
    float glY = WINDOW_H - (float)y; // invert Y

    hoveredButton = -1; // reset hover

    // --- Basket movement ---
    if (gameState == PLAYING) {
        basketX = glX;
        float halfW = enlargeActive ? basketBaseWidth * 0.9f : basketBaseWidth / 2.0f;
        basketX = std::clamp(basketX, halfW, (float)WINDOW_W - halfW);
    }

    // --- Menu buttons hover ---
    if (gameState == MENU) {
        struct Btn { float cy; } btns[3] = {
            { WINDOW_H / 2 + 24 },   // Start
            { WINDOW_H / 2 - 24 },   // High Score
            { WINDOW_H / 2 - 72 }    // Exit
        };
        float bw = 260, bh = 42;

        for (int i = 0; i < 3; i++) {
            float bx1 = WINDOW_W / 2 - bw / 2, bx2 = WINDOW_W / 2 + bw / 2;
            float by1 = btns[i].cy - bh / 2, by2 = btns[i].cy + bh / 2;
            if (glX >= bx1 && glX <= bx2 && glY >= by1 && glY <= by2) {
                hoveredButton = i;
                break;
            }
        }
    }
    // --- Pause button hover ---
    if (gameState == PLAYING || gameState == PAUSED) {
        float pbX = WINDOW_W - 40.0f, pbY = WINDOW_H - 70.0f, pbW = 30.0f, pbH = 30.0f;
        if (glX >= pbX && glX <= pbX + pbW && glY >= pbY && glY <= pbY + pbH) {
            hoveredButton = 0; // optional: highlight pause button
        }
    }

    glutPostRedisplay();
}

// ---------- Reshape ----------
void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, WINDOW_W, 0, WINDOW_H);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ---------- Idle / Timer ----------
void idle() {
    auto now = high_resolution_clock::now();
    double dt = duration<double>(now - lastFrameTime).count();
    if (dt > 0.0) {
        if (gameState == PLAYING) updateGame((float)dt);
        lastFrameTime = now;
        glutPostRedisplay();
    }
}

// ---------- Initialization ----------
void initGL() {
    glClearColor(0.6f, 0.85f, 1.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    clouds.clear();
    for (int i=0;i<6;i++) {
        Cloud c;
        c.scale = randFloat(0.7f, 1.2f);
        c.x = randFloat(-100.0f + i * 150.0f, WINDOW_W + 200.0f);
        c.y = randFloat(WINDOW_H*0.55f, WINDOW_H*0.85f);
        c.speed = randFloat(8.0f, 30.0f);
        clouds.push_back(c);
    }
}

// ---------- Main ----------
int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));
    loadHighScore();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WINDOW_W, WINDOW_H);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("Catch the Egg");
    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        std::cerr << "Error initializing GLEW: " << glewGetErrorString(err) << std::endl;
        return -1;
    }

    initGL();
    initTextures();
    lastFrameTime = high_resolution_clock::now();
    lastSecondTick = lastFrameTime;
    nextSpawnIn = randFloat(SPAWN_INTERVAL_MIN, SPAWN_INTERVAL_MAX);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutIdleFunc(idle);
    glutPassiveMotionFunc(handleMouseMotion);
    glutMotionFunc(handleMouseMotion);
    glutMouseFunc(handleMenuMouse);
    glutPassiveMotionFunc(handleMenuMotion);

    glutMainLoop();
    return 0;
}
