// Registers the Essentia algorithms that are compiled into essentia_key (see
// plugin/cmake/Essentia.cmake). Replaces the file that Essentia's waf build generates
// (src/algorithms/essentia_algorithms_reg.cpp) with only the algorithms needed for key detection.
//
// This file is part of audio-key-detector, Copyright (C) 2026 Tobias Barthold, and is licensed
// under the GNU Affero General Public License v3.0, like Essentia itself.

// FFTA (Accelerate) must come first: Accelerate's headers conflict with Essentia's names.
#include "algorithms/standard/ffta.h"

#include "algorithmfactory.h"
#include "algorithms/complex/magnitude.h"
#include "algorithms/spectral/hpcp.h"
#include "algorithms/spectral/spectralpeaks.h"
#include "algorithms/spectral/spectralwhitening.h"
#include "algorithms/standard/framecutter.h"
#include "algorithms/standard/peakdetection.h"
#include "algorithms/standard/spectrum.h"
#include "algorithms/standard/windowing.h"
#include "algorithms/tonal/key.h"

namespace essentia {
namespace standard {

ESSENTIA_API void registerAlgorithm() {
  AlgorithmFactory::Registrar<FFTA> regFFTA;      // registered under the name "FFT"
  AlgorithmFactory::Registrar<FrameCutter> regFrameCutter;
  AlgorithmFactory::Registrar<Windowing> regWindowing;
  AlgorithmFactory::Registrar<Spectrum> regSpectrum;
  AlgorithmFactory::Registrar<Magnitude> regMagnitude;
  AlgorithmFactory::Registrar<PeakDetection> regPeakDetection;
  AlgorithmFactory::Registrar<SpectralPeaks> regSpectralPeaks;
  AlgorithmFactory::Registrar<SpectralWhitening> regSpectralWhitening;
  AlgorithmFactory::Registrar<HPCP> regHPCP;
  AlgorithmFactory::Registrar<Key> regKey;
}

}  // namespace standard

namespace streaming {

// Only standard-mode algorithms are used.
ESSENTIA_API void registerAlgorithm() {}

}  // namespace streaming
}  // namespace essentia
