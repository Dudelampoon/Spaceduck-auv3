# SpaceDuck — TrackSpacer-style spectral sidechain ducker AUv3 (personal use)
#
# Build for iOS (unsigned, for SideStore):
#   cmake -B build-ios -G Xcode \
#       -DCMAKE_SYSTEM_NAME=iOS \
#       -DCMAKE_OSX_SYSROOT=iphoneos \
#       -DCMAKE_OSX_ARCHITECTURES=arm64 \
#       -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
#       -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
#       -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
#       -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=""
#   cmake --build build-ios --config Release --target SpaceDuck_Standalone \
#       -- CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=""
#
# See .github/workflows/build.yml for the exact CI invocation and README.md
# for the design decisions (JUCE pin, licensing, bus layout, LINK mode).

cmake_minimum_required(VERSION 3.22)

project(SpaceDuck VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE (pinned — see README "JUCE version" for why 9.0.5) ---
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- The plugin ---
# FORMATS: AUv3 (the extension Cubasis/AUM load) + Standalone (the
# containing iOS app SideStore installs — the .appex lives inside it).
# AUv3 requires the Xcode generator; Ninja/Make silently drop the AUv3 target.
juce_add_plugin(SpaceDuck
    VERSION 1.0.0
    COMPANY_NAME "Dude Lampoon"
    BUNDLE_ID com.dudelampoon.spaceduck
    PLUGIN_MANUFACTURER_CODE DudL
    PLUGIN_CODE Spdu
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AUv3 Standalone
    PRODUCT_NAME "SpaceDuck"
)

target_sources(SpaceDuck PRIVATE
    Source/DSP/SpectralDucker.cpp
    Source/Plugin/SpaceDuckAudioProcessor.cpp
    Source/Plugin/SpaceDuckEditor.cpp
)

target_include_directories(SpaceDuck PRIVATE Source)

# juce_audio_utils pulls in audio_processors, gui_basics, etc.
target_link_libraries(SpaceDuck PRIVATE juce::juce_audio_utils)

# Keep the AUv3 extension's bundle ID on JUCE's default derivation:
# com.dudelampoon.spaceduck.spaceduckAUv3 (app ID + one suffix component),
# which satisfies Apple's prefix rule. Do NOT override it with
# XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER — JUCEAide generates the
# extension Info.plist from BUNDLE_ID and the override is ignored there.
