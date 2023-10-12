#include "sprites.h"
#include "levels.h"
#include "water.h"
#include <TFT_eSPI.h> 
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>      

#define BLACK         0x0000
#define WHITE         0x217A5AD
#define LIGHT_BLUE    0xDF9F
#define DARK_BLUE     0x0339

TaskHandle_t animation_tasks_handle;

// pins for arrow buttons
const int btn_up_pin = 21; 
const int btn_down_pin = 32;
const int btn_left_pin = 22;
const int btn_right_pin = 5;

// flags for arrow buttons
int btn_up_pressed = 0;
int btn_down_pressed = 0;
int btn_left_pressed = 0;
int btn_right_pressed = 0;

// some measurements used for displaying elements
const int ROWS = 11;
const int COLS = 20;
const int DISP_WIDTH = 480;
const int DISP_HEIGHT = 320;
const int BARS_OFFSET = 28; // height of the bars
const int TEXT_PADDING = 7;  // dist of text from top/bottom of display
const int TEXT_HEIGHT = 15; // extra padding on bottom bar text (actual text height is 7px but this isn't enough?)

// these numbers will be displayed
int lvl_num = 0;
int tiles_melted = 0;
int tiles_total = 0;
int solved = 0;
int points = 0;

// game vars
int water_head_x; // both set to the puffle's position at the start of each level, then updated each time he moves
int water_head_y;
int puffle_x = 0; // both updated on each full tile movement
int puffle_y = 0;

// game and animation flags/vars
int puffle_available = 0;
int target_tile_x = 0;
int target_tile_y = 0;
int level_passed = 0;
int slide_val = 0;
char slide_dir = 'N';

// map tile ids
const int ID_WATER = 10;
const int ID_INTL = 0;
const int ID_BORDER = 1;
const int ID_OOB = 2;
const int ID_RED = 3;

// display refresh things
int DISP_REFRESH_RATE = 10;
int puffle_anim_rate = 100;
int puffle_anim_counter = 0;
int puffle_speed = 80;
int puffle_speed_counter = 0;
const int MELTING_TILE_LIFETIME = 400; // ms a tile takes to melt
const int WAVE_PERIOD = 1500; // ms for water to go thru all its frames

// for when the gameplay numbers are converted to strings so they can be displayed
char temp_str[4];

// where to put text when it gets updated
int LVL_NUM_X = 105;
int TILES_MELTED_X = 210;
int TILES_TOTAL_X = 255;
int SOLVED_X = 460;
int POINTS_X = 440;

TFT_eSPI    tft = TFT_eSPI();  
TFT_eSprite spr_oob = TFT_eSprite(&tft);
TFT_eSprite spr_intl = TFT_eSprite(&tft);
TFT_eSprite spr_red = TFT_eSprite(&tft);
TFT_eSprite spr_border = TFT_eSprite(&tft);
TFT_eSprite spr_puffle = TFT_eSprite(&tft);
TFT_eSprite spr_water = TFT_eSprite(&tft);
TFT_eSprite spr_current = TFT_eSprite(&tft);
TFT_eSprite spr_single = TFT_eSprite(&tft);
TFT_eSprite spr_double_x = TFT_eSprite(&tft);
TFT_eSprite spr_double_y = TFT_eSprite(&tft);
TFT_eSprite spr_ice = TFT_eSprite(&tft);

uint8_t key_length = 4;
TFT_eSprite *sprite_key[] = { // only for the sprites that can appear on the map arrays
  &spr_oob,     // 0
  &spr_border,  // 1 needs to be 1 for boundary checking on movement
  &spr_intl,    // 2
  &spr_red,     // 3
  &spr_intl,    // 4 puffle starting block
};

const uint16_t* ice_break_stages[6] = {
  ice_break_1_24x24,
  ice_break_2_24x24,
  ice_break_3_24x24,
  ice_break_4_24x24,
  ice_break_5_24x24,
  ice_break_6_24x24,
};

const uint16_t* puffle_frames[3] = {
  puffle_1_24x24,
  puffle_2_24x24,
  puffle_3_24x24,
};
int puffle_frame = 0;

// ALL lcd changed are handled by the animation tasks thread, otherwise it crashes
// TODO do this better
struct stat_updates {
  int add_points;
  bool incr_melted;
  bool incr_lvl;
  bool incr_solved;
  int set_total_tiles;
};
struct stat_updates updates = {0,0,0,0,0};

/***************************************************************************************
Water animation frames
***************************************************************************************/

const uint16_t* wave_stages[10] = {
  water_1_24x24, water_3_24x24, water_5_24x24, 
  water_7_24x24, water_9_24x24, water_11_24x24, 
  water_13_24x24, water_15_24x24, water_17_24x24, 
  water_19_24x24
};

/***************************************************************************************
Struct which will be used for keeping track of the current level's tiles
***************************************************************************************/

