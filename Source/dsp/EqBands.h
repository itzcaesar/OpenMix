#pragma once

// Layout of the 8-band parametric EQ on strips and buses (plain C++, shared by the DSP,
// the engine's settings, strip presets and the UI).
//   band 0       low shelf   (gain, frequency)
//   bands 1..6   bells       (gain, frequency, Q)
//   band 7       high shelf  (gain, frequency)

#include <array>

namespace omx
{
    constexpr int kEqBands = 8;

    enum class EqBandType { lowShelf, bell, highShelf };

    constexpr EqBandType eqBandType (int band) noexcept
    {
        return band == 0 ? EqBandType::lowShelf : band == kEqBands - 1 ? EqBandType::highShelf : EqBandType::bell;
    }

    // Defaults spread across the voice and music range. Bands 0, 3 and 7 are where the old
    // 3-band EQ's low shelf (120 Hz), sweepable mid (1 kHz) and high shelf (8 kHz) live, so
    // sessions from before keep their sound.
    constexpr std::array<float, kEqBands> kEqDefaultHz { 120.0f, 200.0f, 400.0f, 1000.0f, 2000.0f, 3500.0f, 6000.0f, 8000.0f };
    constexpr float kEqDefaultQ = 1.0f;
    constexpr int kEqLegacyLow = 0, kEqLegacyMid = 3, kEqLegacyHigh = 7;

    constexpr float kEqMaxDb = 15.0f;                 // gain range +/-15 dB
    constexpr float kEqMinQ = 0.3f, kEqMaxQ = 10.0f;  // bells only; shelves use a fixed slope

    constexpr float eqMinHz (int band) noexcept { return band == 0 ? 30.0f : band == kEqBands - 1 ? 1000.0f : 20.0f; }
    constexpr float eqMaxHz (int band) noexcept { return band == 0 ? 1000.0f : band == kEqBands - 1 ? 16000.0f : 20000.0f; }

    // Short names for keys and readouts: LS, 2..7, HS.
    inline const char* eqBandShortName (int band) noexcept
    {
        static const char* names[kEqBands] { "LS", "2", "3", "4", "5", "6", "7", "HS" };
        return band >= 0 && band < kEqBands ? names[band] : "";
    }
}
