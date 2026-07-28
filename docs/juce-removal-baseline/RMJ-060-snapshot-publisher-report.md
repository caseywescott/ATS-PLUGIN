# RMJ-060 parameter, progress, and playhead publishers

The neutral snapshot layer now includes a fixed-capacity parameter snapshot,
atomic progress publication, and audio-safe normalized playhead publication.
Each publisher has explicit revision/generation ownership and finite-value
clamping. The focused contract test covers parameter capture, progress
cancellation, and playhead bounds without UI or host types.
