#include "sprites.h"
#include "levels.h"
#include <TFT_eSPI.h> 
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
TFT_eSPI    tft = TFT_eSPI();         // Declare object "tft"

#define BLACK         0x0000
#define WHITE         0x217A5AD
#define LIGHT_BLUE    0xDF9F
#define DARK_BLUE     0x0339

TaskHandle_t animation_tasks_handle;

// pins for arrow buttons
//const int btn_up_pin = 21;
const int btn_down_pin = 21;
const int btn_left_pin = 22;
//const int btn_right_pin = 21;

// flags for arrow buttons
int btn_down_pressed = 0;

// some measurements used for displaying elements
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
int puffle_x = 0;
int puffle_y = 0;
uint8_t (*lvl_map)[20]; // use to refer back to and redraw the tiles when the puffle moves

// game and animation flags/vars
int puffle_available = 0;
int target_tile_x = 0;
int target_tile_y = 0;
int level_passed = 0;
int slide_x = 0;
int slide_y = 0;

// display refresh things
int DISP_REFRESH_RATE = 20;
int puffle_anim_rate = 100;
int puffle_anim_counter = 0;
int puffle_speed = 80;
int puffle_speed_counter = 0;
const int MELTING_TILE_LIFETIME = 400; // overall time a tile takes to melt

// for when the gameplay numbers are converted to strings so they can be displayed
char temp_str[4];

// where to put text when it gets updated
int LVL_NUM_X = 105;
int TILES_MELTED_X = 210;
int TILES_TOTAL_X = 255;
int SOLVED_X = 460;
int POINTS_X = 460;

TFT_eSprite spr_oob = TFT_eSprite(&tft);
TFT_eSprite spr_intl = TFT_eSprite(&tft);
TFT_eSprite spr_red = TFT_eSprite(&tft);
TFT_eSprite spr_border = TFT_eSprite(&tft);
TFT_eSprite spr_puffle = TFT_eSprite(&tft);
TFT_eSprite spr_water = TFT_eSprite(&tft);

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


struct melting_tile {
  int x;
  int y;
  int lifetime;
  bool active;
};
const int MAX_MELTING_TILES = 2;
struct melting_tile melting_tiles[MAX_MELTING_TILES] = {
  {0,0,0,0},
  {0,0,0,0},
};
void new_melting_tile(int new_x, int new_y) {
  for (int i=0; i<MAX_MELTING_TILES; i++) {
    if (melting_tiles[i].active == 0)  {
      melting_tiles[i].x = new_x;
      melting_tiles[i].y = new_y;
      melting_tiles[i].lifetime = 0;
      melting_tiles[i].active = 1;
      break; // IMPORTANT in case they are both 0!
    }
  }
}

