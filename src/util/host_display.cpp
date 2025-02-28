// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string_util.h"
#include "common/timer.h"
#include "core/settings.h" // TODO FIXME
#include "stb_image.h"
#include "stb_image_resize.h"
#include "stb_image_write.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>
Log_SetChannel(HostDisplay);

std::unique_ptr<HostDisplay> g_host_display;

HostDisplay::~HostDisplay() = default;

RenderAPI HostDisplay::GetPreferredAPI()
{
#ifdef _WIN32
  return RenderAPI::D3D11;
#else
  return RenderAPI::OpenGL;
#endif
}

void HostDisplay::DestroyResources()
{
  m_cursor_texture.reset();
}

bool HostDisplay::UpdateTexture(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* data, u32 pitch)
{
  void* map_ptr;
  u32 map_pitch;
  if (!BeginTextureUpdate(texture, width, height, &map_ptr, &map_pitch))
    return false;

  StringUtil::StrideMemCpy(map_ptr, map_pitch, data, pitch, std::min(pitch, map_pitch), height);
  EndTextureUpdate(texture, x, y, width, height);
  return true;
}

bool HostDisplay::ParseFullscreenMode(const std::string_view& mode, u32* width, u32* height, float* refresh_rate)
{
  if (!mode.empty())
  {
    std::string_view::size_type sep1 = mode.find('x');
    if (sep1 != std::string_view::npos)
    {
      std::optional<u32> owidth = StringUtil::FromChars<u32>(mode.substr(0, sep1));
      sep1++;

      while (sep1 < mode.length() && std::isspace(mode[sep1]))
        sep1++;

      if (owidth.has_value() && sep1 < mode.length())
      {
        std::string_view::size_type sep2 = mode.find('@', sep1);
        if (sep2 != std::string_view::npos)
        {
          std::optional<u32> oheight = StringUtil::FromChars<u32>(mode.substr(sep1, sep2 - sep1));
          sep2++;

          while (sep2 < mode.length() && std::isspace(mode[sep2]))
            sep2++;

          if (oheight.has_value() && sep2 < mode.length())
          {
            std::optional<float> orefresh_rate = StringUtil::FromChars<float>(mode.substr(sep2));
            if (orefresh_rate.has_value())
            {
              *width = owidth.value();
              *height = oheight.value();
              *refresh_rate = orefresh_rate.value();
              return true;
            }
          }
        }
      }
    }
  }

  *width = 0;
  *height = 0;
  *refresh_rate = 0;
  return false;
}

std::string HostDisplay::GetFullscreenModeString(u32 width, u32 height, float refresh_rate)
{
  return StringUtil::StdStringFromFormat("%u x %u @ %f hz", width, height, refresh_rate);
}

bool HostDisplay::UsesLowerLeftOrigin() const
{
  const RenderAPI api = GetRenderAPI();
  return (api == RenderAPI::OpenGL || api == RenderAPI::OpenGLES);
}

void HostDisplay::SetDisplayMaxFPS(float max_fps)
{
  m_display_frame_interval = (max_fps > 0.0f) ? (1.0f / max_fps) : 0.0f;
}

bool HostDisplay::ShouldSkipDisplayingFrame()
{
  if (m_display_frame_interval == 0.0f)
    return false;

  const u64 now = Common::Timer::GetCurrentValue();
  const double diff = Common::Timer::ConvertValueToSeconds(now - m_last_frame_displayed_time);
  if (diff < m_display_frame_interval)
    return true;

  m_last_frame_displayed_time = now;
  return false;
}

void HostDisplay::ThrottlePresentation()
{
  const float throttle_rate = (m_window_info.surface_refresh_rate > 0.0f) ? m_window_info.surface_refresh_rate : 60.0f;

  const u64 sleep_period = Common::Timer::ConvertNanosecondsToValue(1e+9f / static_cast<double>(throttle_rate));
  const u64 current_ts = Common::Timer::GetCurrentValue();

  // Allow it to fall behind/run ahead up to 2*period. Sleep isn't that precise, plus we need to
  // allow time for the actual rendering.
  const u64 max_variance = sleep_period * 2;
  if (static_cast<u64>(std::abs(static_cast<s64>(current_ts - m_last_frame_displayed_time))) > max_variance)
    m_last_frame_displayed_time = current_ts + sleep_period;
  else
    m_last_frame_displayed_time += sleep_period;

  Common::Timer::SleepUntil(m_last_frame_displayed_time, false);
}

