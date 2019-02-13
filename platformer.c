#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

#include "SDL.h"
// #include "SDL_image.h"
// #include "SDL_mixer.h"
#include "include/font8x8_basic.h"
#include "SDL2_gfxPrimitives.h"

typedef unsigned char byte;

// entity flags
#define WALL          0x1
#define REVERSE_GRAV  0x2
#define LAVA          0x4
#define FINISH        0x8
#define CHECKPOINT    0x10
#define PORTAL        0x20
#define UNLINKED_BBOX 0x40
#define ENEMY         0x80

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

// shape types
byte POLYGON = 0;

byte NO_COLOR = 32;

byte curr_color_ix = 0;

typedef struct {
  byte type;
  byte fill_color_ix;
  byte stroke_width;
  byte stroke_color_ix;
  byte len_vertices;
  byte max_vertices;
  short* x;
  short* y;
} Shape;

typedef struct {
  byte flags;
  byte health;
  short x;
  short y;
  short w;
  short h;
  float dx;
  float dy;
  float grav_x;
  float grav_y;
  byte len_shapes;
  byte max_shapes;
  Shape* shapes;
} Entity;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} Viewport;

Entity* createEntity(byte mode_type, byte color_ix, short x, short y, short w, short h);
void updateEntityBBox(Entity* ent);
void deleteEntity(int entity_ix);
int indexOfEntity(short x, short y, short w, short h);
void addRectPoints(Shape* shape, short x, short y, short w, short h);
void addPoint(Shape* shape, short x, short y);
void fillShape(Shape* shape);
int render_text(SDL_Renderer* renderer, char str[], int offset_x, int offset_y, int size);
int will_collide(Entity* ent, byte type);
int collides(int x, int y, int w, int h, int ix, Entity entities[], byte type);
int sign(float n);
void error(char* activity);

// game globals
Viewport vp = {};

byte grid_size = 30;

short len_entities;
#define max_entities 100
Entity entities[max_entities];

FILE *level_file;

// adapted from https://www.reddit.com/r/gamemaker/comments/37y24e/perfect_platformer_code/
float start_grav = 0.2;
int jump_speed = 4;
int move_speed = 2;

int start_x = 0;
int start_y = 0;

bool left_pressed = false;
bool right_pressed = false;
bool up_pressed = false;
bool down_pressed = false;

short level_w;
short level_h;

// dead zone makes it so light taps on controller joysticks doesn't drift the player
const int JOYSTICK_DEAD_ZONE = 8000;

