// SPDX-License-Identifier: AGPL-3.0-only
#include "app/bindings.h"
#include <algorithm>

namespace app::bindings {
namespace {

using S = Screen;
using T = Track;
using C = Control;
using G = Gesture;
using J = Job;
using St = Status;
namespace H = held;

constexpr uint32_t step(int n) { return 1u << (n - 1); } // Step 1-16
constexpr uint32_t right_16 = 0xf0f0f0f0u;                 // columns 5-8 of every row
constexpr uint32_t left_16 = 0x0f0f0f0fu;                  // columns 1-4 of every row

const char* const sequencer = "sequencer", * const sampling = "sampling", * const kit_depth = "kit depth",
                * const instruments = "instruments", * const mixer = "mixer & effects", * const song = "song & export",
                * const looper = "looper", * const projects = "MIDI/sync/undo/projects";

// docs/MOVE_CONTROLS.md, "The map", in its order. A doc row of two
// controls ("Plus or Minus") is a row each.
std::vector<Binding> make_table() {
    return {
        // Pads
        {"Pad (Play, kit)", S::Play, T::Kit, H::none, C::Pad, G::Press, J::PlayPad, St::Done},
        {"Pad (Play, synth)", S::Play, T::Synth, H::none, C::Pad, G::Press, J::PlayNote, St::Done},
        {"Pad, right 16 (16 Pitches)", S::Play, T::Pitches, H::none, C::Pad, G::Press, J::PlayPitch, St::Done, "", right_16},
        {"Pad pressure", S::Play, T::Synth, H::none, C::PadPressure, G::Turn, J::Aftertouch, St::Done},
        {"Shift + Pad (Play, kit)", S::Play, T::Kit, H::shift, C::Pad, G::Press, J::SelectPadQuietly, St::Done},
        {"Pad (Browser)", S::Browser, T::Kit, H::none, C::Pad, G::Press, J::PlayPad, St::Done},
        {"Pad (Chop)", S::Chop, T::Any, H::none, C::Pad, G::Press, J::PlaySlice, St::Done},
        {"Pad (Typing)", S::Typing, T::Any, H::none, C::Pad, G::Press, J::TypeKey, St::Done},
        {"Pad (Sampling)", S::Sampling, T::Any, H::none, C::Pad, G::Press, J::RecordPad, St::Done},
        {"Pad let go (Sampling)", S::Sampling, T::Any, H::none, C::Pad, G::Release, J::RecordPadUp, St::Done},
        {"Pad (Session)", S::Session, T::Any, H::none, C::Pad, G::Press, J::LaunchPattern, St::Done},
        {"Pad held + Pad (Session)", S::Session, T::Any, H::pad, C::Pad, G::Press, J::ChainPattern, St::Done},
        {"Shift + Pad (Session)", S::Session, T::Any, H::shift, C::Pad, G::Press, J::PasteBounced, St::Wave2, song},
        {"Pad (Overview)", S::Overview, T::Any, H::none, C::Pad, G::Press, J::SelectProject, St::Done},
        {"Shift + Pad (Overview)", S::Overview, T::Any, H::shift, C::Pad, G::Press, J::ProjectOptions, St::Done},
        {"Pad held + Volume knob", S::Play, T::Kit, H::pad, C::Volume, G::Turn, J::PadLevel, St::Done},
        {"Pad held + Volume knob (Overview)", S::Overview, T::Any, H::pad, C::Volume, G::Turn, J::ProjectVolume, St::Done},
        {"Copy held + Pad (Play, kit)", S::Play, T::Kit, H::copy, C::Pad, G::Press, J::CopyPad, St::Done},
        {"Copy held + Pad (Session)", S::Session, T::Any, H::copy, C::Pad, G::Press, J::CopyPattern, St::Done},
        {"Copy held + Pad (Overview)", S::Overview, T::Any, H::copy, C::Pad, G::Press, J::CopyProject, St::Done},
        {"Delete held + Pad (Play, kit)", S::Play, T::Kit, H::del, C::Pad, G::Press, J::ClearPad, St::Done},
        {"Delete held + Pad (Session)", S::Session, T::Any, H::del, C::Pad, G::Press, J::DeletePattern, St::Done},
        {"Delete held + Pad (Overview)", S::Overview, T::Any, H::del, C::Pad, G::Press, J::DeleteProject, St::Done},
        {"Delete held + Pad (Chop)", S::Chop, T::Any, H::del, C::Pad, G::Press, J::RemoveCut, St::Done},
        {"Mute held + Pad (Play, kit)", S::Play, T::Kit, H::mute, C::Pad, G::Press, J::MutePad, St::Done},
        {"Shift + Mute held + Pad (Play, kit)", S::Play, T::Kit, H::shift | H::mute, C::Pad, G::Press, J::SoloPad, St::Done},
        {"Pad (Knob FX, kit)", S::KnobFx, T::Kit, H::none, C::Pad, G::Press, J::KnobFxPad, St::Done},
        {"Pad (Pad FX)", S::PadFx, T::Any, H::none, C::Pad, G::Press, J::Punch, St::Done},
        {"Delete held + Pad (Pad FX)", S::PadFx, T::Any, H::del, C::Pad, G::Press, J::ClearKept, St::Done},
        {"Pad (Flex Beat)", S::Flex, T::Any, H::none, C::Pad, G::Press, J::FlexPad, St::Done},
        {"Step held + Pad", S::Play, T::Any, H::step, C::Pad, G::Press, J::NoteOnStep, St::Done},
        {"Step held + Pad (Loop)", S::Loop, T::Any, H::step, C::Pad, G::Press, J::NoteOnBar, St::Done},
        {"Shift + Step 16 held + Pad", S::Play, T::Any, H::shift | H::step, C::Pad, G::Press, J::QuantizePad, St::Done, "", 0, 15},

        // Steps
        {"Step (Play)", S::Play, T::Any, H::none, C::Step, G::Tap, J::ToggleStep, St::Done},
        {"Step held + Volume knob", S::Play, T::Any, H::step, C::Volume, G::Turn, J::StepVelocity, St::Done},
        {"Step held + Wheel turn", S::Play, T::Any, H::step, C::WheelTurn, G::Turn, J::StepLength, St::Done},
        {"Step held + Step", S::Play, T::Any, H::step, C::Step, G::Press, J::StepLengthTo, St::Done},
        {"Step held + Plus or Minus", S::Play, T::Any, H::step, C::Plus, G::Tap, J::TransposeStep, St::Done},
        {"Step held + Plus or Minus", S::Play, T::Any, H::step, C::Minus, G::Tap, J::TransposeStep, St::Done},
        {"Step held + long Plus or Minus", S::Play, T::Any, H::step, C::Plus, G::Hold, J::TransposeStepOctave, St::Done},
        {"Step held + long Plus or Minus", S::Play, T::Any, H::step, C::Minus, G::Hold, J::TransposeStepOctave, St::Done},
        {"Step held + Left or Right", S::Play, T::Any, H::step, C::Left, G::Tap, J::NudgeStep, St::Done},
        {"Step held + Left or Right", S::Play, T::Any, H::step, C::Right, G::Tap, J::NudgeStep, St::Done},
        {"Shift + Step held + Left or Right", S::Play, T::Any, H::shift | H::step, C::Left, G::Press, J::NudgeStepFine, St::Done},
        {"Shift + Step held + Left or Right", S::Play, T::Any, H::shift | H::step, C::Right, G::Press, J::NudgeStepFine, St::Done},
        {"Step held + long Left or Right", S::Play, T::Any, H::step, C::Left, G::Hold, J::NudgeStepWhole, St::Done},
        {"Step held + long Left or Right", S::Play, T::Any, H::step, C::Right, G::Hold, J::NudgeStepWhole, St::Done},
        {"Step held + Knob", S::Play, T::Any, H::step, C::Knob, G::Turn, J::StepMotion, St::Done},
        {"Step held + Wheel push", S::Play, T::Any, H::step, C::WheelPush, G::Press, J::StepTrig, St::Done},
        {"Step (Loop)", S::Loop, T::Any, H::none, C::Step, G::Tap, J::SelectBar, St::Done},
        {"Step held + Step (Loop)", S::Loop, T::Any, H::step, C::Step, G::Press, J::LoopRange, St::Done},
        {"Step double press (Loop)", S::Loop, T::Any, H::none, C::Step, G::Double, J::OneBarLoop, St::Done},
        {"Step held + Wheel turn (Loop)", S::Loop, T::Any, H::step, C::WheelTurn, G::Turn, J::BarLength, St::Done},
        {"Step held + Volume knob (Loop)", S::Loop, T::Any, H::step, C::Volume, G::Turn, J::BarVelocity, St::Done},
        {"Step held + Plus or Minus (Loop)", S::Loop, T::Any, H::step, C::Plus, G::Tap, J::TransposeBar, St::Done},
        {"Step held + Plus or Minus (Loop)", S::Loop, T::Any, H::step, C::Minus, G::Tap, J::TransposeBar, St::Done},
        {"Step held + Left or Right (Loop)", S::Loop, T::Any, H::step, C::Left, G::Tap, J::NudgeBar, St::Done},
        {"Step held + Left or Right (Loop)", S::Loop, T::Any, H::step, C::Right, G::Tap, J::NudgeBar, St::Done},
        {"Copy held + Step", S::Play, T::Any, H::copy, C::Step, G::Press, J::CopyStep, St::Done},
        {"Copy held + Step (Loop)", S::Loop, T::Any, H::copy, C::Step, G::Press, J::CopyBar, St::Done},
        {"Delete held + Step (Loop)", S::Loop, T::Any, H::del, C::Step, G::Press, J::ClearBar, St::Done},
        {"Step (Chop)", S::Chop, T::Any, H::none, C::Step, G::Press, J::PlayFromPart, St::Done},
        {"Step (Typing)", S::Typing, T::Any, H::none, C::Step, G::Press, J::TypeDigit, St::Done},
        {"Track button held + Step", S::Any, T::Any, H::track, C::Step, G::Press, J::PickTrack, St::Done},
        {"Track button held + Mute held + Step", S::Any, T::Any, H::track | H::mute, C::Step, G::Press, J::MuteStep, St::Done},
        {"Track button held + Shift + Mute held + Step", S::Any, T::Any, H::track | H::shift | H::mute, C::Step, G::Press, J::SoloStep, St::Done},
        {"Step (Mixer)", S::Mixer, T::Any, H::none, C::Step, G::Tap, J::PickTrack, St::Done},
        {"Mute held + Step (Mixer)", S::Mixer, T::Any, H::mute, C::Step, G::Press, J::MuteStep, St::Done},
        {"Shift + Mute held + Step (Mixer)", S::Mixer, T::Any, H::shift | H::mute, C::Step, G::Press, J::SoloStep, St::Done},
        {"Step (Knob FX)", S::KnobFx, T::Any, H::none, C::Step, G::Tap, J::KnobFxTrack, St::Done},

        // Shift + steps
        {"Shift + Step 1", S::Any, T::Any, H::shift, C::Step, G::Press, J::Projects, St::Done, "", step(1)},
        {"Shift + Step 2", S::Any, T::Any, H::shift, C::Step, G::Press, J::Setup, St::Done, "", step(2)},
        {"Shift + Step 3", S::Any, T::Any, H::shift, C::Step, G::Press, J::Workflow, St::Done, "", step(3)},
        {"Shift + Step 4", S::Any, T::Any, H::shift, C::Step, G::Press, J::Mixer, St::Done, "", step(4)},
        {"Shift + Step 5", S::Any, T::Any, H::shift, C::Step, G::Press, J::Tempo, St::Done, "", step(5)},
        {"Shift + Step 6", S::Any, T::Any, H::shift, C::Step, G::Press, J::Metronome, St::Done, "", step(6)},
        {"Shift + Step 7", S::Any, T::Any, H::shift, C::Step, G::Press, J::Groove, St::Done, "", step(7)},
        {"Shift + Step 8", S::Any, T::Any, H::shift, C::Step, G::Press, J::SixteenPitches, St::Done, "", step(8)},
        {"Shift + Step 9", S::Any, T::Any, H::shift, C::Step, G::Press, J::KeysAndScales, St::Done, "", step(9)},
        {"Shift + Step 10", S::Any, T::Any, H::shift, C::Step, G::Press, J::FullVelocity, St::Done, "", step(10)},
        {"Shift + Step 11", S::Any, T::Any, H::shift, C::Step, G::Press, J::Repeat, St::Done, "", step(11)},
        {"Shift + Step 12", S::Any, T::Any, H::shift, C::Step, G::Press, J::Looper, St::Done, "", step(12)},
        {"Shift + Step 13", S::Any, T::Any, H::shift, C::Step, G::Press, J::Song, St::Done, song, step(13)},
        {"Shift + Step 14", S::Any, T::Any, H::shift, C::Step, G::Press, J::NewPattern, St::Done, "", step(14)},
        {"Shift + Step 15", S::Any, T::Any, H::shift, C::Step, G::Press, J::DoubleLoop, St::Done, "", step(15)},
        {"Shift + Step 16", S::Any, T::Any, H::shift, C::Step, G::Tap, J::Quantize, St::Done, "", step(16)}, // a tap: held, a pad picks one lane

        // Track buttons
        {"Track button", S::Any, T::Any, H::none, C::TrackButton, G::Tap, J::SelectTrack, St::Done},
        {"Track button held", S::Any, T::Any, H::none, C::TrackButton, G::Hold, J::PeekTrack, St::Done},
        {"Track button held + Volume knob", S::Any, T::Any, H::track, C::Volume, G::Turn, J::TrackLevel, St::Done},
        {"Shift + Track button", S::Any, T::Any, H::shift, C::TrackButton, G::Press, J::TrackOptions, St::Done},
        {"Mute held + Track button", S::Any, T::Any, H::mute, C::TrackButton, G::Press, J::MuteTrack, St::Done},
        {"Shift + Mute held + Track button", S::Any, T::Any, H::shift | H::mute, C::TrackButton, G::Press, J::SoloTrack, St::Done},
        {"Track button held + Copy held + Step", S::Any, T::Any, H::track | H::copy, C::Step, G::Press, J::CopyTrack, St::Done},
        {"Track button held + Delete held + Step", S::Any, T::Any, H::track | H::del, C::Step, G::Press, J::ClearTrack, St::Done},
        {"Step (Tracks)", S::Tracks, T::Any, H::none, C::Step, G::Press, J::PickTrack, St::Done},
        {"Copy held + Step (Tracks)", S::Tracks, T::Any, H::copy, C::Step, G::Press, J::CopyTrack, St::Done},
        {"Delete held + Step (Tracks)", S::Tracks, T::Any, H::del, C::Step, G::Press, J::ClearTrack, St::Done},
        {"Mute held + Step (Tracks)", S::Tracks, T::Any, H::mute, C::Step, G::Press, J::MuteStep, St::Done},
        {"Shift + Mute held + Step (Tracks)", S::Tracks, T::Any, H::shift | H::mute, C::Step, G::Press, J::SoloStep, St::Done},
        {"Track button held + Shift + Copy held + Step", S::Any, T::Any, H::track | H::shift | H::copy, C::Step, G::Press, J::MoveTrack, St::Done},
        {"Shift + Copy held + Step (Tracks)", S::Tracks, T::Any, H::shift | H::copy, C::Step, G::Press, J::MoveTrack, St::Done},

        // Knobs
        {"Knob", S::Any, T::Any, H::none, C::Knob, G::Turn, J::TurnKnob, St::Done},
        {"Knob touch", S::Any, T::Any, H::none, C::KnobTouch, G::Press, J::ShowKnob, St::Done},
        {"Shift + Knob", S::Any, T::Any, H::shift, C::Knob, G::Turn, J::TurnKnobFine, St::Done},
        {"Shift + Knob touch", S::Any, T::Any, H::shift, C::KnobTouch, G::Press, J::SecondParameter, St::Done},
        {"Delete held + Knob touch", S::Any, T::Any, H::del, C::KnobTouch, G::Press, J::DeleteMotion, St::Done},
        {"Mute held + Knob touch", S::Any, T::Any, H::mute, C::KnobTouch, G::Press, J::ToggleMotion, St::Done},
        {"Knob (Chop)", S::Chop, T::Any, H::none, C::Knob, G::Turn, J::TurnKnob, St::Done},
        {"Knob (Session)", S::Session, T::Any, H::none, C::Knob, G::Turn, J::TurnMainEffect, St::Wave2, mixer},

        // Volume knob
        {"Volume knob", S::Everywhere, T::Any, H::none, C::Volume, G::Turn, J::MainVolume, St::Done},
        {"Volume touch", S::Any, T::Any, H::none, C::VolumeTouch, G::Press, J::ShowMeter, St::Done},

        // The wheel
        {"Wheel turn (Play)", S::Play, T::Any, H::none, C::WheelTurn, G::Turn, J::Focus, St::Done},
        {"Wheel push (Play)", S::Play, T::Any, H::none, C::WheelPush, G::Press, J::OpenBrowser, St::Done},
        {"Shift + Wheel push (Play, kit)", S::Play, T::Kit, H::shift, C::WheelPush, G::Press, J::SampleOptions, St::Done},
        {"Shift + Wheel push (Play, synth)", S::Play, T::Synth, H::shift, C::WheelPush, G::Press, J::ModuleMenu, St::Done},
        {"Shift + Wheel turn (Play)", S::Play, T::Any, H::shift, C::WheelTurn, G::Turn, J::Page, St::Done},
        {"Wheel touch (Play)", S::Play, T::Any, H::none, C::WheelTouch, G::Press, J::ShowFocus, St::Done},
        {"Mute held + Wheel push (Play)", S::Play, T::Any, H::mute, C::WheelPush, G::Press, J::BypassEffect, St::Done},
        {"Delete held + Wheel push (Play)", S::Play, T::Any, H::del, C::WheelPush, G::Press, J::RemoveEffect, St::Done},
        {"Copy held + Wheel push (Play)", S::Play, T::Any, H::copy, C::WheelPush, G::Press, J::CopyEffect, St::Done},
        {"Copy held + Wheel turn (Play)", S::Play, T::Any, H::copy, C::WheelTurn, G::Turn, J::Focus, St::Done},
        {"Wheel turn (Mixer)", S::Mixer, T::Any, H::none, C::WheelTurn, G::Turn, J::MixerPage, St::Done},
        {"Wheel push (Mixer)", S::Mixer, T::Any, H::none, C::WheelPush, G::Press, J::OpenChain, St::Done},
        {"Wheel turn (Knob FX)", S::KnobFx, T::Any, H::none, C::WheelTurn, G::Turn, J::KnobFxChoose, St::Done},
        {"Wheel push (Knob FX)", S::KnobFx, T::Any, H::none, C::WheelPush, G::Press, J::KnobFxLoad, St::Done},
        {"Shift + Wheel push (Knob FX)", S::KnobFx, T::Any, H::shift, C::WheelPush, G::Press, J::KnobFxAll, St::Done},
        {"Mute held + Wheel push (Knob FX)", S::KnobFx, T::Any, H::mute, C::WheelPush, G::Press, J::BypassEffect, St::Done},
        {"Wheel turn (Pad FX)", S::PadFx, T::Any, H::none, C::WheelTurn, G::Turn, J::PunchBank, St::Done},
        {"Wheel push (Pad FX)", S::PadFx, T::Any, H::none, C::WheelPush, G::Press, J::KeepPunches, St::Done},
        {"Shift + Wheel push (Pad FX)", S::PadFx, T::Any, H::shift, C::WheelPush, G::Press, J::PunchOptions, St::Done},
        {"Wheel turn (Browser)", S::Browser, T::Any, H::none, C::WheelTurn, G::Turn, J::BrowseTurn, St::Done},
        {"Wheel push (Browser)", S::Browser, T::Any, H::none, C::WheelPush, G::Press, J::BrowsePush, St::Done},
        {"Shift + Wheel push (Browser)", S::Browser, T::Any, H::shift, C::WheelPush, G::Press, J::StarFile, St::Done},
        {"Shift held (Browser)", S::Browser, T::Any, H::none, C::Shift, G::Hold, J::BrowserOptions, St::Done},
        {"Wheel turn (list)", S::List, T::Any, H::none, C::WheelTurn, G::Turn, J::ListTurn, St::Done},
        {"Shift + Wheel turn (list)", S::List, T::Any, H::shift, C::WheelTurn, G::Turn, J::ListTurnFine, St::Done},
        {"Wheel push (list)", S::List, T::Any, H::none, C::WheelPush, G::Press, J::ListPush, St::Done},
        {"Pad held + Wheel push (list)", S::List, T::Any, H::pad, C::WheelPush, G::Press, J::ListPush, St::Done},
        {"Wheel push (Tracks)", S::Tracks, T::Any, H::none, C::WheelPush, G::Press, J::ListPush, St::Done}, // a track's question: yes
        {"Wheel turn (Chop)", S::Chop, T::Any, H::none, C::WheelTurn, G::Turn, J::ChopAmount, St::Done},
        {"Wheel push (Chop)", S::Chop, T::Any, H::none, C::WheelPush, G::Press, J::ToPads, St::Done},
        {"Shift + Wheel push (Chop)", S::Chop, T::Any, H::shift, C::WheelPush, G::Press, J::SliceOptions, St::Done},
        {"Shift + Wheel turn (Chop)", S::Chop, T::Any, H::shift, C::WheelTurn, G::Turn, J::JumpHit, St::Done},
        {"Wheel turn (To Pads)", S::ToPads, T::Any, H::none, C::WheelTurn, G::Turn, J::ToPadsChoose, St::Done},
        {"Wheel push (To Pads)", S::ToPads, T::Any, H::none, C::WheelPush, G::Press, J::ToPadsPlace, St::Done},
        {"Wheel turn (Sampling)", S::Sampling, T::Any, H::none, C::WheelTurn, G::Turn, J::InputTurn, St::Done},
        {"Wheel push (Sampling)", S::Sampling, T::Any, H::none, C::WheelPush, G::Press, J::InputPush, St::Done},
        {"Wheel touch (Sampling)", S::Sampling, T::Any, H::none, C::WheelTouch, G::Press, J::ShowSource, St::Done},
        {"Wheel turn (Canvas)", S::Canvas, T::Any, H::none, C::WheelTurn, G::Turn, J::CanvasTurn, St::Done},
        {"Wheel push (Canvas)", S::Canvas, T::Any, H::none, C::WheelPush, G::Press, J::CanvasPush, St::Done},
        {"Wheel push (Typing)", S::Typing, T::Any, H::none, C::WheelPush, G::Press, J::TypeDone, St::Done},
        {"Wheel turn (Session)", S::Session, T::Any, H::none, C::WheelTurn, G::Turn, J::Playhead, St::Done},
        {"Wheel push (Session)", S::Session, T::Any, H::none, C::WheelPush, G::Press, J::LaunchScene, St::Done},
        {"Loop held + Wheel turn", S::Play, T::Any, H::loop, C::WheelTurn, G::Turn, J::LoopLength, St::Done},
        {"Shift + Loop held + Wheel turn", S::Play, T::Any, H::shift | H::loop, C::WheelTurn, G::Turn, J::LoopLengthFine, St::Done},
        {"Wheel turn (Typing)", S::Typing, T::Any, H::none, C::WheelTurn, G::Turn, J::TypeTurn, St::Done},
        {"Wheel turn (Overview)", S::Overview, T::Any, H::none, C::WheelTurn, G::Turn, J::PickProject, St::Done},
        {"Wheel push (Overview)", S::Overview, T::Any, H::none, C::WheelPush, G::Press, J::ProjectOptions, St::Done},

        // Transport
        {"Play", S::Any, T::Any, H::none, C::Play, G::Press, J::PlayStop, St::Done},
        {"Shift + Play", S::Any, T::Any, H::shift, C::Play, G::Press, J::Restart, St::Done},
        {"Play (Overview)", S::Overview, T::Any, H::none, C::Play, G::Press, J::PreviewProject, St::Done},
        {"Rec", S::Any, T::Any, H::none, C::Rec, G::Press, J::Record, St::Done},
        {"Rec (Sampling)", S::Sampling, T::Audio, H::none, C::Rec, G::Press, J::RecordAudio, St::Done},

        // Capture and Sampling
        {"Capture", S::Any, T::Any, H::none, C::Capture, G::Press, J::Recall, St::Done},
        {"Shift + Capture", S::Any, T::Any, H::shift, C::Capture, G::Press, J::ClearRecall, St::Done},
        {"Sampling", S::Any, T::Any, H::none, C::Sampling, G::Press, J::SamplingMode, St::Done},
        {"Shift + Sampling", S::Any, T::Any, H::shift, C::Sampling, G::Press, J::AudioRecall, St::Done},

        // Loop, Mute, Undo, Copy, Delete
        {"Loop", S::Play, T::Any, H::none, C::Loop, G::Tap, J::LoopMode, St::Done},
        {"Shift + Loop", S::Any, T::Any, H::shift, C::Loop, G::Tap, J::SeqPages, St::Done}, // a tap: held with the wheel, the length
        {"Loop held", S::Play, T::Any, H::none, C::Loop, G::Hold, J::Fill, St::Done},
        {"Mute", S::Play, T::Any, H::none, C::Mute, G::Tap, J::MuteSelected, St::Done},
        {"Shift + Mute", S::Any, T::Any, H::shift, C::Mute, G::Tap, J::UnmuteAll, St::Done},
        {"Undo", S::Any, T::Any, H::none, C::Undo, G::Tap, J::Undo, St::Done},
        {"Shift + Undo", S::Any, T::Any, H::shift, C::Undo, G::Press, J::Redo, St::Done},
        {"Undo held", S::Any, T::Any, H::none, C::Undo, G::Hold, J::UndoList, St::Done},
        {"Copy", S::Play, T::Any, H::none, C::Copy, G::Tap, J::DuplicatePattern, St::Done},
        {"Shift + Copy", S::Any, T::Any, H::shift, C::Copy, G::Press, J::Nothing, St::Free},
        {"Delete", S::Play, T::Any, H::none, C::Delete, G::Tap, J::DeleteCurrentPattern, St::Done},
        {"Delete (Typing)", S::Typing, T::Any, H::none, C::Delete, G::Press, J::TypeBackspace, St::Done},
        {"Delete (Browser)", S::Browser, T::Any, H::none, C::Delete, G::Tap, J::DeleteFile, St::Done},
        {"Shift + Delete", S::Any, T::Any, H::shift, C::Delete, G::Press, J::Nothing, St::Free},
        {"Delete (Overview)", S::Overview, T::Any, H::none, C::Delete, G::Tap, J::DeleteSelection, St::Done},

        // Note/Session, Back, Shift
        {"Note/Session", S::Any, T::Any, H::none, C::NoteSession, G::Tap, J::ToggleSession, St::Done},
        {"Note/Session held", S::Any, T::Any, H::none, C::NoteSession, G::Hold, J::PeekSession, St::Done},
        {"Shift + Note/Session", S::Any, T::Any, H::shift, C::NoteSession, G::Press, J::TracksOverview, St::Done},
        {"Back", S::Any, T::Any, H::none, C::Back, G::Press, J::Back, St::Done},
        {"Back held", S::Any, T::Any, H::none, C::Back, G::Hold, J::BackToPlay, St::Done},
        {"Back (Chop)", S::Chop, T::Any, H::none, C::Back, G::Press, J::LeaveChop, St::Done},
        {"Back (Canvas)", S::Canvas, T::Any, H::none, C::Back, G::Press, J::CanvasBack, St::Done},
        {"Back (Typing)", S::Typing, T::Any, H::none, C::Back, G::Press, J::TypeBackspace, St::Done},
        {"Back held (Typing)", S::Typing, T::Any, H::none, C::Back, G::Hold, J::TypeCancel, St::Done},
        {"Shift + Back", S::Everywhere, T::Any, H::shift, C::Back, G::Press, J::Quit, St::Done},
        {"Shift held", S::Any, T::Any, H::none, C::Shift, G::Press, J::ShiftSteps, St::Done},
        {"Shift double press", S::Any, T::Any, H::none, C::Shift, G::Double, J::ShiftLock, St::Done},
        {"Shift held (Typing)", S::Typing, T::Any, H::none, C::Shift, G::Press, J::Uppercase, St::Done},

        // Plus and Minus, Left and Right
        {"Plus or Minus (Play, synth)", S::Play, T::Synth, H::none, C::Plus, G::Press, J::Octave, St::Done},
        {"Plus or Minus (Play, synth)", S::Play, T::Synth, H::none, C::Minus, G::Press, J::Octave, St::Done},
        {"Plus or Minus (Play, kit)", S::Play, T::Kit, H::none, C::Plus, G::Press, J::PadBank, St::Done},
        {"Plus or Minus (Play, kit)", S::Play, T::Kit, H::none, C::Minus, G::Press, J::PadBank, St::Done},
        {"Plus or Minus (16 Pitches)", S::Play, T::Pitches, H::none, C::Plus, G::Press, J::PitchesOctave, St::Done},
        {"Plus or Minus (16 Pitches)", S::Play, T::Pitches, H::none, C::Minus, G::Press, J::PitchesOctave, St::Done},
        {"Shift + Plus or Minus (Play, synth)", S::Play, T::Synth, H::shift, C::Plus, G::Press, J::ScaleStep, St::Done},
        {"Shift + Plus or Minus (Play, synth)", S::Play, T::Synth, H::shift, C::Minus, G::Press, J::ScaleStep, St::Done},
        {"Shift + Plus or Minus (Play, kit)", S::Play, T::Kit, H::shift, C::Plus, G::Press, J::ScaleStep, St::Done},
        {"Shift + Plus or Minus (Play, kit)", S::Play, T::Kit, H::shift, C::Minus, G::Press, J::ScaleStep, St::Done},
        {"Plus or Minus (Session)", S::Session, T::Any, H::none, C::Plus, G::Press, J::TrackBank, St::Done},
        {"Plus or Minus (Session)", S::Session, T::Any, H::none, C::Minus, G::Press, J::TrackBank, St::Done},
        {"Plus or Minus (Chop)", S::Chop, T::Any, H::none, C::Plus, G::Press, J::SlicePage, St::Done},
        {"Plus or Minus (Chop)", S::Chop, T::Any, H::none, C::Minus, G::Press, J::SlicePage, St::Done},
        {"Left or Right (Play)", S::Play, T::Any, H::none, C::Left, G::Press, J::Bar, St::Done},
        {"Left or Right (Play)", S::Play, T::Any, H::none, C::Right, G::Press, J::Bar, St::Done},
        {"Shift + Left or Right", S::Play, T::Any, H::shift, C::Left, G::Press, J::Bar16, St::Done},
        {"Shift + Left or Right", S::Play, T::Any, H::shift, C::Right, G::Press, J::Bar16, St::Done},
        {"Left or Right (Session)", S::Session, T::Any, H::none, C::Left, G::Press, J::PatternPage, St::Done},
        {"Left or Right (Session)", S::Session, T::Any, H::none, C::Right, G::Press, J::PatternPage, St::Done},
        {"Left or Right (Chop)", S::Chop, T::Any, H::none, C::Left, G::Press, J::SliceStep, St::Done},
        {"Left or Right (Chop)", S::Chop, T::Any, H::none, C::Right, G::Press, J::SliceStep, St::Done},
        {"Left or Right (Mixer)", S::Mixer, T::Any, H::none, C::Left, G::Press, J::MixerHalf, St::Done},
        {"Left or Right (Mixer)", S::Mixer, T::Any, H::none, C::Right, G::Press, J::MixerHalf, St::Done},
        {"Plus or Minus (Pad FX)", S::PadFx, T::Any, H::none, C::Plus, G::Press, J::PunchBank, St::Done},
        {"Plus or Minus (Pad FX)", S::PadFx, T::Any, H::none, C::Minus, G::Press, J::PunchBank, St::Done},

        // The Looper (Shift + Step 12): a pad row and a track button a loop, L1 on top
        {"Pad, left 16 (Looper)", S::Looper, T::Any, H::none, C::Pad, G::Press, J::LoopPad, St::Done, "", left_16},
        {"Shift + Pad, left 16 (Looper)", S::Looper, T::Any, H::shift, C::Pad, G::Press, J::LoopPadNow, St::Done, "", left_16},
        {"Pad, right 16 (Looper, kit)", S::Looper, T::Kit, H::none, C::Pad, G::Press, J::PlayPad, St::Done, "", right_16},
        {"Pad, right 16 (Looper, synth)", S::Looper, T::Synth, H::none, C::Pad, G::Press, J::PlayNote, St::Done, "", right_16},
        {"Track button (Looper)", S::Looper, T::Any, H::none, C::TrackButton, G::Tap, J::SelectLoop, St::Done},
        {"Track button held + Volume knob (Looper)", S::Looper, T::Any, H::track, C::Volume, G::Turn, J::LoopLevel, St::Done},
        {"Mute (Looper)", S::Looper, T::Any, H::none, C::Mute, G::Tap, J::MuteLoop, St::Done},
        {"Mute held + Track button (Looper)", S::Looper, T::Any, H::mute, C::TrackButton, G::Press, J::MuteLoop, St::Done},
        {"Shift + Mute held + Track button (Looper)", S::Looper, T::Any, H::shift | H::mute, C::TrackButton, G::Press, J::SoloLoop, St::Done},
        {"Rec (Looper)", S::Looper, T::Any, H::none, C::Rec, G::Press, J::LoopRecord, St::Done},
        {"Wheel turn (Looper)", S::Looper, T::Any, H::none, C::WheelTurn, G::Turn, J::PickLoop, St::Done},
        {"Wheel push (Looper)", S::Looper, T::Any, H::none, C::WheelPush, G::Press, J::LoopSheet, St::Done},
        {"Shift + Wheel turn (Looper)", S::Looper, T::Any, H::shift, C::WheelTurn, G::Turn, J::Page, St::Done},
        {"Shift + Wheel push (Looper)", S::Looper, T::Any, H::shift, C::WheelPush, G::Press, J::Nothing, St::Free},
        {"Step (Looper)", S::Looper, T::Any, H::none, C::Step, G::Tap, J::Nothing, St::Free},
        {"Plus or Minus (Overview)", S::Overview, T::Any, H::none, C::Plus, G::Press, J::ProjectPage, St::Done},
        {"Plus or Minus (Overview)", S::Overview, T::Any, H::none, C::Minus, G::Press, J::ProjectPage, St::Done},

        // Song (Shift + Step 13): the pads four lanes of eight bars, the steps sixteen bars
        {"Pad (Song)", S::Song, T::Any, H::none, C::Pad, G::Press, J::SongPick, St::Done},
        {"Copy held + Pad (Song)", S::Song, T::Any, H::copy, C::Pad, G::Press, J::SongCopyClip, St::Done},
        {"Delete held + Pad (Song)", S::Song, T::Any, H::del, C::Pad, G::Press, J::SongDeleteClip, St::Done},
        {"Mute held + Pad (Song)", S::Song, T::Any, H::mute, C::Pad, G::Press, J::SongMuteClip, St::Done},
        {"Step (Song)", S::Song, T::Any, H::none, C::Step, G::Press, J::SongBar, St::Done},
        {"Step held + Step (Song)", S::Song, T::Any, H::step, C::Step, G::Press, J::SongLoopRange, St::Done},
        {"Wheel turn (Song)", S::Song, T::Any, H::none, C::WheelTurn, G::Turn, J::SongCursor, St::Done},
        {"Wheel push (Song)", S::Song, T::Any, H::none, C::WheelPush, G::Press, J::SongPush, St::Done},
        {"Shift + Wheel push (Song)", S::Song, T::Any, H::shift, C::WheelPush, G::Press, J::SongMarker, St::Done},
        {"Copy (Song)", S::Song, T::Any, H::none, C::Copy, G::Tap, J::SongDuplicate, St::Done},
        {"Delete (Song)", S::Song, T::Any, H::none, C::Delete, G::Tap, J::SongDelete, St::Done},
        {"Loop (Song)", S::Song, T::Any, H::none, C::Loop, G::Tap, J::SongLoopClip, St::Done},
        {"Mute (Song)", S::Song, T::Any, H::none, C::Mute, G::Tap, J::MuteSelected, St::Done},
        {"Plus or Minus (Song)", S::Song, T::Any, H::none, C::Plus, G::Press, J::TrackBank, St::Done},
        {"Plus or Minus (Song)", S::Song, T::Any, H::none, C::Minus, G::Press, J::TrackBank, St::Done},
        {"Left or Right (Song)", S::Song, T::Any, H::none, C::Left, G::Press, J::SongPage, St::Done},
        {"Left or Right (Song)", S::Song, T::Any, H::none, C::Right, G::Press, J::SongPage, St::Done},
        {"Shift + Left or Right (Song)", S::Song, T::Any, H::shift, C::Left, G::Press, J::SongMarkerJump, St::Done},
        {"Shift + Left or Right (Song)", S::Song, T::Any, H::shift, C::Right, G::Press, J::SongMarkerJump, St::Done},
        {"Play (Song)", S::Song, T::Any, H::none, C::Play, G::Press, J::PlaySong, St::Done},
        {"Shift + Play (Song)", S::Song, T::Any, H::shift, C::Play, G::Press, J::RestartSong, St::Done},
        {"Rec (Song)", S::Song, T::Any, H::none, C::Rec, G::Press, J::RecordSong, St::Done},
        {"Sampling (Song)", S::Song, T::Any, H::none, C::Sampling, G::Press, J::SongExport, St::Done},

        // Print and freeze: the PRINT sheet
        {"Track button held + Sampling", S::Any, T::Any, H::track, C::Sampling, G::Press, J::Print, St::Done},
        {"Note/Session held + Sampling", S::Any, T::Any, H::note, C::Sampling, G::Press, J::Print, St::Done}, // the pattern layer's freeze
        {"Pad (Print)", S::Print, T::Any, H::none, C::Pad, G::Press, J::PrintPattern, St::Done},
        {"Plus or Minus (Print)", S::Print, T::Any, H::none, C::Plus, G::Press, J::PrintBank, St::Done},
        {"Plus or Minus (Print)", S::Print, T::Any, H::none, C::Minus, G::Press, J::PrintBank, St::Done},
        {"Wheel turn (Print)", S::Print, T::Any, H::none, C::WheelTurn, G::Turn, J::PrintTurn, St::Done},
        {"Wheel push (Print)", S::Print, T::Any, H::none, C::WheelPush, G::Press, J::PrintPush, St::Done},
    };
}

bool matches(const Binding& row, const Use& use) {
    if (row.control != use.control || row.gesture != use.gesture || row.held != use.held) return false;
    if (row.only && (use.index < 0 || use.index > 31 || !(row.only & (1u << use.index)))) return false;
    if (row.step_held >= 0 && row.step_held != use.held_step) return false;
    return true;
}

// The tracks a row for `track` answers, most particular first.
std::array<Track, 3> tracks_for(Track track) {
    if (track == Track::Pitches) return {Track::Pitches, Track::Kit, Track::Any};
    if (track == Track::Any) return {Track::Any, Track::Count, Track::Count};
    return {track, Track::Any, Track::Count};
}

const Binding* find_in(Screen screen, const Where& where, const Use& use) {
    const auto& rows = table();
    for (Track track : tracks_for(where.track)) {
        if (track == Track::Count) break;
        for (const auto& row : rows)
            if (row.screen == screen && row.track == track && matches(row, use)) return &row;
    }
    return nullptr;
}

} // namespace

const char* job_name(Job job) {
    static const char* const names[] {
#define MELODEE_JOB_NAME(name) #name,
        MELODEE_JOBS(MELODEE_JOB_NAME)
#undef MELODEE_JOB_NAME
    };
    return job < Job::Count ? names[size_t(job)] : "?";
}

const char* status_name(Status status) {
    switch (status) {
    case Status::Done: return "done";
    case Status::Next: return "next";
    case Status::Core: return "core";
    case Status::Wave2: return "wave 2";
    case Status::Free: return "free";
    }
    return "?";
}

const std::vector<Binding>& table() {
    static const std::vector<Binding> rows = make_table();
    return rows;
}

Screen under(Screen screen) {
    switch (screen) {
    case Screen::Loop: case Screen::Browser: case Screen::List: case Screen::Sampling: case Screen::Canvas: case Screen::Looper: return Screen::Play;
    case Screen::Mixer: case Screen::KnobFx: case Screen::PadFx: case Screen::Flex: return Screen::Play;
    case Screen::ToPads: return Screen::Chop;
    default: return screen;
    }
}

const Binding* find(const Where& where, const Use& use) {
    for (Screen screen = where.screen;; screen = under(screen)) {
        if (const Binding* row = find_in(screen, where, use)) return row;
        if (under(screen) == screen) break;
    }
    if (where.screen != Screen::Typing)
        if (const Binding* row = find_in(Screen::Any, where, use)) return row;
    return find_in(Screen::Everywhere, where, use);
}

Job resolve(const Where& where, const Use& use) {
    const Binding* row = find(where, use);
    return row ? row->job : Job::Nothing;
}

bool plays(Job job) {
    // A punch and Flex Beat's pad sound too: the audio thread plays them (app/mixing.h).
    return job == Job::PlayPad || job == Job::PlayNote || job == Job::PlayPitch || job == Job::PlaySlice || job == Job::Punch || job == Job::FlexPad;
}

bool pad_sounds(const Where& where, uint16_t held, unsigned pad) {
    Use use;
    use.control = Control::Pad;
    use.index = int(pad);
    use.held = held;
    const Binding* row = find(where, use);
    return !row || row->job == Job::Nothing || plays(row->job);
}

Mapped map(const move::Input& input) {
    using Kind = move::Input::Kind;
    using move::Button;
    switch (input.kind) {
    case Kind::Pad: return {Control::Pad, input.index};
    case Kind::PadPressure: return {Control::PadPressure, input.index};
    case Kind::Step: return {Control::Step, input.index};
    case Kind::Knob: return {Control::Knob, input.index};
    case Kind::MasterKnob: return {Control::Volume, 0};
    case Kind::MainKnob: return {Control::WheelTurn, 0};
    case Kind::Touch:
        if (input.index < move::knob_count) return {Control::KnobTouch, input.index};
        return {input.index == move::touch_master ? Control::VolumeTouch : Control::WheelTouch, 0};
    case Kind::Button:
        switch (Button(input.index)) {
        case Button::MainPush: return {Control::WheelPush, 0};
        case Button::Row1: return {Control::TrackButton, 0};
        case Button::Row2: return {Control::TrackButton, 1};
        case Button::Row3: return {Control::TrackButton, 2};
        case Button::Row4: return {Control::TrackButton, 3};
        case Button::Shift: return {Control::Shift, 0};
        case Button::Menu: return {Control::NoteSession, 0};
        case Button::Back: return {Control::Back, 0};
        case Button::Capture: return {Control::Capture, 0};
        case Button::Down: return {Control::Minus, 0};
        case Button::Up: return {Control::Plus, 0};
        case Button::Undo: return {Control::Undo, 0};
        case Button::Loop: return {Control::Loop, 0};
        case Button::Copy: return {Control::Copy, 0};
        case Button::Left: return {Control::Left, 0};
        case Button::Right: return {Control::Right, 0};
        case Button::Play: return {Control::Play, 0};
        case Button::Rec: return {Control::Rec, 0};
        case Button::Mute: return {Control::Mute, 0};
        case Button::Sample: return {Control::Sampling, 0};
        case Button::Delete: return {Control::Delete, 0};
        case Button::Count: break;
        }
        break;
    default: break;
    }
    return {};
}

// ---------------------------------------------------------------- gestures

uint16_t Gestures::held(Control for_control) const {
    uint16_t bits = 0;
    if (shift_down || (locked && for_control != Control::Play)) bits |= held::shift;
    for (const auto& down : downs) {
        switch (down.control) {
        case Control::TrackButton: bits |= held::track; break;
        case Control::Step: bits |= held::step; break;
        case Control::Pad: bits |= held::pad; break;
        case Control::Copy: bits |= held::copy; break;
        case Control::Delete: bits |= held::del; break;
        case Control::Mute: bits |= held::mute; break;
        case Control::Loop: bits |= held::loop; break;
        case Control::NoteSession: if (for_control == Control::Sampling) bits |= held::note; break;
        default: break;
        }
    }
    return bits;
}

int Gestures::held_step() const {
    for (const auto& down : downs)
        if (down.control == Control::Step) return down.index;
    return -1;
}

void Gestures::mark_used() {
    for (auto& down : downs) down.used = true;
}

void Gestures::spend(Control control, int index) {
    for (auto& down : downs)
        if (down.control == control && down.index == index) down.used = true;
}

void Gestures::reset() {
    downs.clear();
    last_press = {};
    locked = shift_down = false;
}

std::vector<Use> Gestures::feed(const move::Input& input, Clock::time_point now) {
    std::vector<Use> uses;
    Mapped mapped = map(input);
    if (mapped.control == Control::Count) return uses;
    auto use_of = [&](Gesture gesture) {
        Use use;
        use.control = mapped.control;
        use.index = mapped.index;
        use.gesture = gesture;
        use.held = held(mapped.control);
        use.held_step = held_step();
        use.delta = input.delta;
        return use;
    };
    using Kind = move::Input::Kind;
    if (input.kind == Kind::Knob || input.kind == Kind::MasterKnob || input.kind == Kind::MainKnob) {
        uses.push_back(use_of(Gesture::Turn));
        mark_used(); // a button held while something turns was a modifier
        return uses;
    }
    if (input.kind == Kind::PadPressure) {
        uses.push_back(use_of(Gesture::Turn));
        return uses;
    }
    auto at = std::find_if(downs.begin(), downs.end(), [&](const Down& d) { return d.control == mapped.control && d.index == mapped.index; });
    if (input.pressed) {
        if (at != downs.end()) return uses; // a repeated edge
        uses.push_back(use_of(Gesture::Press));
        bool twice = last_press.control == mapped.control && last_press.index == mapped.index && now - last_press.at <= double_time;
        if (twice) uses.push_back(use_of(Gesture::Double));
        last_press = {mapped.control, mapped.index, twice ? Clock::time_point {} : now}; // a third press starts afresh
        mark_used();
        Down down;
        down.control = mapped.control;
        down.index = mapped.index;
        down.since = now;
        down.held = uses.front().held;
        down.held_step = uses.front().held_step;
        downs.push_back(down);
        if (mapped.control == Control::Shift) {
            if (twice) locked = !locked; // the second press locks; a press while locked unlocks
            else if (locked) locked = false;
            shift_down = true;
        }
        return uses;
    }
    // Let go.
    if (at == downs.end()) return uses;
    Down down = *at;
    downs.erase(at);
    if (mapped.control == Control::Shift) shift_down = false;
    if (input.kind == Kind::Pad) { uses.push_back(use_of(Gesture::Release)); return uses; } // a pad has no tap; let go, a Release
    if (input.kind == Kind::Touch) return uses; // a touch has no tap
    if (!down.used && !down.held_long && now - down.since < hold_time) {
        Use use = use_of(Gesture::Tap);
        use.held = down.held; // as it was when it went down
        use.held_step = down.held_step;
        uses.push_back(use);
    }
    return uses;
}

std::vector<Use> Gestures::tick(Clock::time_point now) {
    std::vector<Use> uses;
    for (auto& down : downs) {
        if (down.used || down.held_long || now - down.since < hold_time) continue;
        if (down.control == Control::Pad || down.control == Control::Step || down.control == Control::KnobTouch ||
            down.control == Control::VolumeTouch || down.control == Control::WheelTouch)
            continue; // a held pad, step or touch is a modifier, no gesture of its own
        down.held_long = true;
        Use use;
        use.control = down.control;
        use.index = down.index;
        use.gesture = Gesture::Hold;
        use.held = down.held;
        use.held_step = down.held_step;
        uses.push_back(use);
    }
    return uses;
}

} // namespace app::bindings
