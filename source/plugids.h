#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace jtw {

// Unique VST3 component UID for JamTaba 2 VST3 Wrapper
static const Steinberg::FUID kJamTabaWrapperUID(
    0x6A546162, 0x32577270, 0x9C4E41B2, 0x8F7D3E19);

enum WrapperParamIds : Steinberg::Vst::ParamID {
    kBypassParamId = 0
};

} // namespace jtw
