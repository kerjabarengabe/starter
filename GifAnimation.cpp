#include "GifAnimation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

// Hitung frame tanpa decode piksel.
static int CountFrames(const char *path) {
  int error = 0;
  GifFileType *g = DGifOpenFileName(path, &error);
  if (!g)
    return 0;

  int count = 0;
  bool ok = true;
  GifRecordType type;

  while (ok && DGifGetRecordType(g, &type) == GIF_OK &&
         type != TERMINATE_RECORD_TYPE) {
    GifByteType *block = nullptr;
    int code = 0;

    if (type == IMAGE_DESC_RECORD_TYPE) {
      ok = DGifGetImageDesc(g) == GIF_OK &&
           DGifGetCode(g, &code, &block) == GIF_OK;
      while (ok && block)
        ok = DGifGetCodeNext(g, &block) == GIF_OK;
      if (ok)
        ++count;
    } else if (type == EXTENSION_RECORD_TYPE) {
      ok = DGifGetExtension(g, &code, &block) == GIF_OK;
      while (ok && block)
        ok = DGifGetExtensionNext(g, &block) == GIF_OK;
    }
  }

  DGifCloseFile(g, &error);
  return count;
}

// =====================================================
// CONSTRUCTOR / DESTRUCTOR
// =====================================================

GifAnimation::~GifAnimation() { Unload(); }

// =====================================================
// LOAD
// =====================================================

