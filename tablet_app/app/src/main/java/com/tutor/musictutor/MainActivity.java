package com.tutor.musictutor;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/**
 * Single-Activity WebView host for the tutor UI. Mirrors HarpHymnal's
 * MainActivity, minus the composer-specific file I/O bridge and the
 * IME focus tracking -- this app is read-only HTML + MIDI input.
 *
 * Static {@link #runJsInActiveWebView} lets MidiSynthService inject
 * MIDI events into the WebView from a background thread.
 */
public class MainActivity extends Activity {
    private WebView webView = null;

    private static java.lang.ref.WeakReference<MainActivity> sActiveRef =
        new java.lang.ref.WeakReference<>(null);

    public static boolean runJsInActiveWebView(String js) {
        final MainActivity a = sActiveRef.get();
        if (a == null || a.webView == null) return false;
        a.runOnUiThread(() -> {
            try { a.webView.evaluateJavascript(js, null); } catch (Exception ignored) {}
        });
        return true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        sActiveRef = new java.lang.ref.WeakReference<>(this);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (sActiveRef.get() == this) {
            sActiveRef = new java.lang.ref.WeakReference<>(null);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Tablet on the music stand -- never sleep during practice.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        webView = new WebView(this);
        setContentView(webView);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(true);
        // Required so JS fetch("hymns.json") from a file:// page can read
        // bundled assets. Both flags are deprecated but still functional;
        // HarpHymnal uses the same pair.
        settings.setAllowFileAccessFromFileURLs(true);
        settings.setAllowUniversalAccessFromFileURLs(true);

        WebView.setWebContentsDebuggingEnabled(true);
        webView.setWebViewClient(new WebViewClient());

        webView.loadUrl("file:///android_asset/index.html");

        // Kick off the MIDI synth foreground service. Runs independent
        // of this Activity so the keyboard plays even if the tutor UI
        // is closed. Also auto-started by UsbMidiAttachActivity on
        // USB-MIDI plug-in.
        try {
            Intent svc = new Intent(this, MidiSynthService.class);
            svc.setAction(MidiSynthService.ACTION_START);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(svc);
            } else {
                startService(svc);
            }
        } catch (Throwable t) {
            android.util.Log.w("MainActivity", "MIDI service start failed", t);
        }
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }
}
