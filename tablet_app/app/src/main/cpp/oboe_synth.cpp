// Native MIDI → SoundFont audio synthesizer for HarpHymnal.
//
// Replaces the prior in-house triangle synth with FluidLite (a stripped-
// down FluidSynth) so the keyboard sounds like a real GM acoustic grand
// piano (or whatever bank/program is loaded). Output goes through the
// same Oboe stream (AAudio Exclusive / LowLatency) so the latency profile
// stays at ~5 ms/burst.
//
// Pipeline per audio callback:
//   1. Drain SPSC event queue (note-on/off pushed from JNI MIDI thread)
//      and apply each event via fluid_synth_noteon / fluid_synth_noteoff.
//   2. Call fluid_synth_write_float() to render exactly `numFrames`
//      stereo samples into Oboe's output buffer.
//
// JNI surface (called by OboeSynth.java):
//   nativeStart()                  — open Oboe + initialise FluidLite
//   nativeStop()                   — tear both down
//   nativeNoteOn(n,v) / NoteOff(n) — queue MIDI events
//   nativeAllNotesOff()            — kill every active voice
//   nativeNoteOnCount()            — counter for the home-grid banner
//   nativeLatencyMs()              — Oboe-reported end-to-end latency
//   nativeSetSpeakerDeviceId(id)   — pin output to BUILTIN_SPEAKER so
//                                    USB-audio devices can't hijack
//   nativeSetSoundfontPath(path)   — full path to the .sf2 file copied
//                                    out of the APK to filesDir

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>

extern "C" {
#include "fluidlite.h"
}

#define TAG "OboeSynth"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

constexpr int kQueueSize = 256;          // power of two

enum class EvKind : uint8_t { NoteOn, NoteOff, AllOff };
struct Event {
    EvKind kind;
    uint8_t note;
    uint8_t vel;
};