bool HostDisplay::GetHostRefreshRate(float* refresh_rate)
{
  if (m_window_info.surface_refresh_rate > 0.0f)
  {
    *refresh_rate = m_window_info.surface_refresh_rate;
    return true;
  }

  return WindowInfo::QueryRefreshRateForWindow(m_window_info, refresh_rate);
}

bool HostDisplay::SetGPUTimingEnabled(bool enabled)
{
  return false;
}

float HostDisplay::GetAndResetAccumulatedGPUTime()
{
  return 0.0f;
}

void HostDisplay::SetSoftwareCursor(std::unique_ptr<GPUTexture> texture, float scale /*= 1.0f*/)
{
  m_cursor_texture = std::move(texture);
  m_cursor_texture_scale = scale;
}

bool HostDisplay::SetSoftwareCursor(const void* pixels, u32 width, u32 height, u32 stride, float scale /*= 1.0f*/)
{
  std::unique_ptr<GPUTexture> tex =
    CreateTexture(width, height, 1, 1, 1, GPUTexture::Format::RGBA8, pixels, stride, false);
  if (!tex)
    return false;

  SetSoftwareCursor(std::move(tex), scale);
  return true;
}

bool HostDisplay::SetSoftwareCursor(const char* path, float scale /*= 1.0f*/)
{
  auto fp = FileSystem::OpenManagedCFile(path, "rb");
  if (!fp)
  {
    return false;
  }

  int width, height, file_channels;
  u8* pixel_data = stbi_load_from_file(fp.get(), &width, &height, &file_channels, 4);
  if (!pixel_data)
  {
    const char* error_reason = stbi_failure_reason();
    Log_ErrorPrintf("Failed to load image from '%s': %s", path, error_reason ? error_reason : "unknown error");
    return false;
  }

  std::unique_ptr<GPUTexture> tex =
    CreateTexture(static_cast<u32>(width), static_cast<u32>(height), 1, 1, 1, GPUTexture::Format::RGBA8, pixel_data,
                  sizeof(u32) * static_cast<u32>(width), false);
  stbi_image_free(pixel_data);
  if (!tex)
    return false;

  Log_InfoPrintf("Loaded %dx%d image from '%s' for software cursor", width, height, path);
  SetSoftwareCursor(std::move(tex), scale);
  return true;
}

void HostDisplay::ClearSoftwareCursor()
{
  m_cursor_texture.reset();
  m_cursor_texture_scale = 1.0f;
}

bool HostDisplay::IsUsingLinearFiltering() const
{
  return g_settings.display_linear_filtering;
}