struct map_tile {
  int id;      // tile type id. if not water id then water_lifetime is irrelevant.
  bool has_ice;
  int water_lifetime; // default should be 0
  int ice_lifetime; // default should be 0
  int next_x; // default should be -1
  int next_y; // default should be -1
};
struct map_tile active_map[ROWS][COLS];

/***************************************************************************************
Functions for updating the numbers  
***************************************************************************************/

void disp_write(uint16_t x, uint16_t y, char text[]) {
  tft.setCursor(x,y);
  tft.println(text);
}

void update_display(int x, int y, int *var, int new_val) {
  tft.setTextColor(LIGHT_BLUE);
  disp_write(x, y, itoa(*var, temp_str, 10));
  *var = new_val;
  tft.setTextColor(TFT_BLUE);
  disp_write(x, y, itoa(*var, temp_str, 10));
}

/***************************************************************************************
Function for handling animation
On each refresh:
- the puffle frame always changes
- the puffle's current tile is always changed
- the puffle might be moving
- some ice blocks might be breaking
- some water, keys etc might be animated
***************************************************************************************/

void animation_tasks(void *parameter) {

  while (1) {

    puffle_anim_counter += DISP_REFRESH_RATE;
    //puffle_speed_counter += DISP_REFRESH_RATE;

    // queue the image for the tile that the puffle is currently stood on or moving from
    spr_current.setSwapBytes(true);
    spr_current.pushImage(0, 0, 24, 24, (uint16_t *)intl_block_24x24);
    spr_current.setSwapBytes(false);
    
    // overlay the puffle on the current tile, with offset if he's moving
    switch (slide_dir) {
      case 'N': spr_puffle.pushToSprite(&spr_current, 0, slide_val, TFT_GREEN); break;
      case 'S': spr_puffle.pushToSprite(&spr_current, 0, -slide_val, TFT_GREEN); break;
      case 'E': spr_puffle.pushToSprite(&spr_current, -slide_val, 0, TFT_GREEN); break;
      case 'W': spr_puffle.pushToSprite(&spr_current, slide_val, 0, TFT_GREEN); break;
    }
    
    // push this current tile
    spr_current.pushSprite(target_tile_x*24, (target_tile_y*24)+BARS_OFFSET);

    /***************************************************************************************
    Handle all water tiles - all have a wave animation, potentially some breaking ice,
    and potentially the puffle
    ***************************************************************************************/

    // starting with the most recent water tile,
    int curr_x = water_head_x;
    int curr_y = water_head_y;
    struct map_tile *curr_tile;
    float frame; // used for water (1-20) and then ice (1-6)
    int frame_int;

    while (curr_x != -1) {

      curr_tile = &(active_map[curr_y][curr_x]);

      // check if water - will only be false if puffle is still on the starting tile?
      if (curr_tile->id == ID_WATER) {
        curr_tile->water_lifetime %= WAVE_PERIOD;
        frame = ( (float)curr_tile->water_lifetime / (float)WAVE_PERIOD ) * 10; // a number from 1 to 20
        if ((int)(frame-DISP_REFRESH_RATE) != (int)frame) {
          frame_int = (int) frame;
          spr_current.setSwapBytes(true);
          spr_current.pushImage(0, 0, 24, 24, (uint16_t*)wave_stages[frame_int]);
          spr_current.setSwapBytes(false);
          curr_tile->water_lifetime += DISP_REFRESH_RATE;
        }
      }

      // check if ice is melting on this tile
      if (curr_tile->has_ice) {
        frame_int = ( (float)curr_tile->ice_lifetime / (float)MELTING_TILE_LIFETIME )  * 6;
        Serial.println(frame);
        spr_ice.pushImage(0, 0, 24, 24, (uint16_t*)ice_break_stages[frame_int]);
        spr_ice.pushToSprite(&spr_current, 0, 0, TFT_BLACK);
        curr_tile->ice_lifetime += DISP_REFRESH_RATE;

        // check if has_ice should now be switched off
        if (curr_tile->ice_lifetime > MELTING_TILE_LIFETIME) { curr_tile->has_ice = false; }

      }

      // check if puffle is on this tile and is moving 
      // why shouldnt we push if he's not moving? i forgot?
      if ( (curr_x == puffle_x) && (curr_y == puffle_y) && (slide_val > 0) ) {
        switch (slide_dir) {
            case 'N': spr_puffle.pushToSprite(&spr_current, 0, slide_val-24, TFT_GREEN); break;
            case 'S': spr_puffle.pushToSprite(&spr_current, 0, 24-slide_val, TFT_GREEN); break;
            case 'E': spr_puffle.pushToSprite(&spr_current, 24-slide_val, 0, TFT_GREEN); break;
            case 'W': spr_puffle.pushToSprite(&spr_current, slide_val-24, 0, TFT_GREEN); break;
          }
      }

      // finally, push the tile
      spr_current.pushSprite(curr_x*24, curr_y*24+BARS_OFFSET);

      // next water tile
      curr_x = curr_tile->next_x;
      curr_y = curr_tile->next_y;
    
    }

    /***************************************************************************************
    Decrement slide_val, and update the puffle's frame,  if it's time
    ***************************************************************************************/

    //if (puffle_speed_counter >= puffle_speed) {

      if (slide_val > 0) {
        slide_val -= 2;
      } else if (slide_val < 0) {
        slide_val = 0;        
      } else {

        // if there's no current movement, but the puffle position isn't the target position,
        // then it has only just reached its target, so we need to update the puffle's position
        if (puffle_x != target_tile_x) {puffle_x = target_tile_x;}
        if (puffle_y != target_tile_y) {puffle_y = target_tile_y;}
        if (!puffle_available) { puffle_available = 1; }
      }
      
      // load the puffle's next frame if it's time
      if (puffle_anim_counter >= puffle_anim_rate) {
        puffle_frame = (puffle_frame+1) % 3;
        spr_puffle.pushImage(0, 0, 24, 24, (uint16_t *)puffle_frames[puffle_frame]);
        puffle_anim_counter = 0;
      
      }

    //  puffle_speed_counter = 0;

    //}

    /***************************************************************************************
    Stats changes
    ***************************************************************************************/

    if (updates.add_points > 0) { 
      update_display(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, &points, points+updates.add_points);
      updates.add_points = 0;
    }
    if (updates.incr_melted) {
      update_display(TILES_MELTED_X, TEXT_PADDING, &tiles_melted, tiles_melted+1);
      updates.incr_melted = 0;
    }
    if (updates.incr_lvl) {
      update_display(LVL_NUM_X, TEXT_PADDING, &lvl_num, lvl_num+1);
      updates.incr_lvl = 0;
    }
    if (updates.incr_solved) {
      update_display(SOLVED_X, TEXT_PADDING, &solved, solved+1);
      updates.incr_solved = 0;
    }
    if (updates.set_total_tiles != 0) {
      update_display(TILES_TOTAL_X, TEXT_PADDING, &tiles_total, updates.set_total_tiles);
      updates.set_total_tiles = 0;
    }

    // finally, wait
    vTaskDelay(pdMS_TO_TICKS(DISP_REFRESH_RATE));

  }
}