// Lock-free single-producer / single-consumer ring buffer between the
// JNI/MIDI thread and the audio callback.
class EventQueue {
public:
    bool push(const Event& e) {
        size_t w = w_.load(std::memory_order_relaxed);
        size_t r = r_.load(std::memory_order_acquire);
        if (((w + 1) & (kQueueSize - 1)) == (r & (kQueueSize - 1))) return false;
        buf_[w & (kQueueSize - 1)] = e;
        w_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(Event& out) {
        size_t r = r_.load(std::memory_order_relaxed);
        size_t w = w_.load(std::memory_order_acquire);
        if ((r & (kQueueSize - 1)) == (w & (kQueueSize - 1))) return false;
        out = buf_[r & (kQueueSize - 1)];
        r_.store(r + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<Event, kQueueSize> buf_{};
    std::atomic<size_t> w_{0};
    std::atomic<size_t> r_{0};
};

class Synth : public oboe::AudioStreamDataCallback,
              public oboe::AudioStreamErrorCallback {
public:
    void setSpeakerDeviceId(int id)         { speakerDeviceId_ = id; }
    void setSoundfontPath(const char* path) { soundfontPath_ = path ? path : ""; }

    bool start() {
        if (stream_) return true;

        // ── Oboe stream first; we need its sample rate before initialising
        //    FluidLite so the synth runs at the right rate. ───────────────
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output);
        b.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        b.setSharingMode(oboe::SharingMode::Exclusive);
        b.setFormat(oboe::AudioFormat::Float);
        b.setChannelCount(oboe::ChannelCount::Stereo);     // FluidLite is stereo
        b.setUsage(oboe::Usage::Media);
        b.setContentType(oboe::ContentType::Music);
        b.setDataCallback(this);
        b.setErrorCallback(this);
        if (speakerDeviceId_ > 0) b.setDeviceId(speakerDeviceId_);

        oboe::Result r = b.openStream(stream_);
        if (r != oboe::Result::OK) {
            LOGE("openStream: %s", oboe::convertToText(r));
            stream_.reset();
            return false;
        }
        sampleRate_ = stream_->getSampleRate();
        LOGI("Oboe stream: sr=%d, frames/burst=%d, channels=%d, sharing=%s, perf=%s",
             sampleRate_, stream_->getFramesPerBurst(),
             stream_->getChannelCount(),
             oboe::convertToText(stream_->getSharingMode()),
             oboe::convertToText(stream_->getPerformanceMode()));
        stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);

        // ── FluidLite synth ─────────────────────────────────────────────
        if (!initFluid()) {
            LOGE("FluidLite init failed");
            stream_->close();
            stream_.reset();
            return false;
        }

        r = stream_->requestStart();
        if (r != oboe::Result::OK) {
            LOGE("requestStart: %s", oboe::convertToText(r));
            stream_->close();
            stream_.reset();
            destroyFluid();
            return false;
        }
        return true;
    }

    void stop() {
        if (stream_) {
            stream_->stop();
            stream_->close();
            stream_.reset();
        }
        destroyFluid();
    }

    void noteOn(uint8_t note, uint8_t vel) {
        queue_.push({EvKind::NoteOn, note, vel});
        noteOnCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void noteOff(uint8_t note) { queue_.push({EvKind::NoteOff, note, 0}); }
    void allNotesOff()         { queue_.push({EvKind::AllOff, 0, 0}); }

    int noteOnCount() const { return noteOnCount_.load(std::memory_order_relaxed); }

    double latencyMs() {
        if (!stream_) return 0.0;
        auto res = stream_->calculateLatencyMillis();
        return res ? res.value() : 0.0;
    }

    // ── Oboe error callback: stream disconnected (cable, route change) ──
    void onErrorAfterClose(oboe::AudioStream* /*stream*/,
                           oboe::Result result) override {
        LOGW("stream disconnected: %s; restarting…", oboe::convertToText(result));
        std::thread([this] {
            stream_.reset();
            // FluidLite state is per-instance and survives — but emit
            // an all-sound-off so the new stream starts silent rather
            // than picking up zombie voice tails.
            if (fluid_) fluid_synth_system_reset(fluid_);
            start();
        }).detach();
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* /*s*/,
                                          void* audioData,
                                          int32_t numFrames) override {
        // Drain queue → FluidLite (audio-thread side).
        Event e;
        while (queue_.pop(e)) {
            switch (e.kind) {
                case EvKind::NoteOn:
                    if (fluid_) fluid_synth_noteon(fluid_, 0, e.note, e.vel);
                    break;
                case EvKind::NoteOff:
                    if (fluid_) fluid_synth_noteoff(fluid_, 0, e.note);
                    break;
                case EvKind::AllOff:
                    if (fluid_) fluid_synth_system_reset(fluid_);
                    break;
            }
        }

        float* out = static_cast<float*>(audioData);
        if (!fluid_) {
            std::memset(out, 0, sizeof(float) * numFrames * 2);
            return oboe::DataCallbackResult::Continue;
        }
        // FluidLite writes interleaved L/R when stride args = 2.
        fluid_synth_write_float(fluid_, numFrames,
                                out, /*loff=*/0, /*lstride=*/2,
                                out, /*roff=*/1, /*rstride=*/2);
        return oboe::DataCallbackResult::Continue;
    }

private:
    bool initFluid() {
        if (fluid_) return true;
        settings_ = new_fluid_settings();
        if (!settings_) return false;
        fluid_settings_setnum(settings_, "synth.sample-rate", (double)sampleRate_);
        fluid_settings_setint(settings_, "synth.polyphony", 64);
        // Reverb / chorus off by default — they tax CPU and we want clean
        // playthrough latency. Can be re-enabled per voice later.
        fluid_settings_setint(settings_, "synth.reverb.active", 0);
        fluid_settings_setint(settings_, "synth.chorus.active", 0);

        fluid_ = new_fluid_synth(settings_);
        if (!fluid_) {
            delete_fluid_settings(settings_);
            settings_ = nullptr;
            return false;
        }
        if (soundfontPath_.empty()) {
            LOGW("no soundfont path set — synth will be silent");
            return true;
        }
        // FLUID_OK = 0, FLUID_FAILED = -1 (per fluidsynth_priv.h, which is
        // not in the public include path). fluid_synth_sfload returns the
        // sfont id (positive) on success or -1 on error.
        int sfid = fluid_synth_sfload(fluid_, soundfontPath_.c_str(), 1);
        if (sfid < 0) {
            LOGE("fluid_synth_sfload failed for %s", soundfontPath_.c_str());
            return true;  // keep the synth alive, just no patches
        }
        LOGI("Soundfont loaded: %s (sfid=%d)", soundfontPath_.c_str(), sfid);
        // Default to GM Acoustic Grand Piano (bank 0, program 0) on ch 0.
        fluid_synth_program_change(fluid_, 0, 0);
        return true;
    }

    void destroyFluid() {
        if (fluid_) {
            delete_fluid_synth(fluid_);
            fluid_ = nullptr;
        }
        if (settings_) {
            delete_fluid_settings(settings_);
            settings_ = nullptr;
        }
    }

    std::shared_ptr<oboe::AudioStream> stream_;
    int sampleRate_ = 48000;

    fluid_synth_t*    fluid_    = nullptr;
    fluid_settings_t* settings_ = nullptr;

    EventQueue queue_;
    std::atomic<int> noteOnCount_{0};
    int speakerDeviceId_ = 0;
    std::string soundfontPath_;
};

Synth g_synth;

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeStart(JNIEnv*, jclass) {
    return g_synth.start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeStop(JNIEnv*, jclass) {
    g_synth.stop();
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeNoteOn(JNIEnv*, jclass,
                                                      jint note, jint vel) {
    g_synth.noteOn((uint8_t)(note & 0x7F), (uint8_t)(vel & 0x7F));
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeNoteOff(JNIEnv*, jclass,
                                                       jint note) {
    g_synth.noteOff((uint8_t)(note & 0x7F));
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeAllNotesOff(JNIEnv*, jclass) {
    g_synth.allNotesOff();
}

JNIEXPORT jint JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeNoteOnCount(JNIEnv*, jclass) {
    return (jint) g_synth.noteOnCount();
}

JNIEXPORT jdouble JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeLatencyMs(JNIEnv*, jclass) {
    return (jdouble) g_synth.latencyMs();
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeSetSpeakerDeviceId(JNIEnv*, jclass,
                                                                  jint id) {
    g_synth.setSpeakerDeviceId((int)id);
}

JNIEXPORT void JNICALL
Java_com_tutor_musictutor_OboeSynth_nativeSetSoundfontPath(JNIEnv* env, jclass,
                                                                jstring jpath) {
    if (!jpath) { g_synth.setSoundfontPath(""); return; }
    const char* cstr = env->GetStringUTFChars(jpath, nullptr);
    g_synth.setSoundfontPath(cstr);
    env->ReleaseStringUTFChars(jpath, cstr);
}

}  // extern "C"