void HostDisplay::CalculateDrawRect(s32 window_width, s32 window_height, float* out_left, float* out_top,
                                    float* out_width, float* out_height, float* out_left_padding,
                                    float* out_top_padding, float* out_scale, float* out_x_scale,
                                    bool apply_aspect_ratio /* = true */) const
{
  const float window_ratio = static_cast<float>(window_width) / static_cast<float>(window_height);
  const float display_aspect_ratio = g_settings.display_stretch ? window_ratio : m_display_aspect_ratio;
  const float x_scale =
    apply_aspect_ratio ?
      (display_aspect_ratio / (static_cast<float>(m_display_width) / static_cast<float>(m_display_height))) :
      1.0f;
  const float display_width = g_settings.display_stretch_vertically ? static_cast<float>(m_display_width) :
                                                                      static_cast<float>(m_display_width) * x_scale;
  const float display_height = g_settings.display_stretch_vertically ? static_cast<float>(m_display_height) / x_scale :
                                                                       static_cast<float>(m_display_height);
  const float active_left = g_settings.display_stretch_vertically ? static_cast<float>(m_display_active_left) :
                                                                    static_cast<float>(m_display_active_left) * x_scale;
  const float active_top = g_settings.display_stretch_vertically ? static_cast<float>(m_display_active_top) / x_scale :
                                                                   static_cast<float>(m_display_active_top);
  const float active_width = g_settings.display_stretch_vertically ?
                               static_cast<float>(m_display_active_width) :
                               static_cast<float>(m_display_active_width) * x_scale;
  const float active_height = g_settings.display_stretch_vertically ?
                                static_cast<float>(m_display_active_height) / x_scale :
                                static_cast<float>(m_display_active_height);
  if (out_x_scale)
    *out_x_scale = x_scale;

  // now fit it within the window
  float scale;
  if ((display_width / display_height) >= window_ratio)
  {
    // align in middle vertically
    scale = static_cast<float>(window_width) / display_width;
    if (g_settings.display_integer_scaling)
      scale = std::max(std::floor(scale), 1.0f);

    if (out_left_padding)
    {
      if (g_settings.display_integer_scaling)
        *out_left_padding = std::max<float>((static_cast<float>(window_width) - display_width * scale) / 2.0f, 0.0f);
      else
        *out_left_padding = 0.0f;
    }
    if (out_top_padding)
    {
      switch (g_settings.display_alignment)
      {
        case DisplayAlignment::RightOrBottom:
          *out_top_padding = std::max<float>(static_cast<float>(window_height) - (display_height * scale), 0.0f);
          break;

        case DisplayAlignment::Center:
          *out_top_padding =
            std::max<float>((static_cast<float>(window_height) - (display_height * scale)) / 2.0f, 0.0f);
          break;

        case DisplayAlignment::LeftOrTop:
        default:
          *out_top_padding = 0.0f;
          break;
      }
    }
  }
  else
  {
    // align in middle horizontally
    scale = static_cast<float>(window_height) / display_height;
    if (g_settings.display_integer_scaling)
      scale = std::max(std::floor(scale), 1.0f);

    if (out_left_padding)
    {
      switch (g_settings.display_alignment)
      {
        case DisplayAlignment::RightOrBottom:
          *out_left_padding = std::max<float>(static_cast<float>(window_width) - (display_width * scale), 0.0f);
          break;

        case DisplayAlignment::Center:
          *out_left_padding =
            std::max<float>((static_cast<float>(window_width) - (display_width * scale)) / 2.0f, 0.0f);
          break;

        case DisplayAlignment::LeftOrTop:
        default:
          *out_left_padding = 0.0f;
          break;
      }
    }

    if (out_top_padding)
    {
      if (g_settings.display_integer_scaling)
        *out_top_padding = std::max<float>((static_cast<float>(window_height) - (display_height * scale)) / 2.0f, 0.0f);
      else
        *out_top_padding = 0.0f;
    }
  }

  *out_width = active_width * scale;
  *out_height = active_height * scale;
  *out_left = active_left * scale;
  *out_top = active_top * scale;
  if (out_scale)
    *out_scale = scale;
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateDrawRect(s32 window_width, s32 window_height,
                                                              bool apply_aspect_ratio /* = true */) const
{
  float left, top, width, height, left_padding, top_padding;
  CalculateDrawRect(window_width, window_height, &left, &top, &width, &height, &left_padding, &top_padding, nullptr,
                    nullptr, apply_aspect_ratio);

  return std::make_tuple(static_cast<s32>(left + left_padding), static_cast<s32>(top + top_padding),
                         static_cast<s32>(width), static_cast<s32>(height));
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateSoftwareCursorDrawRect() const
{
  return CalculateSoftwareCursorDrawRect(m_mouse_position_x, m_mouse_position_y);
}

std::tuple<s32, s32, s32, s32> HostDisplay::CalculateSoftwareCursorDrawRect(s32 cursor_x, s32 cursor_y) const
{
  const float scale = m_window_info.surface_scale * m_cursor_texture_scale;
  const u32 cursor_extents_x = static_cast<u32>(static_cast<float>(m_cursor_texture->GetWidth()) * scale * 0.5f);
  const u32 cursor_extents_y = static_cast<u32>(static_cast<float>(m_cursor_texture->GetHeight()) * scale * 0.5f);

  const s32 out_left = cursor_x - cursor_extents_x;
  const s32 out_top = cursor_y - cursor_extents_y;
  const s32 out_width = cursor_extents_x * 2u;
  const s32 out_height = cursor_extents_y * 2u;

  return std::tie(out_left, out_top, out_width, out_height);
}

std::tuple<float, float> HostDisplay::ConvertWindowCoordinatesToDisplayCoordinates(s32 window_x, s32 window_y,
                                                                                   s32 window_width,
                                                                                   s32 window_height) const
{
  float left, top, width, height, left_padding, top_padding;
  float scale, x_scale;
  CalculateDrawRect(window_width, window_height, &left, &top, &width, &height, &left_padding, &top_padding, &scale,
                    &x_scale);

  // convert coordinates to active display region, then to full display region
  const float scaled_display_x = static_cast<float>(window_x) - left_padding;
  const float scaled_display_y = static_cast<float>(window_y) - top_padding;

  // scale back to internal resolution
  const float display_x = scaled_display_x / scale / x_scale;
  const float display_y = scaled_display_y / scale;

  return std::make_tuple(display_x, display_y);
}

static bool CompressAndWriteTextureToFile(u32 width, u32 height, std::string filename, FileSystem::ManagedCFilePtr fp,
                                          bool clear_alpha, bool flip_y, u32 resize_width, u32 resize_height,
                                          std::vector<u32> texture_data, u32 texture_data_stride,
                                          GPUTexture::Format texture_format)
{

  const char* extension = std::strrchr(filename.c_str(), '.');
  if (!extension)
  {
    Log_ErrorPrintf("Unable to determine file extension for '%s'", filename.c_str());
    return false;
  }

  if (!GPUTexture::ConvertTextureDataToRGBA8(width, height, texture_data, texture_data_stride, texture_format))
    return false;

  if (clear_alpha)
  {
    for (u32& pixel : texture_data)
      pixel |= 0xFF000000;
  }

  if (flip_y)
    GPUTexture::FlipTextureDataRGBA8(width, height, texture_data, texture_data_stride);

  if (resize_width > 0 && resize_height > 0 && (resize_width != width || resize_height != height))
  {
    std::vector<u32> resized_texture_data(resize_width * resize_height);
    u32 resized_texture_stride = sizeof(u32) * resize_width;
    if (!stbir_resize_uint8(reinterpret_cast<u8*>(texture_data.data()), width, height, texture_data_stride,
                            reinterpret_cast<u8*>(resized_texture_data.data()), resize_width, resize_height,
                            resized_texture_stride, 4))
    {
      Log_ErrorPrintf("Failed to resize texture data from %ux%u to %ux%u", width, height, resize_width, resize_height);
      return false;
    }

    width = resize_width;
    height = resize_height;
    texture_data = std::move(resized_texture_data);
    texture_data_stride = resized_texture_stride;
  }

  const auto write_func = [](void* context, void* data, int size) {
    std::fwrite(data, 1, size, static_cast<std::FILE*>(context));
  };

  bool result = false;
  if (StringUtil::Strcasecmp(extension, ".png") == 0)
  {
    result =
      (stbi_write_png_to_func(write_func, fp.get(), width, height, 4, texture_data.data(), texture_data_stride) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".jpg") == 0)
  {
    result = (stbi_write_jpg_to_func(write_func, fp.get(), width, height, 4, texture_data.data(), 95) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".tga") == 0)
  {
    result = (stbi_write_tga_to_func(write_func, fp.get(), width, height, 4, texture_data.data()) != 0);
  }
  else if (StringUtil::Strcasecmp(extension, ".bmp") == 0)
  {
    result = (stbi_write_bmp_to_func(write_func, fp.get(), width, height, 4, texture_data.data()) != 0);
  }

  if (!result)
  {
    Log_ErrorPrintf("Unknown extension in filename '%s' or save error: '%s'", filename.c_str(), extension);
    return false;
  }

  return true;
}

bool HostDisplay::WriteTextureToFile(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, std::string filename,
                                     bool clear_alpha /* = true */, bool flip_y /* = false */,
                                     u32 resize_width /* = 0 */, u32 resize_height /* = 0 */,
                                     bool compress_on_thread /* = false */)
{
  std::vector<u32> texture_data(width * height);
  u32 texture_data_stride = Common::AlignUpPow2(GPUTexture::GetPixelSize(texture->GetFormat()) * width, 4);
  if (!DownloadTexture(texture, x, y, width, height, texture_data.data(), texture_data_stride))
  {
    Log_ErrorPrintf("Texture download failed");
    return false;
  }

  auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "wb");
  if (!fp)
  {
    Log_ErrorPrintf("Can't open file '%s': errno %d", filename.c_str(), errno);
    return false;
  }

  if (!compress_on_thread)
  {
    return CompressAndWriteTextureToFile(width, height, std::move(filename), std::move(fp), clear_alpha, flip_y,
                                         resize_width, resize_height, std::move(texture_data), texture_data_stride,
                                         texture->GetFormat());
  }

  std::thread compress_thread(CompressAndWriteTextureToFile, width, height, std::move(filename), std::move(fp),
                              clear_alpha, flip_y, resize_width, resize_height, std::move(texture_data),
                              texture_data_stride, texture->GetFormat());
  compress_thread.detach();
  return true;
}

bool HostDisplay::WriteDisplayTextureToFile(std::string filename, bool full_resolution /* = true */,
                                            bool apply_aspect_ratio /* = true */, bool compress_on_thread /* = false */)
{
  if (!m_display_texture)
    return false;

  s32 resize_width = 0;
  s32 resize_height = std::abs(m_display_texture_view_height);
  if (apply_aspect_ratio)
  {
    const float ss_width_scale = static_cast<float>(m_display_active_width) / static_cast<float>(m_display_width);
    const float ss_height_scale = static_cast<float>(m_display_active_height) / static_cast<float>(m_display_height);
    const float ss_aspect_ratio = m_display_aspect_ratio * ss_width_scale / ss_height_scale;
    resize_width = g_settings.display_stretch_vertically ?
                     m_display_texture_view_width :
                     static_cast<s32>(static_cast<float>(resize_height) * ss_aspect_ratio);
    resize_height = g_settings.display_stretch_vertically ?
                      static_cast<s32>(static_cast<float>(resize_height) /
                                       (m_display_aspect_ratio /
                                        (static_cast<float>(m_display_width) / static_cast<float>(m_display_height)))) :
                      resize_height;
  }
  else
  {
    resize_width = m_display_texture_view_width;
  }

  if (!full_resolution)
  {
    const s32 resolution_scale = std::abs(m_display_texture_view_height) / m_display_active_height;
    resize_height /= resolution_scale;
    resize_width /= resolution_scale;
  }

  if (resize_width <= 0 || resize_height <= 0)
    return false;

  const bool flip_y = (m_display_texture_view_height < 0);
  s32 read_height = m_display_texture_view_height;
  s32 read_y = m_display_texture_view_y;
  if (flip_y)
  {
    read_height = -m_display_texture_view_height;
    read_y =
      (m_display_texture->GetHeight() - read_height) - (m_display_texture->GetHeight() - m_display_texture_view_y);
  }

  return WriteTextureToFile(m_display_texture, m_display_texture_view_x, read_y, m_display_texture_view_width,
                            read_height, std::move(filename), true, flip_y, static_cast<u32>(resize_width),
                            static_cast<u32>(resize_height), compress_on_thread);
}

bool HostDisplay::WriteDisplayTextureToBuffer(std::vector<u32>* buffer, u32 resize_width /* = 0 */,
                                              u32 resize_height /* = 0 */, bool clear_alpha /* = true */)
{
  if (!m_display_texture)
    return false;

  const bool flip_y = (m_display_texture_view_height < 0);
  s32 read_width = m_display_texture_view_width;
  s32 read_height = m_display_texture_view_height;
  s32 read_x = m_display_texture_view_x;
  s32 read_y = m_display_texture_view_y;
  if (flip_y)
  {
    read_height = -m_display_texture_view_height;
    read_y =
      (m_display_texture->GetHeight() - read_height) - (m_display_texture->GetHeight() - m_display_texture_view_y);
  }

  u32 width = static_cast<u32>(read_width);
  u32 height = static_cast<u32>(read_height);
  std::vector<u32> texture_data(width * height);
  u32 texture_data_stride = Common::AlignUpPow2(m_display_texture->GetPixelSize() * width, 4);
  if (!DownloadTexture(m_display_texture, read_x, read_y, width, height, texture_data.data(), texture_data_stride))
  {
    Log_ErrorPrintf("Failed to download texture from GPU.");
    return false;
  }

  if (!GPUTexture::ConvertTextureDataToRGBA8(width, height, texture_data, texture_data_stride,
                                             m_display_texture->GetFormat()))
  {
    return false;
  }

  if (clear_alpha)
  {
    for (u32& pixel : texture_data)
      pixel |= 0xFF000000;
  }

  if (flip_y)
  {
    std::vector<u32> temp(width);
    for (u32 flip_row = 0; flip_row < (height / 2); flip_row++)
    {
      u32* top_ptr = &texture_data[flip_row * width];
      u32* bottom_ptr = &texture_data[((height - 1) - flip_row) * width];
      std::memcpy(temp.data(), top_ptr, texture_data_stride);
      std::memcpy(top_ptr, bottom_ptr, texture_data_stride);
      std::memcpy(bottom_ptr, temp.data(), texture_data_stride);
    }
  }

  if (resize_width > 0 && resize_height > 0 && (resize_width != width || resize_height != height))
  {
    std::vector<u32> resized_texture_data(resize_width * resize_height);
    u32 resized_texture_stride = sizeof(u32) * resize_width;
    if (!stbir_resize_uint8(reinterpret_cast<u8*>(texture_data.data()), width, height, texture_data_stride,
                            reinterpret_cast<u8*>(resized_texture_data.data()), resize_width, resize_height,
                            resized_texture_stride, 4))
    {
      Log_ErrorPrintf("Failed to resize texture data from %ux%u to %ux%u", width, height, resize_width, resize_height);
      return false;
    }

    width = resize_width;
    height = resize_height;
    *buffer = std::move(resized_texture_data);
    texture_data_stride = resized_texture_stride;
  }
  else
  {
    *buffer = texture_data;
  }

  return true;
}

bool HostDisplay::WriteScreenshotToFile(std::string filename, bool internal_resolution /* = false */,
                                        bool compress_on_thread /* = false */)
{
  u32 width = m_window_info.surface_width;
  u32 height = m_window_info.surface_height;
  auto [draw_left, draw_top, draw_width, draw_height] = CalculateDrawRect(width, height);

  if (internal_resolution && m_display_texture_view_width != 0 && m_display_texture_view_height != 0)
  {
    // If internal res, scale the computed draw rectangle to the internal res.
    // We re-use the draw rect because it's already been AR corrected.
    const float sar =
      static_cast<float>(m_display_texture_view_width) / static_cast<float>(m_display_texture_view_height);
    const float dar = static_cast<float>(draw_width) / static_cast<float>(draw_height);
    if (sar >= dar)
    {
      // stretch height, preserve width
      const float scale = static_cast<float>(m_display_texture_view_width) / static_cast<float>(draw_width);
      width = m_display_texture_view_width;
      height = static_cast<u32>(std::round(static_cast<float>(draw_height) * scale));
    }
    else
    {
      // stretch width, preserve height
      const float scale = static_cast<float>(m_display_texture_view_height) / static_cast<float>(draw_height);
      width = static_cast<u32>(std::round(static_cast<float>(draw_width) * scale));
      height = m_display_texture_view_height;
    }

    // DX11 won't go past 16K texture size.
    constexpr u32 MAX_TEXTURE_SIZE = 16384;
    if (width > MAX_TEXTURE_SIZE)
    {
      height = static_cast<u32>(static_cast<float>(height) /
                                (static_cast<float>(width) / static_cast<float>(MAX_TEXTURE_SIZE)));
      width = MAX_TEXTURE_SIZE;
    }
    if (height > MAX_TEXTURE_SIZE)
    {
      height = MAX_TEXTURE_SIZE;
      width = static_cast<u32>(static_cast<float>(width) /
                               (static_cast<float>(height) / static_cast<float>(MAX_TEXTURE_SIZE)));
    }

    // Remove padding, it's not part of the framebuffer.
    draw_left = 0;
    draw_top = 0;
    draw_width = static_cast<s32>(width);
    draw_height = static_cast<s32>(height);
  }
  if (width == 0 || height == 0)
    return false;

  std::vector<u32> pixels;
  u32 pixels_stride;
  GPUTexture::Format pixels_format;
  if (!RenderScreenshot(width, height,
                        Common::Rectangle<s32>::FromExtents(draw_left, draw_top, draw_width, draw_height), &pixels,
                        &pixels_stride, &pixels_format))
  {
    Log_ErrorPrintf("Failed to render %ux%u screenshot", width, height);
    return false;
  }

  auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "wb");
  if (!fp)
  {
    Log_ErrorPrintf("Can't open file '%s': errno %d", filename.c_str(), errno);
    return false;
  }

  if (!compress_on_thread)
  {
    return CompressAndWriteTextureToFile(width, height, std::move(filename), std::move(fp), true, UsesLowerLeftOrigin(),
                                         width, height, std::move(pixels), pixels_stride, pixels_format);
  }

  std::thread compress_thread(CompressAndWriteTextureToFile, width, height, std::move(filename), std::move(fp), true,
                              UsesLowerLeftOrigin(), width, height, std::move(pixels), pixels_stride, pixels_format);
  compress_thread.detach();
  return true;
}
