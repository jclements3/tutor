package com.tutor.musictutor;

/**
 * Java side of the native Oboe-backed synth. Loads libmusictutor_synth.so
 * once and exposes the JNI surface as static methods. Concurrency-safe:
 * the C++ side uses a lock-free SPSC queue, so MIDI events from any
 * thread land in the audio callback without locks.
 */
public final class OboeSynth {
    static {
        System.loadLibrary("musictutor_synth");
    }

    private OboeSynth() {}

    public static native boolean nativeStart();
    public static native void    nativeStop();
    public static native void    nativeNoteOn(int note, int velocity);
    public static native void    nativeNoteOff(int note);
    public static native void    nativeAllNotesOff();
    public static native int     nativeNoteOnCount();
    public static native double  nativeLatencyMs();
    /** Pin the Oboe output stream to a specific AudioDeviceInfo ID.
     *  Pass the BUILTIN_SPEAKER's getId() before nativeStart() so USB
     *  audio devices can't hijack the route. */
    public static native void    nativeSetSpeakerDeviceId(int id);
    /** Set the absolute path of the .sf2 file FluidLite will load.
     *  Must be a real filesystem path (not assets://) -- the service
     *  copies the bundled assets/soundfont/gm.sf2 to filesDir on first
     *  launch. Call BEFORE nativeStart(). */
    public static native void    nativeSetSoundfontPath(String path);
}
