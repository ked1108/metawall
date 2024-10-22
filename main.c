#include <X11/X.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#include <omp.h>
#include <math.h>
#include "vmath.h"

#define WIDTH   1920
#define HEIGHT  1200
#define NO_BALLS 20
#define BASE_SPEED 300.0f
#define GRID_SCALE 2  // Render at half resolution
#define INFLUENCE_RADIUS 200.0f  // Increased for smoother falloff
#define LOOKUP_TABLE_SIZE 1000
#define SOFT_EDGE_FACTOR 0.75f  // Controls the softness of the edge falloff

typedef struct {
    Display* disp;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    Uint64 previousTime;
    float distanceLookup[LOOKUP_TABLE_SIZE];
} Video;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float radius;
    SDL_Color color;
    SDL_Rect bounds;
    float intensity;  // Added intensity factor per ball
} Ball;

uint32_t* pixelBuffer;
SDL_Color colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 0, 255, 255}};
Ball balls[NO_BALLS];

// Improved smooth falloff function
static inline float SmoothFalloff(float distance, float radius, float influence_radius) {
    if (distance >= influence_radius) return 0.0f;
    
    // Normalized distance between 0 and 1
    float normalized_dist = distance / influence_radius;
    
    // Smoothstep-like function for natural falloff
    float x = 1.0f - normalized_dist;
    float falloff = x * x * (3 - 2 * x);
    
    // Apply radius influence
    falloff *= radius / 40.0f;  // Normalize radius effect
    
    // Add soft edge transition
    falloff *= (1.0f - powf(normalized_dist, SOFT_EDGE_FACTOR));
    
    return falloff;
}

static inline uint32_t ColorToUint32(SDL_Color color) {
    return (color.a << 24) | (color.b << 16) | (color.g << 8) | color.r;
}

// Improved color blending with gamma correction
static inline uint32_t BlendColors(uint32_t existing, uint32_t new, float factor) {
    // Extract color channels
    float r1 = (existing & 0xFF) / 255.0f;
    float g1 = ((existing >> 8) & 0xFF) / 255.0f;
    float b1 = ((existing >> 16) & 0xFF) / 255.0f;
    
    float r2 = (new & 0xFF) / 255.0f;
    float g2 = ((new >> 8) & 0xFF) / 255.0f;
    float b2 = ((new >> 16) & 0xFF) / 255.0f;
    
    // Apply gamma correction and blend
    r1 = sqrtf(r1 * r1 + r2 * r2 * factor);
    g1 = sqrtf(g1 * g1 + g2 * g2 * factor);
    b1 = sqrtf(b1 * b1 + b2 * b2 * factor);
    
    // Convert back to 8-bit color
    uint32_t r = (uint32_t)(fmin(r1 * 255.0f, 255.0f));
    uint32_t g = (uint32_t)(fmin(g1 * 255.0f, 255.0f));
    uint32_t b = (uint32_t)(fmin(b1 * 255.0f, 255.0f));
    
    return (0xFF << 24) | (b << 16) | (g << 8) | r;
}

static void InitDistanceLookup(Video* video) {
    for (int i = 0; i < LOOKUP_TABLE_SIZE; i++) {
        float distance = (float)i / (float)LOOKUP_TABLE_SIZE * INFLUENCE_RADIUS;
        // Create smoother falloff curve in lookup table
        video->distanceLookup[i] = SmoothFalloff(distance, 40.0f, INFLUENCE_RADIUS);
    }
}

/********************* SDL ************************/
static Video Setup(void) {
    Video self;
    self.disp = XOpenDisplay(NULL);
    if(!self.disp) {
        fprintf(stderr, "cant open display :(");
    }
    const Window x11w = RootWindow(self.disp, DefaultScreen(self.disp));
    if(!x11w) {
        fprintf(stderr, "Error getting window");
    }
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "cant intialise SDL :( :%s", SDL_GetError());
    };
    self.window = SDL_CreateWindowFrom((void*) x11w);
    if(!self.window) {
        fprintf(stderr, "cant create window :( :%s", SDL_GetError());
    }
    self.renderer = SDL_CreateRenderer(self.window, 
                                     -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    self.texture = SDL_CreateTexture(self.renderer,
                                   SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   WIDTH/GRID_SCALE, HEIGHT/GRID_SCALE);
    if (!self.texture) {
        fprintf(stderr, "Failed to create texture: %s\n", SDL_GetError());
    }
    
    self.previousTime = SDL_GetPerformanceCounter();
    
    // Initialize lookup table
    InitDistanceLookup(&self);
    
    // Allocate pixel buffer
    pixelBuffer = (uint32_t*)malloc((WIDTH/GRID_SCALE) * (HEIGHT/GRID_SCALE) * sizeof(uint32_t));
    
    return self;
}

static void Clear(Video* self) {
    // Fast clear of pixel buffer
    memset(pixelBuffer, 0, (WIDTH/GRID_SCALE) * (HEIGHT/GRID_SCALE) * sizeof(uint32_t));
}

static void Teardown(Video* self) {
    free(pixelBuffer);
    SDL_DestroyTexture(self->texture);
    XCloseDisplay(self->disp);
    SDL_Quit();
    SDL_DestroyWindow(self->window);
    SDL_DestroyRenderer(self->renderer);
}

