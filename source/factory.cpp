#include "jamtaba_wrapper.h"
#include "plugids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

BEGIN_FACTORY_DEF(
    stringCompanyName,
    stringCompanyWeb,
    stringCompanyEmail)

    DEF_CLASS2(
        INLINE_UID_FROM_FUID(jtw::kJamTabaWrapperUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        stringPluginName,
        0, // SingleComponentEffect is non-distributable
        "Fx|Network",
        FULL_VERSION_STR,
        kVstVersionString,
        jtw::JamTabaWrapperEffect::createInstance)

END_FACTORY
