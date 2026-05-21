#ifndef PCMFLOWG722_H
#define PCMFLOWG722_H

// Umbrella header for PCMFlowG722.
//
// Including this single header gives the user the public API surface of
// the optional G.722 wideband codec add-on for PCMFlow:
//
//   - G722Encoder : 16-bit PCM (16 kHz mono) -> 8-bit G.722 byte (implements PCMSink)
//   - G722Decoder : 8-bit G.722 byte         -> 16-bit PCM (16 kHz mono) (implements PCMSource)
//
// Only the ITU-T G.722 default operating mode (Mode 1 / 64 kbps) is
// exposed in v0.1.x. Mode 2 (56 kbps) and Mode 3 (48 kbps) are not
// exposed; see SPEC.md "Deferred features".
//
// The codec core is vendored under src/external/libg722/ from
// sippy/libg722 (public-domain G.722 implementation by Steve Underwood
// and CMU); see src/external/LICENSE_libg722.md for credits.

#include "pcmflowg722_version.h"
#include "G722Encoder.h"
#include "G722Decoder.h"

#endif // PCMFLOWG722_H
