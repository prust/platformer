#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "font8x8_basic.h"

typedef unsigned char byte;

// grid flags
#define WALL 0x1

typedef struct {
  byte flags;
  byte health;
  unsigned int create_time;
  int x;
  int y;
} Entity;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} Viewport;

bool in_bounds(int x, int y);
int to_x(int ix);
int to_y(int ix);
int to_pos(int x, int y);
void error(char* activity);

// game globals
Viewport vp = {};

int block_w = 30;
int block_h = 30;
const int num_blocks_w = 128; // 2^7
const int num_blocks_h = 128; // 2^7
const int grid_len = num_blocks_w * num_blocks_h;

int main(int num_args, char* args[]) {
  byte grid_flags[grid_len];

  time_t seed = 1529597895; //time(NULL);
  srand(seed);
  // printf("Seed: %lld\n", seed);
  
  // SDL setup
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    error("initializing SDL");

  SDL_Window* window;
  window = SDL_CreateWindow("Future Fortress", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, num_blocks_w * block_w, num_blocks_h * block_h, SDL_WINDOW_RESIZABLE);
  if (!window)
    error("creating window");

  if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) < 0)
    error("Toggling fullscreen mode failed");
  
  // toggle_fullscreen(window);
  SDL_GetWindowSize(window, &vp.w, &vp.h);
  //vp.h -= header_height;

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  // have to manually init b/c C doesn't allow initializing VLAs w/ {0}
  for (int i = 0; i < grid_len; ++i)
    grid_flags[i] = 0;

  SDL_Event evt;
  bool exit_game = false;
  bool is_paused = false;
  unsigned int last_loop_time = SDL_GetTicks();
  unsigned int start_time = SDL_GetTicks();
  unsigned int pause_start = 0;

  bool mode_destroy = false;
  bool mouse_is_down = false;

  while (!exit_game) {
    bool was_paused = is_paused;
    while (SDL_PollEvent(&evt)) {
      int x, y;
      switch(evt.type) {
        case SDL_QUIT:
          exit_game = true;
          break;
        
        case SDL_WINDOWEVENT:
          if (evt.window.event == SDL_WINDOWEVENT_RESIZED) {
            SDL_GetWindowSize(window, &vp.w, &vp.h);
            // vp.h -= header_height;
          }
          break;

        case SDL_MOUSEBUTTONUP:
          mouse_is_down = false;
          break;

        case SDL_MOUSEBUTTONDOWN:
          mouse_is_down = true;
          x = (evt.button.x + vp.x) / block_w;
          y = (evt.button.y + vp.y) / block_h;

          if (in_bounds(x, y)) {
            if (grid_flags[to_pos(x, y)] & WALL) {
              mode_destroy = true;
              grid_flags[to_pos(x, y)] &= (~WALL); // clear WALL bit
            }
            else {
              mode_destroy = false;
              grid_flags[to_pos(x, y)] |= WALL; // set WALL bit
            }            
          }
          break;

        case SDL_MOUSEMOTION:
          if (mouse_is_down) {
            x = (evt.button.x + vp.x) / block_w;
            y = (evt.button.y + vp.y) / block_h;
            if (in_bounds(x, y)) {
              if (mode_destroy) {
                grid_flags[to_pos(x, y)] &= (~WALL); // clear WALL bit
              }
              else {
                grid_flags[to_pos(x, y)] |= WALL; // set WALL bit
              }
            }
          }
          break;

        case SDL_KEYDOWN:
          if (evt.key.keysym.sym == SDLK_ESCAPE)
            exit_game = true;
          else if (evt.key.keysym.sym == SDLK_SPACE)
            is_paused = true;
          break;
      }


      // else if (evt.type == SDL_MOUSEBUTTONDOWN && is_mouseover(&start_game_img, evt.button.x, evt.button.y)) {
      //   SDL_SetCursor(arrow_cursor);
      //   play_level(window, renderer);
      // }
    }

    // handle pause state
    if (was_paused || is_paused) {
      if (!pause_start)
        pause_start = SDL_GetTicks();

      // still paused
      if (is_paused) {
        SDL_Delay(10);
        continue;
      }
      // coming out of paused state
      else {
        // reset last_loop_time when coming out of a pause state
        // otherwise the game will react as if a ton of time has gone by
        last_loop_time = SDL_GetTicks();

        // add elapsed pause time to start time, otherwise the pause time is added to the clock
        start_time += last_loop_time - pause_start;
        pause_start = 0;
      }
    }

    // manage delta time
    unsigned int curr_time = SDL_GetTicks();
    double dt = (curr_time - last_loop_time) / 1000.0; // dt should always be in seconds

    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 44, 34, 30, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");

    // render grid
    if (SDL_SetRenderDrawColor(renderer, 145, 103, 47, 255) < 0)
      error("setting land color");

    for (int i = 0; i < grid_len; ++i) {
      int x = to_x(i);
      int y = to_y(i);

      if (grid_flags[i] & WALL) {
        SDL_Rect wall_rect = {
          .x = x * block_w - vp.x,
          .y = y * block_h - vp.y,
          .w = block_w,
          .h = block_h
        };
        if (SDL_RenderFillRect(renderer, &wall_rect) < 0)
          error("filling wall rect");
      }
    }

    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

  if (SDL_SetWindowFullscreen(window, 0) < 0)
    error("exiting fullscreen");

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

int to_x(int ix) {
  return ix % num_blocks_w;
}

int to_y(int ix) {
  return ix / num_blocks_w;
}

int to_pos(int x, int y) {
  int pos = x + y * num_blocks_w;
  if (pos < 0)
    error("position out of bounds (negative)");
  if (pos >= grid_len)
    error("position out of bounds (greater than grid size)");
  return pos;
}

bool in_bounds(int x, int y) {
  return x >= 0 && x < num_blocks_w &&
    y >= 0 && y < num_blocks_h;
}

int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size) {
  int i;
  for (i = 0; str[i] != '\0'; ++i) {
    int code = str[i];
    if (code < 0 || code > 127)
      error("Text code out of range");

    char* bitmap = font8x8_basic[code];
    int set = 0;
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        set = bitmap[y] & 1 << x;
        if (!set)
          continue;

        SDL_Rect r = {
          .x = offset_x + i * (size) * 8 + x * size,
          .y = offset_y + y * size,
          .w = size,
          .h = size
        };
        if (SDL_RenderFillRect(renderer, &r) < 0)
          error("drawing text block");
      }
    }
  }

  // width of total text string
  return i * size * 8;
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, SDL_GetError());
  SDL_Quit();
  exit(-1);
}
