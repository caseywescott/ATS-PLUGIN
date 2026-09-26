#pragma once

/** @file AtsRtSafety.h
 *  Which ATSA entry points are safe on a real-time audio thread.
 *
 *  The Qatsh ATSA library (tracker, do_synthesis, optimize_sound, etc.) performs
 *  allocation and heavy math. This project therefore does NOT call ATSA from
 *  the audio thread. The audio thread only reads prepared PCM layers held in
 *  owned bounded audio buffers; classic ATS resynthesis and morph snapshot preview
 *  buffers are both prepared before the active buffer index is swapped.
 */
