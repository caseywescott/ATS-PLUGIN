#pragma once

/** @file AtsRtSafety.h
 *  Which ATSA entry points are safe on a real-time audio thread.
 *
 *  The Qatsh ATSA library (tracker, do_synthesis, optimize_sound, etc.) performs
 *  allocation and heavy math. This project therefore does NOT call ATSA from
 *  the audio thread. The audio thread only reads pre-rendered PCM held in
 *  juce::AudioBuffer<float> under brief mutex protection during pointer swap.
 */
