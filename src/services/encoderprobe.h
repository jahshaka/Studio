#ifndef ENCODERPROBE_H
#define ENCODERPROBE_H

// QT'S VIDEO ENCODER PROBE, MADE ONCE PER PROCESS (VIDEO-REC-2).
//
// The first QMediaFormat::supportedVideoCodecs(Encode) of a process constructs
// Qt Multimedia's FFmpeg integration and enumerates the machine's encoders:
// ~0.7-1.0 s on the UI thread, measured. Left alone it was the session's first
// record click (VIDEO-REC-1's "585 ms first click"). It may not happen at BOOT
// (app.startup_quiet: Qt Multimedia is not constructed at boot, Lane 6a), so the
// FIRST project open or create of a process makes it, as one slice behind the
// open's own progress (ProjectRunner), on the UI thread where Qt expects its
// calls; every later open of the session skips the slice.

namespace encoderprobe {

/// True once warm() has run in this process.
bool warmed();
/// Makes the probe (once; a no-op after) and returns the ms it took.
double warm();
/// The ms the probe took when it was made, 0 before.
double warmMs();

}   // namespace encoderprobe

#endif // ENCODERPROBE_H
