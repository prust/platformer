#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

#include "include/SDL.h"
#include "include/SDL_image.h"
#include "include/SDL_mixer.h"
#include "include/font8x8_basic.h"

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

int sign(int n);
bool collides_w_wall(int player_x, int player_y, byte grid_flags[]);

void error(char* activity);

// game globals
Viewport vp = {};

int block_w = 30;
int block_h = 30;
const int num_blocks_w = 128; // 2^7
const int num_blocks_h = 128; // 2^7
const int grid_len = num_blocks_w * num_blocks_h;

FILE *level_file;

// adapted from https://www.reddit.com/r/gamemaker/comments/37y24e/perfect_platformer_code/
float grav = 0.2;
float dx = 0;
float dy = 0;
int jump_speed = 4;
int move_speed = 2;

int player_x = 0;
int player_y = 0;
int player_w = 10;
int player_h = 10;

bool left_pressed = false;
bool right_pressed = false;
bool up_pressed = false;

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

  level_file = fopen("first.level1", "rb"); // read binary

  if (level_file != NULL) {
    int num_bytes_read = fread(grid_flags, grid_len, 1, level_file); // read bytes to our buffer
    if (ferror(level_file))
      printf("reading level file: %s\n", strerror(errno));

    fclose(level_file);
  }
  else {
    // have to manually init b/c C doesn't allow initializing VLAs w/ {0}
    for (int i = 0; i < grid_len; ++i)
      grid_flags[i] = 0;

    // create a 'floor' of blocks along the bottom
    int y = vp.h / block_h - 1;
    for (int x = 0; x < num_blocks_w; ++x)
      grid_flags[to_pos(x, y)] |= WALL; // set WALL bit
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  SDL_Event evt;
  bool exit_game = false;
  bool is_paused = false;
  unsigned int last_loop_time = SDL_GetTicks();
  unsigned int start_time = SDL_GetTicks();
  unsigned int pause_start = 0;

  bool mode_destroy = false;
  bool mouse_is_down = false;

  while (!exit_game) {
    left_pressed = false;
    right_pressed = false;
    up_pressed = false;
    bool was_paused = is_paused;

    // we have to handle arrow keys via getKeyboardState() in order to support multiple keys at a time
    // SDL_KEYDOWN events only allow one key at a time
    const uint8_t *key_state = SDL_GetKeyboardState(NULL);
    
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
          if (evt.key.keysym.sym == SDLK_ESCAPE) {
            exit_game = true;
          }
          else if (evt.key.keysym.sym == SDLK_SPACE) {
            is_paused = !is_paused;
          }
          else if (evt.key.keysym.sym == SDLK_s) {
            level_file = fopen("first.level1", "wb"); // read binary

            if (level_file) {
              int num_bytes_written = fwrite(grid_flags, grid_len, 1, level_file); // write bytes to file
              if (ferror(level_file))
                error("writing level file");
              
              fclose(level_file);
            }
          }
          // gravity-switching
          // not working yet -- reverse gravity prevents left/right movement
          // i think due to the platforming code
          // also, the "jump" mechanic needs to reversed when in reverse gravity

          // else if (evt.key.keysym.sym == SDLK_g)
          //   grav = -grav;
          break;
      }
    }

    if (key_state[SDL_SCANCODE_LEFT])
      left_pressed = true;
    if (key_state[SDL_SCANCODE_RIGHT])
      right_pressed = true;
    if (key_state[SDL_SCANCODE_UP])
      up_pressed = true;

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


    // left/right movement
    if (left_pressed)
      dx = -move_speed;
    else if (right_pressed)
      dx = move_speed;
    else
      dx = 0;
    
    // gravity
    if (dy < 10)
      dy += grav;

    // if touching ground, & jump button pressed, jump
    if (up_pressed && collides_w_wall(player_x, player_y + 1, grid_flags))
      dy = -jump_speed;

    // if it's going to collide (horiz), inch there 1px at a time
    if (collides_w_wall(player_x + dx, player_y, grid_flags) && dx) {
      while (!collides_w_wall(player_x + sign(dx), player_y, grid_flags))
        player_x += sign(dx);
      
      dx = 0;
    }
    player_x += dx;

    // if it's going to collid (vert), inch there 1px at a time
    if (collides_w_wall(player_x, player_y + dy, grid_flags) && dy) {
      while (!collides_w_wall(player_x, player_y + sign(dy), grid_flags))
        player_y += sign(dy);
      
      dy = 0;
    }
    player_y += dy;


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

    // render player
    if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0)
      error("setting player color");

    SDL_Rect player_rect = {
      .x = player_x - vp.x,
      .y = player_y - vp.y,
      .w = player_w,
      .h = player_h
    };
    if (SDL_RenderFillRect(renderer, &player_rect) < 0)
      error("filling player rect");

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

bool collides_w_wall(int player_x, int player_y, byte grid_flags[]) {
  int player_x2 = player_x + player_w;
  int player_y2 = player_y + player_h;

  for (int i = 0; i < grid_len; ++i) {
    if (grid_flags[i] & WALL) {
      int x = to_x(i) * block_w;
      int y = to_y(i) * block_h;
      int x2 = x + block_w;
      int y2 = y + block_h;

      // DO collide if
      if (player_x2 > x && player_x < x2 &&
        player_y2 > y && player_y < y2)
          return true;
    }
  }
  return false;
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, strerror(errno));//SDL_GetError());
  SDL_Quit();
  exit(-1);
}

int sign(int n) {
  if (n > 0)
    return 1;
  else if (n < 0)
    return -1;
  else
    return 0;
}
