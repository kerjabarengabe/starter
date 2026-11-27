#include "GifAnimation.h"
#include "ParallaxBackground.h"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

static const char *OUTLINE_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float time;

out vec4 finalColor;

const float THICKNESS = 2.0;             // lebar garis (pixel texture)
const float THRESHOLD = 0.6;             // naikkan jika garis terlalu ramai
const float SPEED = 2.0;                 // kedip per detik
const vec3 COLOR = vec3(1.0, 0.9, 0.0);  // kuning

// nilai terang + alpha, agar tepi transparan ikut terdeteksi
float value(vec2 uv) {
    vec4 c = texture(texture0, clamp(uv, 0.0, 1.0));
    return dot(c.rgb, vec3(0.299, 0.587, 0.114)) * c.a + c.a;
}

void main() {
    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec2 t = THICKNESS / vec2(textureSize(texture0, 0));

    float tl = value(fragTexCoord + t * vec2(-1.0, -1.0));
    float tc = value(fragTexCoord + t * vec2( 0.0, -1.0));
    float tr = value(fragTexCoord + t * vec2( 1.0, -1.0));
    float ml = value(fragTexCoord + t * vec2(-1.0,  0.0));
    float mr = value(fragTexCoord + t * vec2( 1.0,  0.0));
    float bl = value(fragTexCoord + t * vec2(-1.0,  1.0));
    float bc = value(fragTexCoord + t * vec2( 0.0,  1.0));
    float br = value(fragTexCoord + t * vec2( 1.0,  1.0));

    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);

    float edge = smoothstep(THRESHOLD, THRESHOLD + 0.2, length(vec2(gx, gy)));
    float blink = 0.5 + 0.5 * sin(time * SPEED * 6.28318);
    float a = edge * blink;

    finalColor = vec4(mix(base.rgb, COLOR, a), max(base.a, a));
}
)"; // Slider vertikal. Return true jika nilai diubah user.
static bool VSlider(Rectangle bar, int &value, int maxValue, bool &dragging) {
  const Vector2 mouse = GetMousePosition();
  const int old = value;

  // Area sentuh dibuat lebih lebar dari track.
  const Rectangle hit = {bar.x - 20, bar.y - 20, bar.width + 40,
                         bar.height + 40};

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      CheckCollisionPointRec(mouse, hit))
    dragging = true;

  if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    dragging = false;

  if (dragging && maxValue > 0) {
    const float t =
        std::clamp((bar.y + bar.height - mouse.y) / bar.height, 0.0f, 1.0f);
    value = (int)lroundf(t * maxValue);
  }

  const float t = maxValue > 0 ? (float)value / maxValue : 0.0f;
  const float knobY = bar.y + bar.height * (1.0f - t);

  DrawRectangleRec(hit, Fade(BLACK, 0.7f));
  DrawRectangleRec(bar, DARKGRAY);
  DrawRectangleRec({bar.x, knobY, bar.width, bar.y + bar.height - knobY},
                   SKYBLUE);
  DrawRectangleRec({bar.x - 14, knobY - 8, bar.width + 28, 16}, RAYWHITE);

  return value != old;
}

int main() {
  InitWindow(1280, 1920, "GIF Background");
  SetTargetFPS(60);

  GifAnimation background;
  ParallaxBackground sky;
  sky.Init();
  if (!background.Load("assets/gif.gif")) {
    CloseWindow();
    return 1;
  }

  Shader outline = LoadShaderFromMemory(nullptr, OUTLINE_FS);
  const int timeLoc = GetShaderLocation(outline, "time");

  ToggleFullscreen();
  int selected = 0;
  bool dragging = false;

  while (!WindowShouldClose()) {
    sky.Update(GetFrameTime());
    const int last = background.GetFrameCount() - 1;

    if (IsKeyPressed(KEY_SPACE)) {
      if (background.IsPlaying())
        background.Pause();
      else
        background.Play();
    }

    const int step = (int)IsKeyPressed(KEY_UP) - (int)IsKeyPressed(KEY_DOWN);

    if (step != 0 && selected + step >= 0 && selected + step <= last) {
      selected += step;
      background.Seek(selected);
    }

    background.Update(GetFrameTime());

    if (!dragging && background.IsPlaying() && !background.IsSeeking())
      selected = background.GetCurrentFrame();

    BeginDrawing();
    ClearBackground(SKYBLUE);
    sky.Draw(0);
    const Vector2 size = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    const float time = (float)GetTime();

    SetShaderValue(outline, timeLoc, &time, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(outline);
    background.Draw({0, 0, size.x, size.y});
    EndShaderMode(); // Tinggi slider mengikuti tinggi window.

    const Rectangle bar = {40, 120, 20, GetScreenHeight() - 240.0f};

    if (VSlider(bar, selected, last, dragging))
      background.Seek(selected);

    DrawRectangle(90, 100, 260, 60, Fade(BLACK, 0.7f));
    DrawText(TextFormat("Frame %d / %d", selected, last), 105, 108, 24,
             RAYWHITE);
    DrawText(background.IsSeeking()   ? "seeking..."
             : background.IsPlaying() ? "play"
                                      : "pause",
             105, 136, 20, SKYBLUE);

    EndDrawing();
  }
  sky.Unload();
  UnloadShader(outline);
  background.Unload();
  CloseWindow();

  return 0;
}