static void UpdateBallBounds(Ball* ball) {
    // Increased bounds to account for soft edges
    int radius = (int)(ball->radius * INFLUENCE_RADIUS / GRID_SCALE * 1.2f);
    ball->bounds.x = (int)(ball->pos.x/GRID_SCALE) - radius;
    ball->bounds.y = (int)(ball->pos.y/GRID_SCALE) - radius;
    ball->bounds.w = radius * 2;
    ball->bounds.h = radius * 2;
    
    // Clamp to screen bounds
    ball->bounds.x = fmax(0, ball->bounds.x);
    ball->bounds.y = fmax(0, ball->bounds.y);
    ball->bounds.w = fmin(ball->bounds.w, WIDTH/GRID_SCALE - ball->bounds.x);
    ball->bounds.h = fmin(ball->bounds.h, HEIGHT/GRID_SCALE - ball->bounds.y);
}

void Init() {
    for(int b = 0; b < NO_BALLS; b++) {
        balls[b].pos.x = rand()%WIDTH;
        balls[b].pos.y = rand()%HEIGHT;
        balls[b].vel.x = ((float)rand()/(float)RAND_MAX * 2.0f - 1.0f);
        balls[b].vel.y = ((float)rand()/(float)RAND_MAX * 2.0f - 1.0f);
        float length = sqrt(balls[b].vel.x * balls[b].vel.x + balls[b].vel.y * balls[b].vel.y);
        balls[b].vel.x /= length;
        balls[b].vel.y /= length;
        balls[b].radius = (rand()%20 + 20);  // Adjusted radius range
        balls[b].color = colors[rand()%4];
        balls[b].intensity = (float)(rand()%50 + 50) / 100.0f;  // Random intensity between 0.5 and 1.0
        UpdateBallBounds(&balls[b]);
    }
}

void Update(Video* video) {
    Uint64 currentTime = SDL_GetPerformanceCounter();
    float deltaTime = (float)(currentTime - video->previousTime) / SDL_GetPerformanceFrequency();
    video->previousTime = currentTime;

    // Update ball positions
    for(int i = 0; i < NO_BALLS; i++) {
        Vector2 movement = {
            balls[i].vel.x * BASE_SPEED * deltaTime,
            balls[i].vel.y * BASE_SPEED * deltaTime
        };
        balls[i].pos = Vector2Add(balls[i].pos, movement);

        if(balls[i].pos.x <= 0) {
            balls[i].pos.x = 0;
            balls[i].vel.x *= -1;
        }
        if(balls[i].pos.x >= WIDTH) {
            balls[i].pos.x = WIDTH;
            balls[i].vel.x *= -1;
        }
        if(balls[i].pos.y <= 0 ) {
            balls[i].pos.y = 0;
            balls[i].vel.y *= -1;
        }
        if(balls[i].pos.y >= HEIGHT) {
            balls[i].pos.y = HEIGHT;
            balls[i].vel.y *= -1;
        }
        
        UpdateBallBounds(&balls[i]);
    }

    #pragma omp parallel for collapse(2) schedule(dynamic, 16)
    for(int row = 0; row < HEIGHT/GRID_SCALE; row++) {
        for(int col = 0; col < WIDTH/GRID_SCALE; col++) {
            uint32_t pixel = 0;
            float totalInfluence = 0.0f;
            
            for(int b = 0; b < NO_BALLS; b++) {
                if (col >= balls[b].bounds.x && col < balls[b].bounds.x + balls[b].bounds.w &&
                    row >= balls[b].bounds.y && row < balls[b].bounds.y + balls[b].bounds.h) {
                    
                    float dx = col * GRID_SCALE - balls[b].pos.x;
                    float dy = row * GRID_SCALE - balls[b].pos.y;
                    float distance = sqrt(dx*dx + dy*dy);
                    
                    if (distance < INFLUENCE_RADIUS) {
                        float factor = SmoothFalloff(distance, balls[b].radius, INFLUENCE_RADIUS);
                        factor *= balls[b].intensity;  // Apply ball's intensity
                        
                        pixel = BlendColors(pixel, ColorToUint32(balls[b].color), factor);
                        totalInfluence += factor;
                    }
                }
            }
            
            // Add subtle ambient glow
            if (totalInfluence > 0.0f) {
                float ambientFactor = totalInfluence * 0.15f;  // Adjust for desired glow amount
                pixel = BlendColors(pixel, 0xFFFFFFFF, ambientFactor);
            }
            
            pixelBuffer[col + row * (WIDTH/GRID_SCALE)] = pixel;
        }
    }
}

static void Render(Video* self) {
    void* pixels;
    int pitch;
    SDL_LockTexture(self->texture, NULL, &pixels, &pitch);
    memcpy(pixels, pixelBuffer, (WIDTH/GRID_SCALE) * (HEIGHT/GRID_SCALE) * sizeof(uint32_t));
    SDL_UnlockTexture(self->texture);
    
    // Render scaled texture to fill window
    SDL_Rect dest = {0, 0, WIDTH, HEIGHT};
    SDL_RenderCopy(self->renderer, self->texture, NULL, &dest);
}

int main() {
    Init();
    Video video = Setup();
    
    while(1) {
        Clear(&video);
        Update(&video);
        Render(&video);
        SDL_RenderPresent(video.renderer);
        
        SDL_Event event;
        SDL_PollEvent(&event);
        if(event.type == SDL_QUIT)
            break;
    }
    
    Teardown(&video);
    return 0;
}