// ALL lcd changed are handled by the animation tasks thread, otherwise it crashes
struct stat_updates {
  int add_points;
  bool incr_melted;
  bool incr_lvl;
  bool incr_solved;
  int set_total_tiles;
};
struct stat_updates updates = {0,0,0,0,0};

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
    puffle_speed_counter += DISP_REFRESH_RATE;

    // refresh the target tile to overwrite the puffle's current frame
    // if the puffle is stationary then this is the tile it's stood on
    // if the puffle is moving then this is the tile it's moving onto
    spr_intl.pushSprite(target_tile_x*24, (target_tile_y*24)+BARS_OFFSET);

    // deal with any melting tiles
    for (int i=0; i<MAX_MELTING_TILES; i++) {
      if (melting_tiles[i].active = 1) {

        // active tile is still melting
        if (melting_tiles[i].lifetime < MELTING_TILE_LIFETIME)  {

          // decide where in the melting process it is,
          int stage = ((float)melting_tiles[i].lifetime/(float)MELTING_TILE_LIFETIME) * 6;

          // load the correct frame and push it (over the water), increase lifetime
          spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
          spr_water.pushSprite(melting_tiles[i].x*24, (melting_tiles[i].y*24)+BARS_OFFSET);
          spr_water.pushImage(0, 0, 24, 24, (uint16_t *)ice_break_stages[stage]);
          spr_water.pushSprite(melting_tiles[i].x*24, (melting_tiles[i].y*24)+BARS_OFFSET, TFT_BLACK);
          melting_tiles[i].lifetime += DISP_REFRESH_RATE;
          
        // active tile has actually finished melting
        } else {
          spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
          spr_water.pushSprite(melting_tiles[i].x*24, (melting_tiles[i].y*24)+BARS_OFFSET);
          melting_tiles[i].active = 0;
        }
      }
    }

    // load the puffle's next frame if it's time
    if (puffle_anim_counter >= puffle_anim_rate) {
      puffle_frame = (puffle_frame+1) % 3;
      spr_puffle.pushImage(0, 0, 24, 24, (uint16_t *)puffle_frames[puffle_frame]);
      puffle_anim_counter = 0;
    }


    // place the puffle, factoring in any movement, if it's time
    // might just need 3 instead of 5 clauses
    //if (puffle_speed_counter >= puffle_speed) {

      // moving left
      if (slide_x < 0) {}
      // moving right
      else if (slide_x > 0) {}
      // moving up
      else if (slide_y < 0) {}
      // moving down
      else if (slide_y > 0) { 
        spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET+(25-slide_y), TFT_GREEN); 
        slide_y -= 1;
      }

      else {

        // if there's no current movement, but the puffle position isn't the target position,
        // then it has only just reached its target, so we need to update the puffle's position
        if (puffle_x != target_tile_x) {puffle_x = target_tile_x;}
        if (puffle_y != target_tile_y) {puffle_y = target_tile_y;}
        if (!puffle_available) { puffle_available = 1; }
        spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET, TFT_GREEN); 
        
      }

    //  puffle_speed_counter = 0;

    //}


    // perform any stats changes
    if (updates.add_points > 0) { 
      update_display(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, &points, updates.add_points);
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

void down_pressed() {

  if (puffle_available) { // e.g. if user is holding down buttons while the puffle is moving, just ignore it

    // check the propsed new tile - 1 is id of border block - can't move onto this block
    if (lvl_map[puffle_y+1][puffle_x] == 1) { return; } 

    // don't process further arrow key presses for now
    // (the animation task will set puffle_available = 1 when the slide_y is 0 again)
    puffle_available = 0;

    // the animation task will now constantly rewrite this tile under the puffle
    target_tile_x = puffle_x;
    target_tile_y = puffle_y + 1;

    // the animation task will now handle this tile melting
    new_melting_tile(puffle_x, puffle_y);
    Serial.println("new melting tile added"); Serial.println(puffle_x); Serial.println(puffle_y);

    // the animation task will now handle the puffle gradually moving down
    slide_y = 25;

    // game var stuff - anim task will handle this
    updates.incr_melted = 1;
    updates.add_points = 1;

    /*

    int block_1_x = puffle_x, block_1_y = puffle_y;
    int block_2_x = puffle_x, block_2_y = puffle_y+1;
    int target_y = puffle_y + 1;


    int ice_break_stage = 0;
    
    for (int slide=0; slide<25; slide+=2) {
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET);
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)ice_break_stages[slide/8]);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET, TFT_BLACK);

      spr_intl.pushSprite(block_2_x*24, (block_2_y*24)+BARS_OFFSET);
      spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET+slide, TFT_GREEN);
      delay(10);
    }

    puffle_y += 1;
    puffle_available = 1;

    // finish breaking the tile into water - stages 4,5,6
    // i havent separated the ice breaking and the puffle into different functions bc they 
    // both need to relatively happen at precise times to avoid flickering 
    for (int i=3; i<6; i++) {
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET);
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)ice_break_stages[i]);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET, TFT_BLACK);
      delay(60);
    }
    spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
    spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET);

    */

  }

}



/***************************************************************************************
Functions for setup
***************************************************************************************/


void setup() {

  Serial.begin(9600);
  Serial.println();
  delay(50);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // arrow buttons
  pinMode(btn_down_pin, INPUT);

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
  spr_puffle.setSwapBytes(true);
  spr_puffle.pushImage(0, 0, 24, 24, (uint16_t *)puffle_1_24x24);

  spr_water.setColorDepth(16);
  spr_water.createSprite(24, 24);
  spr_water.setSwapBytes(true);
  spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);

  spr_oob.pushImage(0, 0, 24, 24, (uint16_t *)oob_block_24x24);
  spr_border.pushImage(0, 0, 24, 24, (uint16_t *)border_block_24x24);
  spr_intl.pushImage(0, 0, 24, 24, (uint16_t *)intl_block_24x24);
  spr_red.pushImage(0, 0, 24, 24, (uint16_t *)red_block_24x24);
}


// given a level number, 
// TODO make this actually a pointer again
void load_level(uint8_t lvl[][20]) {

  // get the corresponding level map from the list of pointers to levels
  lvl_map = lvl;
  
  // load the tiles onto the screen, while counting the meltable tiles
  int tiles = 0;
  for (int row=0; row<11; row++) {
    for (int col=0; col<20; col++) {
      int block_id = lvl[row][col];
      sprite_key[block_id]->pushSprite(col*24, (row*24)+BARS_OFFSET);
      if (block_id == 2) { tiles++; }
      if (block_id == 4) { 
        puffle_x = col; puffle_y = row; 
        target_tile_x = puffle_x; target_tile_y = puffle_y;
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
    btn_down_pressed = digitalRead(btn_down_pin);
    if (btn_down_pressed) { down_pressed(); }
  }

}