bool GifAnimation::Load(const char *path) {
  Unload();

  if (!path)
    return false;

  filename = path;

  if (!OpenDecoder()) {
    TraceLog(LOG_ERROR, "GIF: Failed to open %s", path);
    Unload();
    return false;
  }

  canvas.assign(static_cast<size_t>(width) * height * 4, 0);

  int delay = 0;

  if (!ReadNextFrame(delay)) {
    TraceLog(LOG_ERROR, "GIF: Failed to decode first frame");
    Unload();
    return false;
  }

  Image image = {canvas.data(), width, height, 1,
                 PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

  texture = LoadTextureFromImage(image);

  if (texture.id == 0) {
    Unload();
    return false;
  }

  // Delay 0-1 cs diperlakukan 10 cs, seperti browser.
  frameDuration = (delay <= 1 ? 10 : delay) * 0.01f;
  timer = 0.0f;

  playing = true;
  finished = false;

  TraceLog(LOG_INFO, "GIF loaded: %dx%d, first frame duration %.3f s", width,
           height, frameDuration);

  shownFrame = currentFrame;

  frameCount = CountFrames(path);

  StartDecode();
  return true;
}
// =====================================================
// OPEN DECODER
// =====================================================

bool GifAnimation::OpenDecoder() {
  int error = 0;

  gif = DGifOpenFileName(filename.c_str(), &error);

  if (!gif)
    return false;

  width = gif->SWidth;
  height = gif->SHeight;

  if (width <= 0 || height <= 0)
    return false;

  if (static_cast<size_t>(width) * height > 100000000)
    return false;

  return true;
}

// =====================================================
// EXTENSION READER
// =====================================================

bool GifAnimation::ReadExtension() {
  int extensionCode = 0;
  GifByteType *extension = nullptr;

  if (DGifGetExtension(gif, &extensionCode, &extension) == GIF_ERROR)
    return false;

  while (extension) {
    if (extensionCode == GRAPHICS_EXT_FUNC_CODE && extension[0] >= 4) {
      unsigned char packed = extension[1];

      nextControl.disposal = (packed >> 2) & 0x07;
      nextControl.transparent = (packed & 0x01) != 0;
      nextControl.delay = extension[2] | (extension[3] << 8);

      nextControl.transparentIndex =
          nextControl.transparent ? extension[4] : NO_TRANSPARENT_COLOR;
    }

    if (DGifGetExtensionNext(gif, &extension) == GIF_ERROR)
      return false;
  }

  return true;
}

// =====================================================
// PIXEL
// =====================================================

void GifAnimation::SetPixel(int x, int y, unsigned char r, unsigned char g,
                            unsigned char b, unsigned char a) {
  if (x < 0 || y < 0 || x >= width || y >= height)
    return;

  size_t index = (static_cast<size_t>(y) * width + x) * 4;

  canvas[index + 0] = r;
  canvas[index + 1] = g;
  canvas[index + 2] = b;
  canvas[index + 3] = a;
}

// =====================================================
// INITIAL CANVAS
// =====================================================

void GifAnimation::InitializeCanvas() {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  unsigned char a = 0;

  if (!nextControl.transparent && gif->SColorMap) {
    int index = gif->SBackGroundColor;

    if (index >= 0 && index < gif->SColorMap->ColorCount) {
      GifColorType color = gif->SColorMap->Colors[index];

      r = color.Red;
      g = color.Green;
      b = color.Blue;
      a = 255;
    }
  }

  for (size_t i = 0; i < canvas.size(); i += 4) {
    canvas[i + 0] = r;
    canvas[i + 1] = g;
    canvas[i + 2] = b;
    canvas[i + 3] = a;
  }

  canvasInitialized = true;
}

// =====================================================
// CLEAR RECTANGLE
// =====================================================

void GifAnimation::ClearRect(const FrameRect &rect) {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  unsigned char a = 0;

  if (!previousControl.transparent && gif->SColorMap) {
    int index = gif->SBackGroundColor;

    if (index >= 0 && index < gif->SColorMap->ColorCount) {
      GifColorType color = gif->SColorMap->Colors[index];

      r = color.Red;
      g = color.Green;
      b = color.Blue;
      a = 255;
    }
  }

  for (int y = rect.top; y < rect.top + rect.height; ++y) {
    for (int x = rect.left; x < rect.left + rect.width; ++x) {
      SetPixel(x, y, r, g, b, a);
    }
  }
}

// =====================================================
// APPLY PREVIOUS DISPOSAL
// =====================================================

void GifAnimation::ApplyPreviousDisposal() {
  if (!hasPreviousFrame)
    return;

  switch (previousControl.disposal) {
  case DISPOSE_BACKGROUND:
    ClearRect(previousRect);
    break;

  case DISPOSE_PREVIOUS:
    if (backup.size() == canvas.size())
      canvas = backup;
    break;

  default:
    break;
  }

  backup.clear();
}

// =====================================================
// DECODE IMAGE
// =====================================================

bool GifAnimation::DrawDecodedFrame() {
  const GifImageDesc &desc = gif->Image;

  ColorMapObject *colorMap = desc.ColorMap ? desc.ColorMap : gif->SColorMap;

  if (!colorMap)
    return false;

  const int fw = desc.Width;
  const int fh = desc.Height;

  if (fw <= 0 || fh <= 0)
    return false;

  lineBuffer.resize(fw);

  const FrameRect rect = {desc.Left, desc.Top, fw, fh};

  // Simpan canvas sebelum menggambar jika disposal = 3.
  if (nextControl.disposal == DISPOSE_PREVIOUS)
    backup = canvas;

  // Palet dikonversi ke RGBA sekali per frame.
  uint32_t lut[256] = {};
  const int count = std::min(colorMap->ColorCount, 256);

  for (int i = 0; i < count; ++i) {
    const GifColorType &c = colorMap->Colors[i];
    const unsigned char px[4] = {c.Red, c.Green, c.Blue, 255};
    std::memcpy(&lut[i], px, 4);
  }

  const int transparent =
      nextControl.transparent ? nextControl.transparentIndex : -1;
  const int visibleWidth = std::min(fw, width - desc.Left);

  auto drawLine = [&](int row) -> bool {
    // Baris tetap harus dibaca walaupun di luar canvas.
    if (DGifGetLine(gif, lineBuffer.data(), fw) == GIF_ERROR)
      return false;

    const int y = desc.Top + row;

    if (y >= height || visibleWidth <= 0)
      return true;

    unsigned char *dst =
        canvas.data() + (static_cast<size_t>(y) * width + desc.Left) * 4;

    for (int x = 0; x < visibleWidth; ++x) {
      const int index = lineBuffer[x];

      if (index != transparent && index < count)
        std::memcpy(dst + x * 4, &lut[index], 4);
    }

    return true;
  };

  if (desc.Interlace) {
    static const int starts[] = {0, 4, 2, 1};
    static const int steps[] = {8, 8, 4, 2};

    for (int pass = 0; pass < 4; ++pass) {
      for (int row = starts[pass]; row < fh; row += steps[pass]) {
        if (!drawLine(row))
          return false;
      }
    }
  } else {
    for (int row = 0; row < fh; ++row) {
      if (!drawLine(row))
        return false;
    }
  }

  previousRect = rect;
  previousControl = nextControl;
  hasPreviousFrame = true;

  return true;
}
// =====================================================
// READ NEXT FRAME
// =====================================================

bool GifAnimation::ReadNextFrame(int &delay) {
  while (true) {
    GifRecordType recordType;

    if (DGifGetRecordType(gif, &recordType) == GIF_ERROR)
      return false;

    switch (recordType) {
    case EXTENSION_RECORD_TYPE:
      if (!ReadExtension())
        return false;
      break;

    case IMAGE_DESC_RECORD_TYPE:
      if (DGifGetImageDesc(gif) == GIF_ERROR)
        return false;

      ApplyPreviousDisposal();

      if (!canvasInitialized)
        InitializeCanvas();

      if (!DrawDecodedFrame())
        return false;

      // GIF delay is stored in hundredths of a second (centiseconds).
      // A value of zero is treated as 1 cs to avoid a zero-duration frame.
      delay = std::max(1, nextControl.delay);

      // Log only occasionally to avoid flooding the console.
      if (currentFrame < 0 || (currentFrame % 30) == 0) {
        TraceLog(LOG_INFO, "GIF frame %d | delay: %d cs | %.1f FPS",
                 currentFrame + 1, delay, 100.0f / delay);
      }

      // GCE applies only to the next graphic.
      nextControl = FrameControl{};

      ++currentFrame;
      return true;

    case TERMINATE_RECORD_TYPE:
      return false;

    default:
      break;
    }
  }
}

// =====================================================
// GPU UPDATE
// =====================================================

void GifAnimation::UpdateGpuTexture() {
  if (texture.id != 0 && !canvas.empty())
    UpdateTexture(texture, canvas.data());
}

// =====================================================
// UPDATE
// =====================================================

void GifAnimation::Update(float deltaTime) {
  if (targetFrame.load() >= 0) {
    UpdateSeek();
    return;
  }

  if (!playing || finished || !pending.valid())
    return;

  timer += std::clamp(deltaTime, 0.0f, 0.1f);

  if (timer < frameDuration)
    return;

  if (pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;

  if (!pending.get()) {
    finished = true;
    playing = false;
    return;
  }

  timer = std::min(timer - frameDuration, frameDuration);
  frameDuration = (pendingDelay <= 1 ? 10 : pendingDelay) * 0.01f;
  shownFrame = currentFrame;

  UpdateGpuTexture();
  StartDecode();
}

void GifAnimation::Seek(int frame) {
  if (texture.id == 0 || frameCount <= 0)
    return;

  targetFrame = std::clamp(frame, 0, frameCount - 1);
  playing = false;
  finished = false;
}

void GifAnimation::StartSeek() {
  seekJob = true;

  pending = std::async(std::launch::async, [this]() {
    while (true) {
      const int target = targetFrame.load();

      if (target < 0)
        return false; // dibatalkan
      if (currentFrame == target)
        return true;

      const bool ok = currentFrame > target ? RestartDecoder(pendingDelay)
                                            : ReadNextFrame(pendingDelay);
      if (!ok)
        return false;
    }
  });
}

void GifAnimation::UpdateSeek() {
  if (pending.valid()) {
    if (pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;

    const bool ok = pending.get();

    if (seekJob && !ok) {
      seekJob = false;
      targetFrame = -1;
      return;
    }
  }

  if (currentFrame != targetFrame.load()) {
    StartSeek();
    return;
  }

  seekJob = false;
  targetFrame = -1;
  shownFrame = currentFrame;
  frameDuration = (pendingDelay <= 1 ? 10 : pendingDelay) * 0.01f;
  timer = 0.0f;

  UpdateGpuTexture();
  StartDecode();
}

bool GifAnimation::IsSeeking() const { return targetFrame.load() >= 0; }

int GifAnimation::GetFrameCount() const { return frameCount; } //
void GifAnimation::StartDecode() {
  const bool loop = looping;

  pending = std::async(std::launch::async, [this, loop]() {
    if (ReadNextFrame(pendingDelay))
      return true;
    return loop && RestartDecoder(pendingDelay);
  });
}
// =====================================================
// RESTART DECODER
// =====================================================

bool GifAnimation::RestartDecoder(int &delay) {
  if (gif) {
    int error = 0;
    DGifCloseFile(gif, &error);
    gif = nullptr;
  }

  if (!OpenDecoder())
    return false;

  canvas.assign(static_cast<size_t>(width) * height * 4, 0);
  backup.clear();

  nextControl = FrameControl{};
  previousControl = FrameControl{};
  previousRect = FrameRect{};

  canvasInitialized = false;
  hasPreviousFrame = false;
  currentFrame = -1;

  return ReadNextFrame(delay);
} // =====================================================
// DRAW
// =====================================================

void GifAnimation::Draw(Vector2 position) const {
  if (texture.id == 0)
    return;

  DrawTextureV(texture, position, WHITE);
}

void GifAnimation::Draw(Rectangle destination) const {
  if (texture.id == 0)
    return;

  const Rectangle source = {0, 0, static_cast<float>(texture.width),
                            static_cast<float>(texture.height)};

  DrawTexturePro(texture, source, destination, {0, 0}, 0.0f, WHITE);
}

// =====================================================
// PLAYBACK
// =====================================================

void GifAnimation::Play() {
  if (finished)
    Reset();

  playing = (texture.id != 0);
}

void GifAnimation::Reset() {
  if (filename.empty())
    return;
  targetFrame = -1;
  seekJob = false;
  if (pending.valid())
    pending.get(); // tunggu thread selesai

  int delay = 0;

  if (!RestartDecoder(delay)) {
    finished = true;
    playing = false;
    return;
  }

  frameDuration = (delay <= 1 ? 10 : delay) * 0.01f;
  timer = 0.0f;
  finished = false;
  playing = true;
  shownFrame = currentFrame;

  UpdateGpuTexture();
  StartDecode();
}
void GifAnimation::Pause() { playing = false; }

void GifAnimation::SetLooping(bool enabled) { looping = enabled; }

// =====================================================
// GETTERS
// =====================================================

bool GifAnimation::IsPlaying() const { return playing; }

bool GifAnimation::IsFinished() const { return finished; }

int GifAnimation::GetWidth() const { return texture.width; }

int GifAnimation::GetHeight() const { return texture.height; }

int GifAnimation::GetCurrentFrame() const { return shownFrame; }
float GifAnimation::GetFrameDuration() const { return frameDuration; }

// =====================================================
// UNLOAD
// =====================================================
void GifAnimation::Unload() {
  targetFrame = -1;
  seekJob = false;

  if (pending.valid())
    pending.get(); // wajib sebelum menutup decoder

  if (gif) {
    int error = 0;
    DGifCloseFile(gif, &error);
    gif = nullptr;
  }

  if (texture.id != 0) {
    UnloadTexture(texture);
    texture = {0};
  }

  canvas.clear();
  backup.clear();
  lineBuffer.clear();
  filename.clear();

  width = 0;
  height = 0;
  currentFrame = -1;
  shownFrame = -1;

  timer = 0.0f;
  frameDuration = 0.1f;

  canvasInitialized = false;
  hasPreviousFrame = false;
  playing = false;
  finished = false;

  nextControl = FrameControl{};
  previousControl = FrameControl{};
  previousRect = FrameRect{};
}
