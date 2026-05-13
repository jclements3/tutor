package com.tutor.musictutor;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;

/**
 * Transparent launcher hit by Android when a class-compliant USB MIDI
 * device is plugged in (matched by res/xml/usb_midi_filter.xml). Starts
 * the synth service and finishes immediately -- no UI flash, no
 * MainActivity launch. The keyboard makes sound regardless of whether
 * the tutor UI is open.
 */
public class UsbMidiAttachActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent svc = new Intent(this, MidiSynthService.class);
        svc.setAction(MidiSynthService.ACTION_START);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }
        finish();
    }
}
