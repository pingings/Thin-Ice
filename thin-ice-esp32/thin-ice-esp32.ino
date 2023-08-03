#include "sprites.h"
#include "levels.h"
#include <TFT_eSPI.h> 
TFT_eSPI    tft = TFT_eSPI();         // Declare object "tft"

#define BLACK         0x0000
#define WHITE         0x217A5AD
#define LIGHT_BLUE    0xDF9F
#define DARK_BLUE     0x0339

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

// game flags
int puffle_available = 0;
int level_passed = 0;

// for when the gameplay numbers are converted to strings so they can be displayed
char lvl_num_str[4];
char tiles_melted_str[4];
char tiles_total_str[4];
char solved_str[4];
char points_str[4];

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

/***************************************************************************************
***************************************************************************************/

void disp_write(uint16_t x, uint16_t y, char text[]) {
  tft.setCursor(x,y);
  tft.println(text);
}

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
  disp_write(LVL_NUM_X, TEXT_PADDING, itoa(lvl_num, lvl_num_str, 10));
  disp_write(TILES_MELTED_X, TEXT_PADDING, itoa(tiles_melted, tiles_melted_str, 10));
  disp_write(TILES_TOTAL_X, TEXT_PADDING, itoa(tiles_total, tiles_total_str, 10));
  disp_write(SOLVED_X, TEXT_PADDING, itoa(solved, solved_str, 10));
  disp_write(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, itoa(points, points_str, 10));

  
}

void incr_tiles_melted() {
    
  tft.setTextColor(LIGHT_BLUE);
  disp_write(TILES_MELTED_X, TEXT_PADDING, itoa(tiles_melted, tiles_melted_str, 10));
  tiles_melted++;
  tft.setTextColor(TFT_BLUE);
  disp_write(TILES_MELTED_X, TEXT_PADDING, itoa(tiles_melted, tiles_melted_str, 10));
}

void incr_level_counter() {

  tft.setTextColor(LIGHT_BLUE);
  disp_write(LVL_NUM_X, TEXT_PADDING, itoa(lvl_num, lvl_num_str, 10));
  lvl_num++;
  tft.setTextColor(TFT_BLUE);
  disp_write(LVL_NUM_X, TEXT_PADDING, itoa(lvl_num, lvl_num_str, 10));
}

void set_tiles_total(int tiles) {

  tft.setTextColor(LIGHT_BLUE);
  disp_write(TILES_TOTAL_X, TEXT_PADDING, itoa(tiles_total, tiles_total_str, 10));
  tiles_total = tiles;
  tft.setTextColor(TFT_BLUE);  
  disp_write(TILES_TOTAL_X, TEXT_PADDING, itoa(tiles_total, tiles_total_str, 10));
}

void add_points(int to_add) {

  tft.setTextColor(LIGHT_BLUE);
  disp_write(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, itoa(points, points_str, 10));
  points += to_add;
  tft.setTextColor(TFT_BLUE);
  disp_write(POINTS_X, DISP_HEIGHT-TEXT_HEIGHT-TEXT_PADDING, itoa(points, points_str, 10));

}

void incr_solved() {

  tft.setTextColor(LIGHT_BLUE);
  disp_write(SOLVED_X, TEXT_PADDING, itoa(solved, solved_str, 10));
  solved++;
  tft.setTextColor(TFT_BLUE);
  disp_write(SOLVED_X, TEXT_PADDING, itoa(solved, solved_str, 10));
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
        tiles++;
      }
    }
  }

  // set/reset the game text
  set_tiles_total(tiles);
  incr_level_counter(); 

  // place the puffle
  spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET, TFT_GREEN);

}

void move_down() {

  if (puffle_available) { // e.g. if user is holding down buttons while the puffle is moving, just ignore it

    puffle_available = 0;

    int block_1_x = puffle_x, block_1_y = puffle_y;
    int block_2_x = puffle_x, block_2_y = puffle_y+1;
    int target_y = puffle_y + 1;

    // 1 is id of border block - can't move onto this block
    if (lvl_map[block_2_y][block_2_x] == 1) { return; } 

    incr_tiles_melted();
    add_points(1);
    int ice_break_stage = 0;
    
    for (int slide=0; slide<25; slide+=2) {
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)water_24x24);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET);
      spr_water.pushImage(0, 0, 24, 24, (uint16_t *)ice_break_stages[slide/8]);
      spr_water.pushSprite(block_1_x*24, (block_1_y*24)+BARS_OFFSET, TFT_BLACK);

      spr_intl.pushSprite(block_2_x*24, (block_2_y*24)+BARS_OFFSET);
      spr_puffle.pushSprite(puffle_x*24, (puffle_y*24)+BARS_OFFSET+slide, TFT_GREEN);
      //Serial.println("moved puffle to x y"); Serial.println(puffle_x); Serial.println(puffle_y);
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

  }

}

void loop(void) {

  // load level 1
  setup_sprites(sprite_key, key_length);
  load_level(lvl_1);
  level_passed = 0;
  puffle_available = 1;
  
  while (level_passed != 1) {
    btn_down_pressed = digitalRead(btn_down_pin);
    if (btn_down_pressed) { move_down(); }
  }

}