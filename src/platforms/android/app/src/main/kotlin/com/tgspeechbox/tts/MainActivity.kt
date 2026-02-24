/*
 * MainActivity — Launcher activity for TGSpeechBox.
 *
 * Consumer-facing UI with a Speak tab and an Advanced settings tab.
 * Uses TgsbSpeakEngine (direct JNI) for speech playback, completely
 * independent of the TextToSpeechService used by TalkBack.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider

class MainActivity : ComponentActivity() {

    private lateinit var viewModel: TgsbViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewModel = ViewModelProvider(this)[TgsbViewModel::class.java]
        setContent {
            TgsbApp(viewModel = viewModel)
        }
    }
}
