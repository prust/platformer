#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <sys/resource.h>

#include "SDL.h"
// #include "SDL_image.h"
// #include "SDL_mixer.h"
#include "include/font8x8_basic.h"
#include "SDL2_gfxPrimitives.h"

typedef unsigned char byte;

// entity flags
#define WALL         0x1
#define REVERSE_GRAV 0x2
#define LAVA         0x4
#define FINISH       0x8
#define CHECKPOINT   0x10
#define PORTAL       0x20

typedef uint32_t Color;

// DawnBringer23 Palette (http://pixeljoint.com/forum/forum_posts.asp?TID=16247)
Color colors[32] = {
  0xff000000,
  0xff342022,
  0xff3c2845,
  0xff313966,
  0xff3b568f,
  0xff2671df,
  0xff66a0d9,
  0xff9ac3ee,
  0xff36f2fb,
  0xff50e599,
  0xff30be6a,
  0xff6e9437,
  0xff2f694b,
  0xff244b52,
  0xff393c32,
  0xff743f3f,
  0xff826030,
  0xffe16e5b,
  0xffff9b63,
  0xffe4cd5f,
  0xfffcdbcb,
  0xffffffff,
  0xffb7ad9b,
  0xff877e84,
  0xff6a6a69,
  0xff525659,
  0xff8a4276,
  0xff3232ac,
  0xff6357d9,
  0xffba7bd7,
  0xff4a978f,
  0xff306f8a,
};

byte curr_color_ix = 0;

typedef struct {
  short x[64];
  short y[64];
  byte vertices_len;
  byte color_ix;
  bool is_filled;
} Shape;

typedef struct {
  byte flags;
  byte health;
  short x;
  short y;
  short w;
  short h;
  Shape shapes[64];
  byte shapes_len;
} Entity;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} Viewport;

int sign(float n);
int collides(int player_x, int player_y, Entity entities[], byte type);
int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size);

void error(char* activity);

// game globals
Viewport vp = {};

int entities_len;
int max_entities;

FILE *level_file;

// adapted from https://www.reddit.com/r/gamemaker/comments/37y24e/perfect_platformer_code/
float start_grav = 0.2;
float grav = 0.2;
float dx = 0;
float dy = 0;
int jump_speed = 4;
int move_speed = 2;

int start_x = 0;
int player_x = 0;
int start_y = 0;
int player_y = 0;
int player_w = 10;
int player_h = 10;

bool left_pressed = false;
bool right_pressed = false;
bool up_pressed = false;
bool down_pressed = false;

// dead zone makes it so light taps on controller joysticks doesn't drift the player
const int JOYSTICK_DEAD_ZONE = 8000;

