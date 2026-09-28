#pragma once
#include "common/shared_defs.h"
#include <cstring>
#include <initializer_list>

// Zero is an active override for several CE settings. A streaming consumer must
// preserve the game's rendering choices, including HDR, DLSS and frame limits.
inline void initialize_passive_capture_config(SharedGraphicsConfig& config) {
    config = {};
    for (char* text : {config.vsyncMode, config.anisotropicFiltering,
                      config.mipMapping, config.mipBias, config.msaaSamples,
                      config.dlssAutoExposure, config.dlssExposureNormalization,
                      config.dlssDebugOverlay}) {
        std::strcpy(text, "default");
    }
    std::strcpy(config.samplerOverrideMode, "safe");
    std::strcpy(config.mipBiasMode, "strict");
    config.prerenderLimit = -1.0f;
    config.backbufferCount = -1;
    config.dlssSharpening = -2.0f;
    config.tonemapperSharpen = -1.0f;
    config.internalFpsLimit = -1.0f;
    config.internalTextureMipBias = 1000.0f;
    config.displayGamma = -1.0f;
    config.depthOfField = -1;
    config.dlssSuperResolution = -1;
    config.hdrOutput = -1;
    config.hdrColorGamut = -1;
}