int main(int num_args, char* args[]) {
  time_t seed = 1529597895; //time(NULL);
  srand(seed);
  // printf("Seed: %lld\n", seed);

  Entity player = {
    .flags = 0,
    .health = 1,
    .x = 0,
    .y = 0,
    .w = 10,
    .h = 10,
    .dx = 0,
    .dy = 0,
    .grav_x = 0,
    .grav_y = 0.2,
    // since we're not rendering players generically yet, we don't need to set shapes
    .len_shapes = 0,
    .max_shapes = 0,
    .shapes = NULL
  };
  
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

  level_file = fopen("current.level2", "rb"); // read binary

  if (level_file == NULL) {
    len_entities = 0;
    level_w = vp.w;
    level_h = vp.h;
    
    // create a default ground entity/shape
    Entity* ground_ent = createEntity(WALL, 6, 0, vp.h - (vp.h % grid_size) - grid_size, vp.w, grid_size);
    Shape* ground_shape = &(ground_ent->shapes[0]);
    fillShape(ground_shape);
    addRectPoints(ground_shape, 0, 0, vp.w, grid_size);
  }
  else {
    // Load level from file (deserialize)

    // seek to the end to find the size
    if (fseek(level_file, 0, SEEK_END)) {
      fclose(level_file);
      error("Error seeking to the end of the level file");
    }

    size_t num_bytes = ftell(level_file);

    if (fseek(level_file, 0, SEEK_SET)) {
      fclose(level_file);
      error("Error seeking to the beginning of the level file");
    }

    void* buffer = calloc(1, num_bytes);
    fread(buffer, num_bytes, 1, level_file); // read bytes into our buffer
    if (ferror(level_file))
      printf("reading level file: %s\n", strerror(errno));
    
    fclose(level_file);

    void* buffer_ix = buffer;
    memcpy(&len_entities, buffer_ix, sizeof(len_entities));
    buffer_ix += sizeof(len_entities);
    memcpy(&level_w, buffer_ix, sizeof(level_w));
    buffer_ix += sizeof(level_w);
    memcpy(&level_h, buffer_ix, sizeof(level_h));
    buffer_ix += sizeof(level_h);

    for (int i = 0; i < len_entities; ++i) {
      Entity* entity = &entities[i];
      memcpy(entity, buffer_ix, sizeof(Entity));
      buffer_ix += sizeof(Entity);

      entity->shapes = (Shape*)calloc(64, sizeof(Shape));
      for (int j = 0; j < entity->len_shapes; ++j) {
        Shape* shape = &(entity->shapes[j]);
        memcpy(shape, buffer_ix, sizeof(Shape));
        buffer_ix += sizeof(Shape);

        shape->x = (short*)calloc(64, sizeof(short)),
        shape->y = (short*)calloc(64, sizeof(short)),

        // copy x & y vertices
        memcpy(shape->x, buffer_ix, shape->len_vertices * sizeof(short));
        buffer_ix += shape->len_vertices * sizeof(short);
        memcpy(shape->y, buffer_ix, shape->len_vertices * sizeof(short));
        buffer_ix += shape->len_vertices * sizeof(short);
      }
    }

    // sanity check
    if (buffer_ix - buffer != num_bytes) {
      printf("%lu - num_bytes\n", num_bytes);
      printf("%lu - pointer diff\n", buffer_ix - buffer);
      error("byte mismatch on file save");
    }
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
    error("creating renderer");

  if (SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) < 0)
    error("setting blend mode");

  // setup controllers (& controller joysticks)
  bool has_controller = false;
  int max_controllers = SDL_NumJoysticks();
  SDL_GameController* controllers[max_controllers];
  for (int i = 0; i < max_controllers; ++i) {
    if (SDL_IsGameController(i)) {
      controllers[i] = SDL_GameControllerOpen(i); // need to do this in order to receive events
      has_controller = true;
    }
    else {
      controllers[i] = NULL;
    }
  }

  SDL_Event evt;
  bool exit_game = false;
  bool is_paused = false;
  unsigned int last_loop_time = SDL_GetTicks();
  unsigned int start_time = SDL_GetTicks();
  unsigned int pause_start = 0;

  bool destroy_mode = false;
  byte mode_type = WALL;
  bool tile_mode = true;
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

  Shape* selected_shape = NULL;

  while (!exit_game) {
    // reset left/right every time when not using a controller
    if (!has_controller) {
      left_pressed = false;
      right_pressed = false;
    }
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
              selected_shape->stroke_color_ix = curr_color_ix;
          }
          else if (tile_mode) {
            short x = mouse_x - (mouse_x % grid_size);
            short y = mouse_y - (mouse_y % grid_size);

            int existing_tile_ix = indexOfEntity(x, y, grid_size, grid_size);

            if (existing_tile_ix > -1) {
              destroy_mode = true;
              deleteEntity(existing_tile_ix);
            }
            else {
              destroy_mode = false;
              Entity* ent = createEntity(mode_type, curr_color_ix, x, y, grid_size, grid_size);
              Shape* shape = &(ent->shapes[0]);
              fillShape(shape);
              addRectPoints(shape, 0, 0, grid_size, grid_size);
              if (mode_type == ENEMY) {
                ent->dx = 1;
                ent->grav_y = 0.2;
              }
            }
          }
          else { // drawing-mode
            if (selected_shape) {
              // x/y relative to entity
              short x = mouse_x - entities[len_entities - 1].x;
              short y = mouse_y - entities[len_entities - 1].y;

              // snap to complete shape
              if (abs(x - selected_shape->x[0]) < 8 && abs(y - selected_shape->y[0]) < 8 && selected_shape->len_vertices > 1) {
                x = selected_shape->x[0];
                y = selected_shape->y[0];
                fillShape(selected_shape);
              }

              selected_shape->x[selected_shape->len_vertices - 1] = x;
              selected_shape->y[selected_shape->len_vertices - 1] = y;
              // FIX: this is confusing, its just checking whether we ran fillShape() above
              if (selected_shape->fill_color_ix != NO_COLOR) {
                selected_shape = NULL;
              }
              else {
                updateEntityBBox(&(entities[len_entities - 1]));

                // create new tentative point
                addPoint(selected_shape, x, y);
              }
            }
            else {
              Entity* ent = createEntity(mode_type, curr_color_ix, mouse_x, mouse_y, 0, 0);
              if (mode_type == ENEMY) {
                ent->dx = 1;
                ent->grav_y = 0.2;
              }
              selected_shape = &(ent->shapes[0]);

              selected_shape->x[0] = 0;
              selected_shape->y[0] = 0;
              selected_shape->len_vertices = 2;
              selected_shape->x[1] = 0;
              selected_shape->y[1] = 0;
            }
          }
          break;

        case SDL_MOUSEMOTION:
          mouse_x = evt.button.x + vp.x;
          mouse_y = evt.button.y + vp.y;
          if (mouse_y < palette_y) {
            if (tile_mode) {
              if (mouse_is_down && mode_type != ENEMY) {
                short x = mouse_x - (mouse_x % grid_size);
                short y = mouse_y - (mouse_y % grid_size);
                
                if (destroy_mode) {
                  int existing_tile_ix = indexOfEntity(x, y, grid_size, grid_size);
                  if (existing_tile_ix > -1)
                    deleteEntity(existing_tile_ix);
                }
                else {
                  int existing_tile_ix = indexOfEntity(x, y, grid_size, grid_size);
                  if (existing_tile_ix == -1) {
                    Entity* ent = createEntity(mode_type, curr_color_ix, x, y, grid_size, grid_size);
                    Shape* shape = &(ent->shapes[0]);
                    fillShape(shape);
                    addRectPoints(shape, 0, 0, grid_size, grid_size);
                    if (mode_type == ENEMY) {
                      ent->dx = 1;
                      ent->grav_y = 0.2;
                    }
                  }
                }
              }
            }
            else { // drawing mode
              if (selected_shape) {
                // x/y relative to entity
                short x = mouse_x - entities[len_entities - 1].x;
                short y = mouse_y - entities[len_entities - 1].y;
                
                // snap to complete shape
                if (abs(x - selected_shape->x[0]) < 8 && abs(y - selected_shape->y[0]) < 8) {
                  x = selected_shape->x[0];
                  y = selected_shape->y[0];
                }

                selected_shape->x[selected_shape->len_vertices - 1] = x;
                selected_shape->y[selected_shape->len_vertices - 1] = y;
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
          else if (evt.key.keysym.sym == SDLK_t) {
            tile_mode = !tile_mode;
          }
          else if (evt.key.keysym.sym == SDLK_s) {
            level_file = fopen("current.level2", "wb"); // read binary

            if (level_file) {
              // calc the # of bytes we need
              size_t num_bytes = sizeof(len_entities) + sizeof(level_w) + sizeof(level_h);
              for (int i = 0; i < len_entities; ++i) {
                Entity* entity = &entities[i];
                num_bytes += sizeof(Entity);

                for (int j = 0; j < entity->len_shapes; ++j) {
                  Shape* shape = &(entity->shapes[j]);
                  num_bytes += sizeof(Shape);
                  num_bytes += shape->len_vertices * sizeof(short) * 2;
                }
              }

              // malloc() a buffer & copy the bytes to the buffer
              void* buffer = malloc(num_bytes);
              void* buffer_ix = buffer;
              memcpy(buffer_ix, &len_entities, sizeof(len_entities));
              buffer_ix += sizeof(len_entities);
              memcpy(buffer_ix, &level_w, sizeof(level_w));
              buffer_ix += sizeof(level_w);
              memcpy(buffer_ix, &level_h, sizeof(level_h));
              buffer_ix += sizeof(level_h);

              for (int i = 0; i < len_entities; ++i) {
                Entity* entity = &entities[i];
                memcpy(buffer_ix, entity, sizeof(Entity));
                buffer_ix += sizeof(Entity);

                for (int j = 0; j < entity->len_shapes; ++j) {
                  Shape* shape = &(entity->shapes[j]);
                  memcpy(buffer_ix, shape, sizeof(Shape));
                  buffer_ix += sizeof(Shape);

                  // copy x & y vertices
                  memcpy(buffer_ix, shape->x, shape->len_vertices * sizeof(short));
                  buffer_ix += shape->len_vertices * sizeof(short);
                  memcpy(buffer_ix, shape->y, shape->len_vertices * sizeof(short));
                  buffer_ix += shape->len_vertices * sizeof(short);
                }
              }

              // sanity check
              if (buffer_ix - buffer != num_bytes) {
                printf("%lu - num_bytes\n", num_bytes);
                printf("%lu - pointer diff\n", buffer_ix - buffer);
                error("byte mismatch on file save");
              }
              
              // write the bytes to the file
              fwrite(buffer, num_bytes, 1, level_file); // write bytes to file
              if (ferror(level_file))
                error("writing level file");
              
              fclose(level_file);
              free(buffer);
            }
          }
          else if (evt.key.keysym.sym == SDLK_RETURN) {
            if (selected_shape) {
              selected_shape->len_vertices--;
              selected_shape = NULL;
            }
          }
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
          else if (evt.key.keysym.sym == SDLK_m) {
            mode_type = ENEMY;
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
          if (evt.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            if (player.grav_y > 0.0) {
              up_pressed = true;
            }
            else if (player.grav_y < 0.0) {
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
      player.dx = -move_speed;
    else if (right_pressed)
      player.dx = move_speed;
    else
      player.dx = 0;
    
    // gravity
    if ((player.grav_y > 0 && player.dy < 10) || (player.grav_y < 0 && player.dy > -10))
      player.dy += player.grav_y;

    for (int i = 0; i < len_entities; ++i) {
      Entity* ent = &(entities[i]);
      if (ent->grav_y && ((ent->grav_y > 0 && ent->dy < 10) || (ent->grav_y < 0 && ent->dy > -10)))
        ent->dy += ent->grav_y;
    }

    if (will_collide(&player, REVERSE_GRAV) > -1)
      player.grav_y = -player.grav_y;

    if (will_collide(&player, FINISH) > -1)
      won_game = true;

    if (will_collide(&player, CHECKPOINT) > -1) {
      start_x = player.x;
      start_y = player.y;
      start_grav = player.grav_y;
    }

    int portal_ix = will_collide(&player, PORTAL);
    if (portal_ix > -1) {
      for (int i = 0; i < len_entities; ++i) {
        if (i != portal_ix && entities[i].flags & PORTAL) {
          int delta_x = entities[i].x - entities[portal_ix].x;
          int delta_y = entities[i].y - entities[portal_ix].y;
          player.x += delta_x;
          player.y += delta_y;
          player.dx = -player.dx;
          player.dy = -player.dy;
          break;
        }
      }
    }

    // start over if you hit lava or an enemy or fall offscreen
    if ((will_collide(&player, LAVA) > -1) || (will_collide(&player, ENEMY) > -1) ||
      player.x < 0 || player.x > vp.w || player.y < 0 || player.y > vp.h) {
      player.grav_y = start_grav;
      player.dx = 0;
      player.dy = 0;
      player.x = start_x;
      player.y = start_y;
    }

    // if touching ground, & jump button pressed, jump
    if (up_pressed && collides(player.x, player.y + 1, player.w, player.h, -1, entities, WALL) > -1)
      player.dy = -jump_speed;
    else if (down_pressed && collides(player.x, player.y - 1, player.w, player.h, -1, entities, WALL) > -1)
      player.dy = jump_speed;

    // if it's going to collide (horiz), inch there 1px at a time
    if (collides(player.x + player.dx, player.y, player.w, player.h, -1, entities, WALL) > -1 && player.dx) {
      while (!(collides(player.x + sign(player.dx), player.y, player.w, player.h, -1, entities, WALL) > -1))
        player.x += sign(player.dx);
      
      player.dx = 0;
    }
    player.x += player.dx;

    // if it's going to collid (vert), inch there 1px at a time
    if (collides(player.x, player.y + player.dy, player.w, player.h, -1, entities, WALL) > -1 && player.dy) {
      while (!(collides(player.x, player.y + sign(player.dy), player.w, player.h, -1, entities, WALL) > -1))
        player.y += sign(player.dy);
      
      player.dy = 0;
    }
    player.y += player.dy;

    // if an enemy is going to collide, inch there & *reverse* the direction
    for (int i = 0; i < len_entities; ++i) {
      Entity* ent = &(entities[i]);
      if (ent->dx) {
        if (collides(ent->x + ent->dx, ent->y, ent->w, ent->h, i, entities, WALL) > -1 && ent->dx) {
          while (!(collides(ent->x + sign(ent->dx), ent->y, ent->w, ent->h, i, entities, WALL) > -1))
            ent->x += sign(ent->dx);
          
          ent->dx = -ent->dx;
        }
        else {
          ent->x += ent->dx;
        }
      }

      if (ent->dy) {
        if (collides(ent->x, ent->y + ent->dy, ent->w, ent->h, i, entities, WALL) > -1 && ent->dy) {
          while (!(collides(ent->x, ent->y + sign(ent->dy), ent->w, ent->h, i, entities, WALL) > -1))
            ent->y += sign(ent->dy);
          
          ent->dy = -ent->dy / 8;
        }
        else {
          ent->y += ent->dy;
        }
      }
    }

    // if an enemy goes offscreen, delete it
    // we do this in a separate loop b/c deleteEntity() moves the last entity to earlier in the loop
    // and will cause the loop to skip that last entity
    for (int i = 0; i < len_entities; ++i) {
      Entity* ent = &(entities[i]);
      if (ent->dx && (ent->x + ent->w < 0 || ent->x > vp.w))
        deleteEntity(i);
      else if (ent->dy && (ent->y + ent->h < 0 || ent->y > vp.h))
        deleteEntity(i);
    }

    // set BG color
    if (SDL_SetRenderDrawColor(renderer, 44, 34, 30, 255) < 0)
      error("setting bg color");
    if (SDL_RenderClear(renderer) < 0)
      error("clearing renderer");

    // render polygon
    for (int i = 0; i < len_entities; ++i) {
      Entity* ent = &entities[i];
      Shape* shape = &(ent->shapes[0]);

      short *vx = shape->x;
      short *vy = shape->y;
      if (shape->fill_color_ix != NO_COLOR) {
        aapolygonColor(renderer, ent->x, ent->y, vx, vy, shape->len_vertices, colors[shape->fill_color_ix]);
        filledPolygonColor(renderer, ent->x, ent->y, vx, vy, shape->len_vertices, colors[shape->fill_color_ix]);// 0xFF000000);
      }
      else {
        for (int j = 0; j < shape->len_vertices - 1; ++j)
          aalineColor(renderer, vx[j] + ent->x, vy[j] + ent->y, vx[j + 1] + ent->x, vy[j + 1] + ent->y, colors[shape->stroke_color_ix]);
      }
    }

    // draw palette
    for (int i = 0; i < colors_len; ++i)
      boxColor(renderer, i * palette_color_size, palette_y, i * palette_color_size + palette_color_size, palette_y + palette_h, colors[i]);

    // render player
    if (SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255) < 0)
      error("setting player color");

    SDL_Rect player_rect = {
      .x = player.x - vp.x,
      .y = player.y - vp.y,
      .w = player.w,
      .h = player.h
    };
    if (SDL_RenderFillRect(renderer, &player_rect) < 0)
      error("filling player rect");

    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

  // free dynamically allocated memory
  for (int i = 0; i < len_entities; ++i) {
    Entity* ent = &entities[i];
    for (int j = 0; j < ent->len_shapes; ++j) {
      Shape* shape = &(ent->shapes[j]);
      free(shape->x);
      free(shape->y);
    }
    free(ent->shapes);
  }

  for (int i = 0; i < max_controllers; ++i)
    if (controllers[i])
      SDL_GameControllerClose(controllers[i]);

  if (SDL_SetWindowFullscreen(window, 0) < 0)
    error("exiting fullscreen");

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

Entity* createEntity(byte mode_type, byte color_ix, short x, short y, short w, short h) {
  entities[len_entities].flags = WALL | mode_type;
  entities[len_entities].shapes = (Shape*)calloc(64, sizeof(Shape)),
  entities[len_entities].len_shapes++;
  entities[len_entities].shapes[0].x = (short*)calloc(64, sizeof(short));
  entities[len_entities].shapes[0].y = (short*)calloc(64, sizeof(short));
  entities[len_entities].shapes[0].stroke_color_ix = color_ix;
  entities[len_entities].shapes[0].fill_color_ix = NO_COLOR;
  entities[len_entities].x = x;
  entities[len_entities].y = y;
  entities[len_entities].w = w;
  entities[len_entities].h = h;
  
  len_entities++;

  return &entities[len_entities - 1];
}

// delete by copying the tip entity over the one to remove
void deleteEntity(int entity_ix) {
  Entity* ent = &(entities[entity_ix]);
  // DRY violation: fix
  for (int j = 0; j < ent->len_shapes; ++j) {
    Shape* shape = &(ent->shapes[j]);
    free(shape->x);
    free(shape->y);
  }
  free(ent->shapes);

  entities[entity_ix] = entities[len_entities - 1];
  memset(&entities[len_entities - 1], 0, sizeof(Entity));
  len_entities--;
}

void updateEntityBBox(Entity* ent) {
  Shape* shape = &(ent->shapes[0]);

  // update entity's bounding box by iterating vertices
  short min_x = shape->x[0];
  short min_y = shape->y[0];

  for (int i = 1; i < shape->len_vertices; ++i) {
    if (shape->x[i] < min_x)
      min_x = shape->x[i];
    if (shape->y[i] < min_y)
      min_y = shape->y[i];
  }

  // shape points are relative to the entity bounding box, so the min x/y should be 0,0
  // if the x/y mins are no longer 0, update the points so it is & update the entity the other way, so it doesn't move
  if (min_x) {
    for (int i = 1; i < shape->len_vertices; ++i)
      shape->x[i] -= min_x;
    ent->x += min_x;
  }
  if (min_y) {
    for (int i = 1; i < shape->len_vertices; ++i)
      shape->y[i] -= min_y;
    ent->y += min_y;
  }

  short max_x = shape->x[0];
  short max_y = shape->y[0];

  for (int i = 1; i < shape->len_vertices; ++i) {
    if (shape->x[i] > max_x)
      max_x = shape->x[i];
    if (shape->y[i] > max_y)
      max_y = shape->y[i];
  }

  // the width/height should reflect the max x/y, once points are all relative to the entity
  ent->w = max_x;
  ent->h = max_y;
}

int indexOfEntity(short x, short y, short w, short h) {
  // check if there's already a tile here
  int existing_ent_ix = -1;
  for (int i = 0; i < len_entities; ++i)
    if (entities[i].x == x && entities[i].y == y && entities[i].h == h && entities[i].w == w)
      existing_ent_ix = i;

  return existing_ent_ix;
}

// adds 5 points (4 lines) to create a closed rectangle polygon
void addRectPoints(Shape* shape, short x, short y, short w, short h) {
  addPoint(shape, x, y);
  addPoint(shape, x + w, y);
  addPoint(shape, x + w, y + h);
  addPoint(shape, x, y + h);
  addPoint(shape, x, y);
}

void addPoint(Shape* shape, short x, short y) {
  shape->x[shape->len_vertices] = x;
  shape->y[shape->len_vertices] = y;
  shape->len_vertices++;
}

// fills a shape w/ its stroke color & sets stroke color to none
void fillShape(Shape* shape) {
  shape->fill_color_ix = shape->stroke_color_ix;
  shape->stroke_color_ix = NO_COLOR;
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

int will_collide(Entity* ent, byte type) {
  return collides(ent->x + ent->dx, ent->y + ent->dy, ent->w, ent->h, -1, entities, type);
}

int collides(int x, int y, int w, int h, int ix, Entity entities[], byte type) {
  int x2 = x + w;
  int y2 = y + h;

  for (int i = 0; i < len_entities; ++i) {
    if (i != ix && entities[i].flags & type) {
      int other_x = entities[i].x;
      int other_y = entities[i].y;
      int other_x2 = entities[i].x + entities[i].w;
      int other_y2 = entities[i].y + entities[i].h;

      // DO collide if
      if (x2 > other_x && x < other_x2 &&
        y2 > other_y && y < other_y2)
          return i;
    }
  }
  return -1;
}

int sign(float n) {
  if (n > 0)
    return 1;
  else if (n < 0)
    return -1;
  else
    return 0;
}

void error(char* activity) {
  printf("%s failed: %s\n", activity, strerror(errno));//SDL_GetError());
  SDL_Quit();
  exit(-1);
}
