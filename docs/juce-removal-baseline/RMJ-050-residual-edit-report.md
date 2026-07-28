# RMJ-050 residual edit service

`ResidualEditService` is a JUCE-free, atomic residual matrix boundary. It
supports mute, scale, dB draw/interpolation, smoothing, band tilt, clipboard
copy/paste, swap, and equal-energy redistribution over normalized band/frame
selections. It validates finite non-negative energy and commits only a fully
validated working copy, making one operation suitable for one undo transaction
and one invalidation revision.

The focused test exercises successful and rejected edits, clipboard shape,
finite output, swap, draw, and redistribution behavior. Integration into the
live editor and model undo composition remains a later host/UI gate.
