// cloud_formation.cpp — SDL2 + OpenGL ES 1.1 "soft blob" cumulus formation
// Minimal deps; works like your violinist.cpp scaffold (fixed-function ES 1.x)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <ctime>

#include "SDL2/SDL.h"
#if defined(__ANDROID__) || defined(__IPHONEOS__)
  #include "SDL_opengles.h"        // ES 1.x fixed-function
#else
  #include <GLES/gl.h>             // Desktop packages often provide this name
  #include <GLES/glext.h>
#endif

// ---------- tiny helpers ----------
static inline float frand() { return rand() / (float)RAND_MAX; }
static inline float clampf(float x, float a, float b){ return std::max(a, std::min(b, x)); }

// Solid color (RGBA)
static inline void setColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a=1.0f) { glColor4f(r,g,b,a); }

// Vertex-colored rectangle (for gradients)
static void fillRectGradient(GLfloat x, GLfloat y, GLfloat w, GLfloat h,
                             const GLfloat c00[4], const GLfloat c10[4],
                             const GLfloat c11[4], const GLfloat c01[4]) {
    const GLfloat verts[] = { x, y,  x+w, y,  x+w, y+h,  x, y+h };
    const GLfloat cols[]  = {
        c00[0], c00[1], c00[2], c00[3],
        c10[0], c10[1], c10[2], c10[3],
        c11[0], c11[1], c11[2], c11[3],
        c01[0], c01[1], c01[2], c01[3],
    };
    const GLushort idx[] = {0,1,2, 0,2,3};
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glColorPointer (4, GL_FLOAT, 0, cols);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Solid rectangle
static void fillRect(GLfloat x, GLfloat y, GLfloat w, GLfloat h, const GLfloat c[4]) {
    const GLfloat verts[] = { x, y,  x+w, y,  x+w, y+h,  x, y+h };
    const GLushort idx[] = {0,1,2, 0,2,3};
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    setColor(c[0],c[1],c[2],c[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Soft “blob” disc: layered rings with fading alpha (cheap radial falloff)
static void drawSoftBlob(GLfloat cx, GLfloat cy, GLfloat R,
                         const GLfloat rgb[3], float alphaPeak=0.18f, int rings=8) {
    // Draw smaller to larger discs, each with lower alpha — gives smooth edges.
    for (int i=0; i<rings; ++i) {
        float t = (i+1)/(float)rings;                 // 0..1
        float r = t*R;
        float a = alphaPeak * std::pow(1.0f - t, 1.6f);
        // Triangle fan
        const int slices = 32;
        std::vector<GLfloat> v; v.reserve((size_t)2*(slices+2));
        v.push_back(cx); v.push_back(cy);
        for (int s=0; s<=slices; ++s) {
            float ang = (float)s / slices * 2.0f * (float)M_PI;
            v.push_back(cx + r*std::cos(ang));
            v.push_back(cy + r*std::sin(ang));
        }
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v.data());
        glColor4f(rgb[0], rgb[1], rgb[2], a);
        glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)(v.size()/2));
        glDisableClientState(GL_VERTEX_ARRAY);
    }
}

// ---------- simple “atmospheric” model ----------
struct Puff {
    float x, y;       // position
    float r;          // radius
    float vx, vy;     // velocity (advection/updraft)
    float growth;     // dr/dt
    float wobble;     // small horizontal meander
    float life, maxLife; // seconds
    float whiten;     // 0..1 whiteness (matures as it rises)
};

struct Emitter {
    float x0, x1;     // horizontal source span (near ground)
    float y;          // emission height
    float rate;       // puffs/sec
};

static void spawnPuff(std::vector<Puff>& P, const Emitter& E, int winW, int winH) {
    Puff p{};
    p.x = E.x0 + frand()*(E.x1 - E.x0);
    p.y = E.y + frand()*10.f;
    p.r = 12.f + frand()*10.f;
    p.vx = (frand()-0.5f)*8.f;     // gentle breeze
    p.vy = 12.f + frand()*10.f;    // updraft
    p.growth = 3.f + frand()*6.f;  // grows as condenses
    p.wobble = (frand()*2.f - 1.f) * 0.8f;
    p.life = 0.f;
    p.maxLife = 18.f + frand()*8.f;
    p.whiten = 0.2f;
    P.push_back(p);
}

static void updatePuffs(std::vector<Puff>& P, float dt, float breeze, int winW, int winH) {
    for (auto& p : P) {
        p.life += dt;
        // Updraft weakens with height; breeze blows right
        float heightNorm = clampf(p.y / (float)winH, 0.f, 1.f);
        float up = (1.0f - 0.4f*heightNorm);
        p.vy = 10.f * up + 8.f;                // keep rising gently
        p.vx += (breeze - p.vx) * 0.05f;       // ease toward breeze
        p.x  += (p.vx + p.wobble*std::sin(2.0f*p.life)) * dt;
        p.y  += p.vy * dt;
        p.r  += p.growth * dt * (0.6f + 0.4f*(1.0f-heightNorm));
        p.whiten = clampf(p.whiten + dt*0.15f, 0.f, 1.f);
        // confine horizontally (wrap)
        if (p.x < -100.f) p.x += (float)winW + 200.f;
        if (p.x >  winW+100.f) p.x -= (float)winW + 200.f;
    }
    // remove old/high puffs
    P.erase(std::remove_if(P.begin(), P.end(), [&](const Puff& p){
        return (p.life > p.maxLife) || (p.y - p.r > winH*1.1f);
    }), P.end());
}

// Soft compositing: draw many overlapping blobs to suggest merging/formation
static void drawClouds(const std::vector<Puff>& P) {
    for (const auto& p : P) {
        // base tint slightly bluish-grey near source, turns white as it matures
        float w = p.whiten;
        GLfloat rgb[3] = {
            0.85f*w + 0.75f*(1.f-w),
            0.86f*w + 0.78f*(1.f-w),
            0.90f*w + 0.82f*(1.f-w)
        };
        // use higher alpha in the center for smaller puffs; larger ones get softer
        float peak = 0.22f * (1.0f / (1.0f + 0.004f*p.r));
        drawSoftBlob(p.x, p.y, p.r, rgb, peak, 9);
    }
}

// ---------- main ----------
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    srand((unsigned)time(nullptr));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Ask for an OpenGL ES 1.1 context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    int winW = 960, winH = 600;
    SDL_Window* win = SDL_CreateWindow(
        "Cloud Formation — SDL2 + OpenGL ES 1.1",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1); // vsync

    auto setOrtho = [&](int w, int h) {
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0.f, (GLfloat)w, 0.f, (GLfloat)h, -1.f, 1.f); // 2D pixels, origin bottom-left
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glShadeModel(GL_SMOOTH);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    };
    setOrtho(winW, winH);

    // Emitters representing moist thermals / convergence lines
    std::vector<Emitter> emitters = {
        { winW*0.18f, winW*0.38f, 110.f, 4.0f },  // left thermal
        { winW*0.55f, winW*0.82f, 110.f, 3.2f }   // right thermal
    };
    float emitterTimerA=0.f, emitterTimerB=0.f;

    std::vector<Puff> puffs;
    bool running = true;
    Uint32 lastTicks = SDL_GetTicks();
    float breeze = 12.f;  // pixels/sec → “wind”

    auto drawScene = [&](float timeSec) {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        // --- Sky gradient ---
        GLfloat top[4]    = {0.42f, 0.66f, 0.95f, 1.f};
        GLfloat mid[4]    = {0.62f, 0.78f, 0.98f, 1.f};
        GLfloat near[4]   = {0.78f, 0.86f, 0.99f, 1.f};
        fillRectGradient(0, winH*0.45f, (GLfloat)winW, winH*0.55f, top, top, mid, mid);
        fillRectGradient(0, 0, (GLfloat)winW, winH*0.45f, mid, mid, near, near);

        // --- Horizon & ground ---
        GLfloat ground[4] = {0.40f, 0.55f, 0.35f, 1.f};
        fillRect(0, 0, (GLfloat)winW, 110.f, ground);

        // Distant hills (simple darker strips)
        GLfloat hill1[4]={0.33f,0.47f,0.32f,1.f};
        fillRect(0, 110.f, (GLfloat)winW, 18.f, hill1);
        GLfloat hill2[4]={0.28f,0.42f,0.30f,1.f};
        fillRect(0, 128.f, (GLfloat)winW, 12.f, hill2);

        // --- Clouds ---
        drawClouds(puffs);

        // Optional faint sun haze
        GLfloat sunRGB[3] = {1.0f, 0.98f, 0.88f};
        drawSoftBlob(winW*0.82f, winH*0.80f, 60.f, sunRGB, 0.06f, 10);
    };

    while (running) {
        // events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                winW = ev.window.data1; winH = ev.window.data2;
                setOrtho(winW, winH);
                // keep emitters anchored near ground
                emitters[0].x0 = winW*0.18f; emitters[0].x1 = winW*0.38f; emitters[0].y = 110.f;
                emitters[1].x0 = winW*0.55f; emitters[1].x1 = winW*0.82f; emitters[1].y = 110.f;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q) running = false;
                if (ev.key.keysym.sym == SDLK_LEFT)  breeze -= 4.f;
                if (ev.key.keysym.sym == SDLK_RIGHT) breeze += 4.f;
                if (ev.key.keysym.sym == SDLK_UP) { // “humid day” → more emission
                    for (auto& e: emitters) e.rate += 0.8f;
                }
                if (ev.key.keysym.sym == SDLK_DOWN) {
                    for (auto& e: emitters) e.rate = std::max(0.6f, e.rate - 0.8f);
                }
            }
        }

        // timing
        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTicks) * 0.001f;
        lastTicks = now;
        dt = clampf(dt, 0.0f, 0.033f); // clamp to keep stable

        // spawn puffs from emitters (Poisson-ish)
        emitterTimerA += dt*emitters[0].rate;
        while (emitterTimerA >= 1.f) { spawnPuff(puffs, emitters[0], winW, winH); emitterTimerA -= 1.f; }
        emitterTimerB += dt*emitters[1].rate;
        while (emitterTimerB >= 1.f) { spawnPuff(puffs, emitters[1], winW, winH); emitterTimerB -= 1.f; }

        // occasionally seed mid-level moisture to hint anvils/merging
        if (frand() < 0.02f*dt*60.f) {
            Emitter mid{ winW*0.30f, winW*0.70f, winH*0.45f + frand()*50.f, 1.0f };
            spawnPuff(puffs, mid, winW, winH);
        }

        // update “atmosphere”
        updatePuffs(puffs, dt, breeze, winW, winH);

        // draw
        glLoadIdentity();
        drawScene(now*0.001f);

        SDL_GL_SwapWindow(win);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
