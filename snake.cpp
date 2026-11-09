#include "snake.hpp"
#include "Arduino.h"
#include "input.hpp"
#include "display.hpp"
#include "LittleFS.h"

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#define CELL        4
#define GRID_W      (OLED_WIDTH / CELL)
#define GRID_H      (OLED_HEIGHT / CELL)
#define MAX_SNAKE   128

static const char* BEST_SCORE_PATH = "/snake_best.sys";

#define SNAKE_FPS        10
#define FRAME_INTERVAL   (1000 / SNAKE_FPS)

#define STATE_START    0
#define STATE_PLAYING  1
#define STATE_GAMEOVER 2

static GyverOLED<SSD1306_128x64, OLED_BUFFER>* oled;

static int8_t snakeX[MAX_SNAKE];
static int8_t snakeY[MAX_SNAKE];
static int snakeLen;
static int8_t dirX, dirY;
static int8_t nextDirX, nextDirY;
static int8_t foodX, foodY;
static int score, bestScore;
static int current_state;

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

static inline void fillCell(int cx, int cy, uint8_t mode) {
    int x = cx * CELL;
    int y = cy * CELL;
    oled->rect(x, y, x + CELL - 1, y + CELL - 1, mode);
}

static inline void drawBorder() {
    oled->rect(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1, OLED_STROKE);
}

static void spawnFood() {
    while (true) {
        int8_t fx = random(1, GRID_W - 1);
        int8_t fy = random(1, GRID_H - 1);

        bool onSnake = false;
        for (int i = 0; i < snakeLen; i++) {
            if (snakeX[i] == fx && snakeY[i] == fy) { onSnake = true; break; }
        }
        if (!onSnake) {
            foodX = fx;
            foodY = fy;
            return;
        }
    }
}

static void resetSnake() {
    snakeLen = 3;
    snakeX[0] = GRID_W / 2;     snakeY[0] = GRID_H / 2;
    snakeX[1] = GRID_W / 2 - 1; snakeY[1] = GRID_H / 2;
    snakeX[2] = GRID_W / 2 - 2; snakeY[2] = GRID_H / 2;

    dirX = 1; dirY = 0;
    nextDirX = 1; nextDirY = 0;

    score = 0;
    spawnFood();
}

static void handleTurnInput(const Input& input) {
    static uint32_t lastTurn = 0;
    if (millis() - lastTurn < 120) return;

    if (input.Center == ButtonState::Down || input.Center == ButtonState::Hold) {
        lastTurn = millis();
        int8_t nx = -dirY;
        int8_t ny =  dirX;
        if (!(nx == -dirX && ny == -dirY)) {
            nextDirX = nx;
            nextDirY = ny;
        }
    } else if (input.Right == ButtonState::Down || input.Right == ButtonState::Hold) {
        lastTurn = millis();
        int8_t nx =  dirY;
        int8_t ny = -dirX;
        if (!(nx == -dirX && ny == -dirY)) {
            nextDirX = nx;
            nextDirY = ny;
        }
    }
}

void snakeInit(GyverOLED<SSD1306_128x64, OLED_BUFFER>* _oled) {
    oled = _oled;
    bestScore = loadBestScore();
    current_state = STATE_START;
    resetSnake();
}

void snakeGame(void) {
    resetSnake();
    current_state = STATE_PLAYING;

    while (1) {
        yield();

        Input input = GetInput();

        if (input.Left == ButtonState::Down) {
            return;
        }

        if (current_state == STATE_PLAYING) {
            handleTurnInput(input);

            static uint32_t frameTimer = millis();
            if (millis() - frameTimer >= FRAME_INTERVAL) {
                frameTimer = millis();

                dirX = nextDirX;
                dirY = nextDirY;

                int8_t headX = snakeX[0] + dirX;
                int8_t headY = snakeY[0] + dirY;

                if (headX <= 0 || headX >= GRID_W - 1 || headY <= 0 || headY >= GRID_H - 1) {
                    current_state = STATE_GAMEOVER;
                } else {
                    bool selfHit = false;
                    for (int i = 0; i < snakeLen - 1; i++) {
                        if (snakeX[i] == headX && snakeY[i] == headY) { selfHit = true; break; }
                    }
                    if (selfHit) {
                        current_state = STATE_GAMEOVER;
                    } else {
                        bool ate = (headX == foodX && headY == foodY);
                        if (ate && snakeLen < MAX_SNAKE) snakeLen++;

                        for (int i = snakeLen - 1; i > 0; i--) {
                            snakeX[i] = snakeX[i - 1];
                            snakeY[i] = snakeY[i - 1];
                        }
                        snakeX[0] = headX;
                        snakeY[0] = headY;

                        if (ate) {
                            score++;
                            if (score > bestScore) {
                                bestScore = score;
                                saveBestScore(bestScore);
                            }
                            spawnFood();
                        }
                    }
                }

                oled->clear();
                drawBorder();
                fillCell(foodX, foodY, OLED_FILL);
                for (int i = 0; i < snakeLen; i++) {
                    fillCell(snakeX[i], snakeY[i], OLED_FILL);
                }
                oled->setCursor(84, 0);
                oled->print(score);
                oled->update();
            }
        } else if (current_state == STATE_GAMEOVER) {
            oled->clear();
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

            bool anyPressed = false;
            while (!anyPressed) {
                yield();
                input = GetInput();
                if (input.Left == ButtonState::Down) return;
                if (input.Center == ButtonState::Down || input.Right == ButtonState::Down) {
                    anyPressed = true;
                }
            }
            while (input.Center == ButtonState::Down || input.Center == ButtonState::Hold ||
                   input.Right  == ButtonState::Down || input.Right  == ButtonState::Hold) {
                yield();
                input = GetInput();
                if (input.Left == ButtonState::Down) return;
            }
            resetSnake();
            current_state = STATE_PLAYING;
        }
    }
}