//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderDefaults.cpp
/// @brief Implements independent rendering reset scopes without resetting playback.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/EditorRenderDefaults.h"

namespace lmx::app {

//======================================================================================================================
void resetRenderingGroup(EditorRenderSettings& settings, EditorRenderGroup group) {
    const EditorRenderSettings defaults;
    switch (group) {
    case EditorRenderGroup::Lighting:
        settings.localLightMode = defaults.localLightMode;
        settings.lightDebugView = defaults.lightDebugView;
        settings.lightCheck = defaults.lightCheck;
        break;
    case EditorRenderGroup::Exposure:
        settings.exposureEv = defaults.exposureEv;
        settings.autoExposureEnabled = defaults.autoExposureEnabled;
        settings.exposureLowPercentile = defaults.exposureLowPercentile;
        settings.exposureHighPercentile = defaults.exposureHighPercentile;
        settings.exposureTargetGrey = defaults.exposureTargetGrey;
        settings.exposureEvMin = defaults.exposureEvMin;
        settings.exposureEvMax = defaults.exposureEvMax;
        settings.exposureCompensationEv = defaults.exposureCompensationEv;
        settings.exposureAdaptUpStopsPerSecond = defaults.exposureAdaptUpStopsPerSecond;
        settings.exposureAdaptDownStopsPerSecond = defaults.exposureAdaptDownStopsPerSecond;
        break;
    case EditorRenderGroup::Bloom:
        settings.bloomEnabled = defaults.bloomEnabled;
        settings.bloomThreshold = defaults.bloomThreshold;
        settings.bloomIntensity = defaults.bloomIntensity;
        break;
    case EditorRenderGroup::Shadows:
        settings.shadowFilter = defaults.shadowFilter;
        break;
    case EditorRenderGroup::Reconstruction:
        settings.temporalEnabled = defaults.temporalEnabled;
        settings.jitterEnabled = defaults.jitterEnabled;
        settings.reconstruction = defaults.reconstruction;
        settings.temporalDebugView = defaults.temporalDebugView;
        break;
    case EditorRenderGroup::Resolution:
        settings.renderScale = defaults.renderScale;
        settings.dynamicResolutionEnabled = defaults.dynamicResolutionEnabled;
        settings.gpuBudgetMilliseconds = defaults.gpuBudgetMilliseconds;
        break;
    case EditorRenderGroup::Display:
        settings.wireframe = defaults.wireframe;
        settings.poolingEnabled = defaults.poolingEnabled;
        break;
    }
}

//======================================================================================================================
bool renderingGroupChanged(const EditorRenderSettings& settings, EditorRenderGroup group) {
    const EditorRenderSettings defaults;
    switch (group) {
    case EditorRenderGroup::Lighting:
        return settings.localLightMode != defaults.localLightMode ||
               settings.lightDebugView != defaults.lightDebugView ||
               settings.lightCheck != defaults.lightCheck;
    case EditorRenderGroup::Exposure:
        return settings.exposureEv != defaults.exposureEv ||
               settings.autoExposureEnabled != defaults.autoExposureEnabled ||
               settings.exposureLowPercentile != defaults.exposureLowPercentile ||
               settings.exposureHighPercentile != defaults.exposureHighPercentile ||
               settings.exposureTargetGrey != defaults.exposureTargetGrey ||
               settings.exposureEvMin != defaults.exposureEvMin ||
               settings.exposureEvMax != defaults.exposureEvMax ||
               settings.exposureCompensationEv != defaults.exposureCompensationEv ||
               settings.exposureAdaptUpStopsPerSecond != defaults.exposureAdaptUpStopsPerSecond ||
               settings.exposureAdaptDownStopsPerSecond != defaults.exposureAdaptDownStopsPerSecond;
    case EditorRenderGroup::Bloom:
        return settings.bloomEnabled != defaults.bloomEnabled ||
               settings.bloomThreshold != defaults.bloomThreshold ||
               settings.bloomIntensity != defaults.bloomIntensity;
    case EditorRenderGroup::Shadows:
        return settings.shadowFilter != defaults.shadowFilter;
    case EditorRenderGroup::Reconstruction:
        return settings.temporalEnabled != defaults.temporalEnabled ||
               settings.jitterEnabled != defaults.jitterEnabled ||
               settings.reconstruction != defaults.reconstruction ||
               settings.temporalDebugView != defaults.temporalDebugView;
    case EditorRenderGroup::Resolution:
        return settings.renderScale != defaults.renderScale ||
               settings.dynamicResolutionEnabled != defaults.dynamicResolutionEnabled ||
               settings.gpuBudgetMilliseconds != defaults.gpuBudgetMilliseconds;
    case EditorRenderGroup::Display:
        return settings.wireframe != defaults.wireframe ||
               settings.poolingEnabled != defaults.poolingEnabled;
    }
    return false;
}

} // namespace lmx::app
