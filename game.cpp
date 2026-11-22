#include "Game.h"
#include "../core/rl.h"
#include <string>

using namespace rl;

Game::Game() {}

Game::~Game() {}

void Game::init() {}

void Game::update(const InputState &input) {
    touchPoint = input;
}

void Game::draw() {

    const InputState& rawInput = touchPoint;

    int y = 60;

    DrawText(
        TextFormat("TOUCH COUNT: %d",
                   (int)touchPoint.touches.size()),
        20, y, 24, YELLOW
    );

    y += 40;

    for (const auto& t : touchPoint.touches) {

    // Jangan gambar jika touch sudah tidak aktif
    if (!t.down)
        continue;

    DrawCircleV(
        t.position,
        40.0f,
        RED
    );

    DrawCircleLines(
        (int)t.position.x,
        (int)t.position.y,
        40.0f,
        GREEN
    );

    DrawCircleV(
        t.position,
        6.0f,
        YELLOW
    );
}
}
void Game::close() {}