/***************************************************************************************
Functions for handling movement
***************************************************************************************/

void up_pressed() {
  bool success = arrow_pressed(puffle_x, puffle_y - 1);
  if (success) { slide_dir = 'N'; slide_val = 25; }
}
void down_pressed() {
  bool success = arrow_pressed(puffle_x, puffle_y + 1);
  if (success) { slide_dir = 'S'; slide_val = 25; }
}
void left_pressed() {
  bool success = arrow_pressed(puffle_x - 1, puffle_y);
  if (success) { slide_dir = 'W'; slide_val = 25; }
}
void right_pressed() {
  bool success = arrow_pressed(puffle_x + 1, puffle_y);
  if (success) { slide_dir = 'E'; slide_val = 25; }
}

bool arrow_pressed(int new_x, int new_y) {

  if (puffle_available && (active_map[new_y][new_x].id != ID_BORDER) && (active_map[new_y][new_x].id != ID_WATER)) {

    puffle_available = 0;

    // the animation task will now constantly rewrite this tile under the puffle
    target_tile_x = new_x;
    target_tile_y = new_y;

    // the new tile will point to the current water head, and then will become the current water head
    active_map[new_y][new_x].next_x = water_head_x;
    active_map[new_y][new_x].next_y = water_head_y;
    water_head_x = new_x;
    water_head_y = new_y;

    // the tile that the puffle is moving off is will now become water
    active_map[puffle_y][puffle_x].id = ID_WATER;
    active_map[puffle_y][puffle_x].has_ice = true;

    // game var stuff - anim task will handle this
    updates.incr_melted = 1;
    updates.add_points = 1;

    return 1;

  } else {
    return 0;
  }

}

/***************************************************************************************
Functions for setup
***************************************************************************************/