int main(int num_args, char* args[]) {
  // increase the stack limit, since we currently allocate *everything* on the stack
  struct rlimit rl;
  int rlimit_rlt = getrlimit(RLIMIT_STACK, &rl);
  if (rlimit_rlt > 0)
    fprintf(stderr, "getrlimit returned error %d\n", rlimit_rlt);
  rl.rlim_cur = 16 * 1024 * 1024; // 16 MiB min stack size
  rlimit_rlt = setrlimit(RLIMIT_STACK, &rl);
  if (rlimit_rlt > 0)
    fprintf(stderr, "setrlimit returned error %d\n", rlimit_rlt);

  entities_len = 0;
  max_entities = 100;
  Entity entities[max_entities];

  time_t seed = 1529597895; //time(NULL);
  srand(seed);
  // printf("Seed: %lld\n", seed);
  
  // SDL setup
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    error("initializing SDL");

  SDL_Window* window;
  window = SDL_CreateWindow("Platformer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 30 * 128, 30 * 128, SDL_WINDOW_RESIZABLE);
  if (!window)
    error("creating window");

  if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) < 0)
    error("Toggling fullscreen mode failed");
  
  // toggle_fullscreen(window);
  SDL_GetWindowSize(window, &vp.w, &vp.h);
  //vp.h -= header_height;

  // level_file = fopen("first.level1", "rb"); // read binary

  // if (level_file != NULL) {
  //   int num_bytes_read = fread(grid_flags, grid_len, 1, level_file); // read bytes to our buffer
  //   if (ferror(level_file))
  //     printf("reading level file: %s\n", strerror(errno));

  //   fclose(level_file);
  // }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  int MAX_CONTROLLERS = 4;
  SDL_GameController *ControllerHandles[MAX_CONTROLLERS];
  for (int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex)
    ControllerHandles[ControllerIndex] = NULL;

  int MaxJoysticks = SDL_NumJoysticks();
  int ControllerIndex = 0;
  for(int JoystickIndex=0; JoystickIndex < MaxJoysticks; ++JoystickIndex) {
    if (!SDL_IsGameController(JoystickIndex))
      continue;
    
    if (ControllerIndex >= MAX_CONTROLLERS)
      break;
    
    ControllerHandles[ControllerIndex] = SDL_GameControllerOpen(JoystickIndex);
    ControllerIndex++;
  }

  SDL_Event evt;
  bool exit_game = false;
  bool is_paused = false;
  unsigned int last_loop_time = SDL_GetTicks();
  unsigned int start_time = SDL_GetTicks();
  unsigned int pause_start = 0;

  bool mode_destroy = false;
  byte mode_type = WALL;
  bool mouse_is_down = false;
  bool won_game = false;

  left_pressed = false;
  right_pressed = false;

  int colors_len = sizeof(colors) / sizeof(colors[0]);
  int palette_color_size = 25;
  int palette_x = 0;
  int palette_y = vp.h - palette_color_size;
  int palette_w = colors_len * palette_color_size;
  int palette_h = palette_color_size;

  // create empty default entity & shape
  Entity default_entity = {
    .flags = 0,
    .health = 0,
    .x = 0,
    .y = 0,
    .w = 0,
    .h = 0,
    .shapes = {},
    .shapes_len = 0
  };

  Shape default_shape = {
    .x = {},
    .y = {},
    .vertices_len = 0,
    .color_ix = curr_color_ix,
    .is_filled = false
  };

  // create a default ground
  entities_len++;
  Entity ground = {
    .flags = WALL,
    .health = 0,
    .x = 0,
    .y = vp.h - 75,
    .w = vp.w,
    .h = 25,
    .shapes = {},
    .shapes_len = 0
  };
  entities[0] = ground;
  entities[0].shapes_len++;
  Shape ground_shape = {
    .x = {0, 0, vp.w, vp.w},
    .y = {vp.h - 75, vp.h - 50, vp.h - 50, vp.h - 75},
    .vertices_len = 4,
    .color_ix = 6,
    .is_filled = true
  };
  entities[0].shapes[0] = ground_shape;

  Shape* selected_shape = NULL;

  while (!exit_game) {
    // reset left/right every time when not using a controller
    // if (controller == NULL) {
      // left_pressed = false;
      // right_pressed = false;
    // }
    up_pressed = false;
    down_pressed = false;
    bool was_paused = is_paused;

    // we have to handle arrow keys via getKeyboardState() in order to support multiple keys at a time
    // SDL_KEYDOWN events only allow one key at a time
    const uint8_t *key_state = SDL_GetKeyboardState(NULL);
    
    while (SDL_PollEvent(&evt)) {
      // this is above the input section b/c it's a pause condition & the pause short-circuits
      // you win if you hit a Finish square
      if (won_game) {
        is_paused = true;
        render_text(renderer, "You Won!", vp.w / 2 - 100, vp.h / 2, 4);
        SDL_RenderPresent(renderer);
      }

      int mouse_x, mouse_y;
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
          mouse_x = evt.button.x + vp.x;
          mouse_y = evt.button.y + vp.y;

          if (mouse_x >= palette_x && mouse_x <= palette_x + palette_w && mouse_y >= palette_y && mouse_y <= palette_y + palette_h) {
            curr_color_ix = (mouse_x - palette_x) / palette_color_size;
            if (selected_shape)
              selected_shape->color_ix = curr_color_ix;
          }
          else if (selected_shape) {
            // snap to complete shape
            if (abs(mouse_x - selected_shape->x[0]) < 8 &&
              abs(mouse_y - selected_shape->y[0]) < 8 && selected_shape->vertices_len > 1) {
              mouse_x = selected_shape->x[0];
              mouse_y = selected_shape->y[0];
              selected_shape->is_filled = true;
            }

            selected_shape->x[selected_shape->vertices_len - 1] = mouse_x;
            selected_shape->y[selected_shape->vertices_len - 1] = mouse_y;
            if (selected_shape->is_filled) {
              selected_shape = NULL;
            }
            else {
              // update entity's bounding box by iterating vertices
              short min_x = selected_shape->x[0];
              short max_x = selected_shape->x[0];
              short min_y = selected_shape->y[0];
              short max_y = selected_shape->y[0];
              for (int i = 1; i < selected_shape->vertices_len; ++i) {
                if (selected_shape->x[i] < min_x)
                  min_x = selected_shape->x[i];
                else if (selected_shape->x[i] > max_x)
                  max_x = selected_shape->x[i];
                if (selected_shape->y[i] < min_y)
                  min_y = selected_shape->y[i];
                else if (selected_shape->y[i] > max_y)
                  max_y = selected_shape->y[i];
              }
              entities[entities_len - 1].x = min_x;
              entities[entities_len - 1].y = min_y;
              entities[entities_len - 1].w = max_x - min_x;
              entities[entities_len - 1].h = max_y - min_y;

              // create new tentative point
              selected_shape->vertices_len++;
              selected_shape->x[selected_shape->vertices_len - 1] = mouse_x;
              selected_shape->y[selected_shape->vertices_len - 1] = mouse_y;
            }
          }
          else {
            entities_len++;
            entities[entities_len - 1] = default_entity;
            entities[entities_len - 1].flags = WALL | mode_type;
            entities[entities_len - 1].shapes_len++;
            entities[entities_len - 1].shapes[0] = default_shape;
            entities[entities_len - 1].shapes[0].color_ix = curr_color_ix;

            selected_shape = &(entities[entities_len - 1].shapes[0]);

            selected_shape->x[0] = mouse_x;
            selected_shape->y[0] = mouse_y;
            selected_shape->vertices_len = 2;
            selected_shape->x[1] = mouse_x;
            selected_shape->y[1] = mouse_y;
          }
          break;

        case SDL_MOUSEMOTION:
          mouse_x = evt.button.x + vp.x;
          mouse_y = evt.button.y + vp.y;

          if (selected_shape && mouse_y < palette_y) {
            // snap to complete shape
            if (abs(mouse_x - selected_shape->x[0]) < 8 &&
              abs(mouse_y - selected_shape->y[0]) < 8) {
              mouse_x = selected_shape->x[0];
              mouse_y = selected_shape->y[0];
            }

            selected_shape->x[selected_shape->vertices_len - 1] = mouse_x;
            selected_shape->y[selected_shape->vertices_len - 1] = mouse_y;
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
            // level_file = fopen("first.level1", "wb"); // read binary

            // if (level_file) {
            //   int num_bytes_written = fwrite(grid_flags, grid_len, 1, level_file); // write bytes to file
            //   if (ferror(level_file))
            //     error("writing level file");
              
            //   fclose(level_file);
            // }
          }
          else if (evt.key.keysym.sym == SDLK_RETURN) {
            if (selected_shape) {
              selected_shape->vertices_len--;
              selected_shape = NULL;
            }
          }
          // gravity-switching
          // not working yet -- reverse gravity prevents left/right movement
          // i think due to the platforming code
          // also, the "jump" mechanic needs to reversed when in reverse gravity

          else if (evt.key.keysym.sym == SDLK_g) {
            mode_type = REVERSE_GRAV;
          }
          else if (evt.key.keysym.sym == SDLK_w) {
            mode_type = WALL;
          }
          else if (evt.key.keysym.sym == SDLK_l) {
            mode_type = LAVA;
          }
          else if (evt.key.keysym.sym == SDLK_f) {
            mode_type = FINISH;
          }
          else if (evt.key.keysym.sym == SDLK_c) {
            mode_type = CHECKPOINT;
          }
          else if (evt.key.keysym.sym == SDLK_p) {
            mode_type = PORTAL;
          }
          break;

        case SDL_JOYAXISMOTION:
          // X axis
          if (evt.jaxis.axis == 0) {
            if (evt.jaxis.value < -JOYSTICK_DEAD_ZONE) {
              left_pressed = true;
            }
            else if (evt.jaxis.value > JOYSTICK_DEAD_ZONE) {
              right_pressed = true;
            }
            else {
              right_pressed = false;
              left_pressed = false;
            }
          }
          break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          if (evt.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            if (grav > 0.0) {
              up_pressed = true;
            }
            else if (grav < 0.0) {
              down_pressed = true;
            }
          }
          break;
      }
    }

    if (key_state[SDL_SCANCODE_LEFT])
      left_pressed = true;
    if (key_state[SDL_SCANCODE_RIGHT])
      right_pressed = true;
    if (key_state[SDL_SCANCODE_UP])
      up_pressed = true;
    if (key_state[SDL_SCANCODE_DOWN])
      down_pressed = true;

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
    if ((grav > 0 && dy < 10) || (grav < 0 && dy > -10))
      dy += grav;

    if (collides(player_x + dx, player_y + dy, entities, REVERSE_GRAV) > -1)
      grav = -grav;

    if (collides(player_x + dx, player_y + dy, entities, FINISH) > -1)
      won_game = true;

    if (collides(player_x + dx, player_y + dy, entities, CHECKPOINT) > -1) {
      start_x = player_x;
      start_y = player_y;
      start_grav = grav;
    }

    int portal_ix = collides(player_x + dx, player_y + dy, entities, PORTAL);
    if (portal_ix > -1) {
      for (int i = 0; i < entities_len; ++i) {
        if (i != portal_ix && entities[i].flags & PORTAL) {
          int delta_x = entities[i].x - entities[portal_ix].x;
          int delta_y = entities[i].y - entities[portal_ix].y;
          player_x += delta_x;
          player_y += delta_y;
          dx = -dx;
          dy = -dy;
          break;
        }
      }
    }

    // start over if you hit lava or fall offscreen
    if ((collides(player_x + dx, player_y + dy, entities, LAVA) > -1) ||
      player_x < 0 || player_x > vp.w || player_y < 0 || player_y > vp.h) {
      grav = start_grav;
      dx = 0;
      dy = 0;
      player_x = start_x;
      player_y = start_y;
    }

    // if touching ground, & jump button pressed, jump
    if (up_pressed && collides(player_x, player_y + 1, entities, WALL) > -1)
      dy = -jump_speed;
    else if (down_pressed && collides(player_x, player_y - 1, entities, WALL) > -1)
      dy = jump_speed;

    // if it's going to collide (horiz), inch there 1px at a time
    if (collides(player_x + dx, player_y, entities, WALL) > -1 && dx) {
      while (!(collides(player_x + sign(dx), player_y, entities, WALL) > -1))
        player_x += sign(dx);
      
      dx = 0;
    }
    player_x += dx;

    // if it's going to collid (vert), inch there 1px at a time
    if (collides(player_x, player_y + dy, entities, WALL) > -1 && dy) {
      while (!(collides(player_x, player_y + sign(dy), entities, WALL) > -1))
        player_y += sign(dy);
      
      dy = 0;
    }
    player_y += dy;


    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 44, 34, 30, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");

    // render polygon
    for (int i = 0; i < entities_len; ++i) {
      Entity* ent = &entities[i];
      Shape* shape = &ent->shapes[0];

      short *vx = shape->x;
      short *vy = shape->y;
      if (shape->is_filled) {
        aapolygonColor(renderer, vx, vy, shape->vertices_len, colors[shape->color_ix]);    
        filledPolygonColor(renderer, vx, vy, shape->vertices_len, colors[shape->color_ix]);// 0xFF000000);
      }
      else {
        for (int j = 0; j < shape->vertices_len - 1; ++j) {
          aalineColor(renderer, vx[j], vy[j], vx[j + 1], vy[j + 1], colors[shape->color_ix]);

          // thickLineColor(renderer, vx[j], vy[j], vx[j + 1], vy[j + 1], 2, colors[shape->color_ix]);
          
          // I tried another AA line, 1px shifted in both dimensions, to make line thicker
          // but that produced weird artifacts/optical illusions
          // best to do non-AA lines w/ AA lines on both sides to smooth?
          // or, probably best to just implement an aathickLineColor() function
        }
      }
    }

    // draw palette
    for (int i = 0; i < colors_len; ++i)
      boxColor(renderer, i * palette_color_size, palette_y, i * palette_color_size + palette_color_size, palette_y + palette_h, colors[i]);

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

  for (int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex)
    if (ControllerHandles[ControllerIndex])
      SDL_GameControllerClose(ControllerHandles[ControllerIndex]);

  if (SDL_SetWindowFullscreen(window, 0) < 0)
    error("exiting fullscreen");

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
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

int collides(int player_x, int player_y, Entity entities[], byte type) {
  int player_x2 = player_x + player_w;
  int player_y2 = player_y + player_h;

  for (int i = 0; i < entities_len; ++i) {
    if (entities[i].flags & type) {
      int x = entities[i].x;
      int y = entities[i].y;
      int x2 = entities[i].x + entities[i].w;
      int y2 = entities[i].y + entities[i].h;

      // DO collide if
      if (player_x2 > x && player_x < x2 &&
        player_y2 > y && player_y < y2)
          return i;
    }
  }
  return -1;
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, strerror(errno));//SDL_GetError());
  SDL_Quit();
  exit(-1);
}

int sign(float n) {
  if (n > 0)
    return 1;
  else if (n < 0)
    return -1;
  else
    return 0;
}
