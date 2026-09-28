#pragma once
#include <dxgiformat.h>

// Sampling these typed views already converts sRGB channels to linear light.
inline bool capture_view_linearizes(DXGI_FORMAT format) {
  return format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
         format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
         format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}