void setup() {

  Serial.begin(230400);
  Serial.println();
  delay(50);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // arrow buttons
  pinMode(btn_up_pin, INPUT);
  pinMode(btn_down_pin, INPUT);
  pinMode(btn_left_pin, INPUT);
  pinMode(btn_right_pin, INPUT);

  // bars and font setup
  tft.fillRect(0,0,DISP_WIDTH,BARS_OFFSET,LIGHT_BLUE); // top bar
  tft.fillRect(0,DISP_HEIGHT-BARS_OFFSET,DISP_WIDTH,BARS_OFFSET,LIGHT_BLUE); // bottom bar
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLUE);

  // write the permanent text
  disp_write(30, TEXT_PADDING, "LEVEL");
  disp_write(235, TEXT_PADDING, "/");
  disp_write(360, TEXT_PADDING, "SOLVED");
  disp_write(340, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, "POINTS"); 

  // write the var text
  disp_write(LVL_NUM_X, TEXT_PADDING, itoa(lvl_num, temp_str, 10));
  disp_write(TILES_MELTED_X, TEXT_PADDING, itoa(tiles_melted, temp_str, 10));
  disp_write(TILES_TOTAL_X, TEXT_PADDING, itoa(tiles_total, temp_str, 10));
  disp_write(SOLVED_X, TEXT_PADDING, itoa(solved, temp_str, 10));
  disp_write(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, itoa(points, temp_str, 10));

  xTaskCreate(animation_tasks, "animation_tasks", 8192, NULL, 1, &animation_tasks_handle);
  vTaskSuspend(animation_tasks_handle);

}

// TODO sort all this out!!
void setup_sprites(TFT_eSprite *sprites[], size_t num_sprites) {
  for (size_t i=0; i<num_sprites; i++) {
    sprites[i]->setColorDepth(16);
    sprites[i]->createSprite(24, 24);
    sprites[i]->setSwapBytes(true);
  }

  spr_puffle.setColorDepth(16);
  spr_puffle.createSprite(24, 24);
  spr_puffle.pushImage(0, 0, 24, 24, (uint16_t *)puffle_1_24x24);
  spr_puffle.setSwapBytes(true);

  spr_water.setColorDepth(16);
  spr_water.createSprite(24, 24);
  spr_water.setSwapBytes(true);
  spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);

  spr_current.setColorDepth(16);
  spr_current.createSprite(24, 24);
  spr_current.setSwapBytes(true);
  spr_current.pushImage(0, 0, 24, 24, (uint16_t *)intl_block_24x24);

  spr_ice.setColorDepth(16);
  spr_ice.createSprite(24, 24);
  spr_ice.setSwapBytes(true);

  spr_single.setColorDepth(16);
  spr_single.createSprite(24, 24);

  spr_double_x.setColorDepth(16);
  spr_double_x.createSprite(24, 48);

  spr_oob.pushImage(0, 0, 24, 24, (uint16_t *)oob_block_24x24);
  spr_border.pushImage(0, 0, 24, 24, (uint16_t *)border_block_24x24);
  spr_intl.pushImage(0, 0, 24, 24, (uint16_t *)intl_block_24x24);
  spr_red.pushImage(0, 0, 24, 24, (uint16_t *)red_block_24x24);


  
}

/***************************************************************************************
Other functions and main loop
***************************************************************************************/

// given a level number, 
// TODO make this actually a pointer again
void load_level(uint8_t lvl[][COLS]) {

  // load the tiles onto the screen, while counting the meltable tiles
  int tiles = 0;
  for (int row=0; row<ROWS; row++) {
    for (int col=0; col<COLS; col++) {

      int block_id = lvl[row][col];

      // "reset" each active_map tile
      active_map[row][col].id = block_id;
      active_map[row][col].has_ice = false;
      active_map[row][col].water_lifetime = 0;
      active_map[row][col].ice_lifetime = 0;
      active_map[row][col].next_x = -1;
      active_map[row][col].next_y = -1;

      sprite_key[block_id]->pushSprite(col*24, row*24+BARS_OFFSET);
      if (block_id == 2) { 
        tiles++; 
      } else if (block_id == 4) { 
        puffle_x = col; puffle_y = row; // puffle will be spawned here
        water_head_x = col; water_head_y = row; // this will be our first water tile
        target_tile_x = puffle_x; target_tile_y = puffle_y; // bc the puffle isn't moving yet
        tiles++;
      }
    }
  }

  // set/reset the game text
  updates.incr_lvl = 1;
  updates.set_total_tiles = tiles;

  // place the puffle
  spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET, TFT_GREEN);

}

void loop(void) {
  delay(1000); // crashes without the delay! previously it's had to be immediately before vTaskResume

  // load level 1
  setup_sprites(sprite_key, key_length);
  load_level(lvl_1);
  level_passed = 0;

  
  vTaskResume(animation_tasks_handle);

  puffle_available = 1;
  
  while (level_passed != 1) {

    btn_up_pressed = digitalRead(btn_up_pin);
    if (btn_up_pressed) { up_pressed(); }

    btn_down_pressed = digitalRead(btn_down_pin);
    if (btn_down_pressed) { down_pressed(); }

    btn_left_pressed = digitalRead(btn_left_pin);
    if (btn_left_pressed) { left_pressed(); }

    btn_right_pressed = digitalRead(btn_right_pin);
    if (btn_right_pressed) { right_pressed(); }
  }

}