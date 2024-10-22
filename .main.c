#include <X11/X.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#include <omp.h>
#include "vmath.h"

#define WIDTH 	1920
#define HEIGHT 	1200

#define NO_BALLS 20

typedef struct {
	Display* disp;
	SDL_Window* window;
	SDL_Renderer* renderer;
} Video;

typedef struct {
	Vector2 pos;
	Vector2 vel;
	float radius;
	SDL_Color color;
} Ball;

SDL_Color grid[WIDTH*HEIGHT] = {0};

SDL_Color colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 0, 255, 255}};

Ball balls[NO_BALLS];

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
    return self;
}

static void Clear(Video* self) {
	SDL_SetRenderDrawColor(self->renderer, 0, 0, 0, 255); 
	SDL_RenderClear(self->renderer);
}

static void Teardown(Video* self) {
    XCloseDisplay(self->disp);
    SDL_Quit();
    SDL_DestroyWindow(self->window);
    SDL_DestroyRenderer(self->renderer);
}

static void Render(Video* self) {
	for(int row = 0; row < HEIGHT; row++) {
		for(int col = 0; col < WIDTH; col++) {
			SDL_Color color = grid[col + row*WIDTH];
			SDL_SetRenderDrawColor(self->renderer, color.r, color.g, color.b, color.a);
			SDL_RenderDrawPoint(self->renderer, col, row);
		}
	}
}

/********************** METABALLS ***********************/
void Init() {
	for(int b = 0; b < NO_BALLS; b++) {
		balls[b].pos.x = rand()%WIDTH;
		balls[b].pos.y = rand()%HEIGHT;

		balls[b].vel.x = (float)rand()/(float)RAND_MAX * 8;
		balls[b].vel.y = (float)rand()/(float)RAND_MAX * 8;

		balls[b].radius = (rand()+20)%40;

		int color = rand()%4;
		balls[b].color = colors[color];
	}
}

void Update() {
	#pragma omp parallel for collapse(2)
	for(int row = 0; row < HEIGHT; row++) {
		for(int col = 0; col < WIDTH; col++) {
			int index = col + row*WIDTH;
			SDL_Color pixel = {0};
			for(int b = 0; b < NO_BALLS; b++) {
				float distance = Vector2Distance(balls[b].pos, (Vector2){col, row});
				pixel.r = (balls[b].color.r / distance * balls[b].radius + pixel.r > 255) ? 255 : balls[b].color.r / distance * balls[b].radius + pixel.r; 
				pixel.g = (balls[b].color.g / distance * balls[b].radius + pixel.g > 255) ? 255 : balls[b].color.g / distance * balls[b].radius + pixel.g; 
				pixel.b = (balls[b].color.b / distance * balls[b].radius + pixel.b > 255) ? 255 : balls[b].color.b / distance * balls[b].radius + pixel.b; 
				
			}

			pixel.a = 255;

			grid[index] = pixel;
		}
	}

	for(int i = 0; i < NO_BALLS; i++) {
		balls[i].pos = Vector2Add(balls[i].pos, balls[i].vel);
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
	}
}

int main() {
    Init();
    Video video = Setup();

    while(1) {
			Clear(&video);
			Render(&video);
			SDL_RenderPresent(video.renderer);
			Update();
			SDL_Event event;
			SDL_PollEvent(&event);
			if(event.type == SDL_QUIT)
					break;
		}

    Teardown(&video);

    return 0;
}
