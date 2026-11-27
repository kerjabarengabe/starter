

#pragma once

#include "raylib.h"
#include <gif_lib.h>

#include <atomic>
#include <future>
#include <string>
#include <vector>

class GifAnimation {
public:
  GifAnimation() = default;
  ~GifAnimation();

  GifAnimation(const GifAnimation &) = delete;
  GifAnimation &operator=(const GifAnimation &) = delete;

  bool Load(const char *filename);

  void Update(float deltaTime);

  void Draw(Vector2 position) const;
  void Draw(Rectangle destination) const;

  void Seek(int frame);
  bool IsSeeking() const;
  int GetFrameCount() const;

  void Play();
  void Pause();
  void Reset();

  void SetLooping(bool enabled);

  bool IsPlaying() const;
  bool IsFinished() const;

  int GetWidth() const;
  int GetHeight() const;

  int GetCurrentFrame() const;
  float GetFrameDuration() const;
  void Unload();

private:
  struct FrameControl {
    int delay = 10;
    int disposal = DISPOSAL_UNSPECIFIED;
    int transparentIndex = NO_TRANSPARENT_COLOR;
    bool transparent = false;
  };

  struct FrameRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
  };

  bool OpenDecoder();
  bool ReadNextFrame(int &delay);

  bool ReadExtension();

  bool DrawDecodedFrame();

  void ApplyPreviousDisposal();
  void InitializeCanvas();

  void SetPixel(int x, int y, unsigned char r, unsigned char g, unsigned char b,
                unsigned char a);

  void ClearRect(const FrameRect &rect);

  void UpdateGpuTexture();

  bool RestartDecoder(int &delay);
  void StartDecode();

private:
  void UpdateSeek();
  void StartSeek();

  std::atomic<int> targetFrame{-1};
  bool seekJob = false;
  int frameCount = 0;

  std::future<bool> pending;
  int pendingDelay = 0;
  int shownFrame = -1;

  GifFileType *gif = nullptr;

  Texture2D texture = {0};

  std::string filename;

  std::vector<unsigned char> canvas;
  std::vector<unsigned char> backup;
  std::vector<GifPixelType> lineBuffer;

  FrameControl nextControl;
  FrameControl previousControl;

  FrameRect previousRect;

  int width = 0;
  int height = 0;

  int currentFrame = -1;

  float timer = 0.0f;
  float frameDuration = 0.1f;

  bool canvasInitialized = false;
  bool hasPreviousFrame = false;

  bool playing = false;
  bool looping = true;
  bool finished = false;
};
