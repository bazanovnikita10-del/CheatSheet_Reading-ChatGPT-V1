#include "flappyBird.hpp"
#include "Arduino.h"
#include "input.hpp"
#include "display.hpp"
#include "LittleFS.h"

#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define BIRD_SIZE   16

const uint8_t FlappyBitmap [] PROGMEM = {
    0x70, 0x88, 0x08, 0x0c, 0x0a, 0x92, 0x61, 0x01, 0x1d, 0xa3, 0xc1, 0xc1, 0xda, 0xc4, 0xf8, 0xc0,
    0x00, 0x00, 0x03, 0x05, 0x05, 0x08, 0x08, 0x08, 0x09, 0x0b, 0x07, 0x07, 0x07, 0x07, 0x07, 0x03
};

static const char* BEST_SCORE_PATH = "/flappy_best.sys";

#define FLAPPY_FPS 30
#define FRAME_INTERVAL (1000 / FLAPPY_FPS)

#define STATE_START    0
#define STATE_PLAYING  1
#define STATE_GAMEOVER 2

static GyverOLED<SSD1306_128x64, OLED_BUFFER>* oled;

static int current_state;
static int birdX;
static int birdY, gravity;
static int score, bestScore;
static int wallX[2];
static int gapY[2];
#define GAP_SIZE        30
#define WALL_THICKNESS  10

static int loadBestScore() {
    if (!LittleFS.exists(BEST_SCORE_PATH)) {
        File f = LittleFS.open(BEST_SCORE_PATH, "w", true);
        if (f) { f.print("0!"); f.close(); }
        return 0;
    }
    File f = LittleFS.open(BEST_SCORE_PATH, "r");
    if (!f) return 0;
    uint8_t buf[12];
    f.readBytesUntil('!', buf, sizeof(buf) - 1);
    f.close();
    buf[11] = 0;
    return atoi((char*)buf);
}

static void saveBestScore(int val) {
    File f = LittleFS.open(BEST_SCORE_PATH, "w");
    if (!f) return;
    f.print(val);
    f.print('!');
    f.close();
}

static inline void fillRect(int x, int y, int w, int h, uint8_t mode) {
    if (w <= 0 || h <= 0) return;
    oled->rect(x, y, x + w - 1, y + h - 1, mode);
}

static void resetGame() {
    birdY = OLED_HEIGHT / 2;
    gravity = -4;
    score = 0;
    wallX[0] = OLED_WIDTH;
    gapY[0] = OLED_HEIGHT / 2 - GAP_SIZE / 2;
    wallX[1] = OLED_WIDTH + OLED_WIDTH / 2;
    gapY[1] = random(10, OLED_HEIGHT - GAP_SIZE - 10);
}

static void wipeScreen() {
    oled->clear();
    oled->update();
}

void flappyInit(GyverOLED<SSD1306_128x64, OLED_BUFFER>* _oled) {
    oled = _oled;
    bestScore = loadBestScore();
    current_state = STATE_START;
    birdX = OLED_WIDTH / 4;
}

void flappyBirdGame(void) {
    resetGame();
    current_state = STATE_PLAYING;

    while (1) {
        yield();

        Input input = GetInput();

        if (input.Left == ButtonState::Down) {
            return;
        }

        if (current_state == STATE_PLAYING) {
            if (input.Center == ButtonState::Down || input.Center == ButtonState::Hold) {
                gravity = -4;
            }

            static uint32_t frameTimer = millis();
            if (millis() - frameTimer >= FRAME_INTERVAL) {
                frameTimer = millis();

                oled->clear();

                gravity++;
                birdY += gravity;
                birdY = constrain(birdY, 0, OLED_HEIGHT - BIRD_SIZE);
                if (birdY == OLED_HEIGHT - BIRD_SIZE) gravity = -2;

                oled->drawBitmap(birdX, birdY, FlappyBitmap, BIRD_SIZE, BIRD_SIZE);

                for (int i = 0; i < 2; i++) {
                    fillRect(wallX[i], 0, WALL_THICKNESS, gapY[i], OLED_FILL);
                    fillRect(wallX[i], gapY[i] + GAP_SIZE, WALL_THICKNESS,
                             OLED_HEIGHT - (gapY[i] + GAP_SIZE), OLED_FILL);

                    wallX[i] -= 4;

                    if (wallX[i] < -WALL_THICKNESS) {
                        wallX[i] = OLED_WIDTH;
                        gapY[i] = random(10, OLED_HEIGHT - GAP_SIZE - 10);
                    }

                    if (wallX[i] == birdX) {
                        score++;
                        if (score > bestScore) {
                            bestScore = score;
                            saveBestScore(bestScore);
                        }
                    }

                    if ((birdX + BIRD_SIZE > wallX[i] && birdX < wallX[i] + WALL_THICKNESS)
                        && (birdY < gapY[i] || birdY + BIRD_SIZE > gapY[i] + GAP_SIZE)) {
                        current_state = STATE_GAMEOVER;
                    }
                }

                oled->setCursor(OLED_WIDTH / 2 - 6, 0);
                oled->print(score);
                oled->update();
            }
        } else if (current_state == STATE_GAMEOVER) {
            wipeScreen();

            oled->setCursor(16, 1);
            oled->print(F("ИГРА ОКОНЧЕНА"));

            oled->setCursor(0, 3);
            oled->print(F("ОЧКИ:"));
            oled->setCursor(60, 3);
            oled->print(score);

            oled->setCursor(0, 5);
            oled->print(F("ЛУЧШИЙ РЕЗУЛЬТАТ:"));
            oled->setCursor(60, 6);
            oled->print(bestScore);

            oled->update();

            while (input.Center == ButtonState::Down || input.Center == ButtonState::Hold) {
                yield();
                input = GetInput();
                if (input.Left == ButtonState::Down) return;
            }
            while (input.Center == ButtonState::None) {
                yield();
                input = GetInput();
                if (input.Left == ButtonState::Down) return;
            }

            resetGame();
            current_state = STATE_PLAYING;
        }
    }
}