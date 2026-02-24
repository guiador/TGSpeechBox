/*
 * TgsbViewModel — Shared state for the TGSpeechBox consumer UI.
 *
 * Drives the Speak & Basics tab and the Advanced tab.  Uses
 * TgsbSpeakEngine (direct JNI) for speech — NOT the TextToSpeech
 * client API, which conflicts with TalkBack using the same service.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.app.Application
import android.content.SharedPreferences
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.Locale

/** One entry in the language dropdown (deduplicated). */
data class LanguageItem(
    val displayName: String,
    val langDef: TgsbTtsService.Companion.LangDef
)

class TgsbViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "TgsbUI"
    }

    private val prefs: SharedPreferences =
        application.getSharedPreferences(TgsbTtsService.PREFS_NAME, 0)

    private val engine = TgsbSpeakEngine(application)

    // ── UI state ──────────────────────────────────────────────────────

    val textToSpeak = MutableStateFlow("Hello world. This is TGSpeechBox.")
    val selectedLanguageIndex = MutableStateFlow(0)
    val selectedVoiceIndex = MutableStateFlow(0)
    val speedRate = MutableStateFlow(1.0f)        // 0.3 – 3.0
    val pitchHz = MutableStateFlow(120f)           // 40 – 300 Hz
    val isSpeaking = MutableStateFlow(false)
    val engineReady = MutableStateFlow(false)
    val errorMessage = MutableStateFlow<String?>(null)

    // ── Data lists ────────────────────────────────────────────────────

    /** Deduplicated, sorted language list for the dropdown. */
    val languages: List<LanguageItem> = buildLanguageList()

    val voices: List<TgsbTtsService.Companion.VoiceDef> = TgsbTtsService.VOICES

    // ── Language filter state (Advanced tab) ──────────────────────────

    private val _enabledLocaleKeys = MutableStateFlow(loadEnabledKeys())
    val enabledLocaleKeys: StateFlow<Set<String>> = _enabledLocaleKeys

    /** All unique locale keys for the checkbox list. */
    val allLocaleEntries: List<Pair<String, String>> = buildLocaleEntries()

    // ── Init ──────────────────────────────────────────────────────────

    init {
        // Voice preset
        val savedPreset = prefs.getString(
            TgsbTtsService.PREF_VOICE_PRESET,
            TgsbTtsService.DEFAULT_PRESET
        ) ?: TgsbTtsService.DEFAULT_PRESET
        val idx = voices.indexOfFirst { it.id == savedPreset }
        if (idx >= 0) selectedVoiceIndex.value = idx

        // Default language: match device locale
        val deviceLocale = Locale.getDefault()
        val langMatch = languages.indexOfFirst { item ->
            val ld = item.langDef.displayLocale
            ld.language == deviceLocale.language && ld.country == deviceLocale.country
        }
        val langFallback = if (langMatch < 0) {
            languages.indexOfFirst { it.langDef.displayLocale.language == deviceLocale.language }
        } else langMatch
        if (langFallback >= 0) {
            selectedLanguageIndex.value = langFallback
            Log.i(TAG, "Default language: ${languages[langFallback].displayName}")
        }

        // Start the standalone engine
        if (engine.start()) {
            engineReady.value = true
            errorMessage.value = null

            // Set initial language + voice
            val ld = languages[selectedLanguageIndex.value].langDef
            engine.setLanguage(ld.espeakLang, ld.tgsbLang)
            engine.setVoice(voices[selectedVoiceIndex.value].id)

            engine.onSpeakingChanged = { speaking ->
                isSpeaking.value = speaking
            }

            Log.i(TAG, "Standalone engine ready")
        } else {
            errorMessage.value = "Failed to start speech engine."
            Log.e(TAG, "Engine start failed")
        }
    }

    override fun onCleared() {
        engine.shutdown()
        super.onCleared()
    }

    // ── Speak / Stop ──────────────────────────────────────────────────

    fun speak() {
        val text = textToSpeak.value
        if (text.isBlank()) return

        // Apply current settings
        val ld = languages[selectedLanguageIndex.value].langDef
        engine.setLanguage(ld.espeakLang, ld.tgsbLang)
        engine.setVoice(voices[selectedVoiceIndex.value].id)

        errorMessage.value = null
        engine.speak(text, speedRate.value.toDouble(), pitchHz.value.toDouble())
        Log.i(TAG, "speak: lang=${ld.espeakLang} speed=${speedRate.value} pitch=${pitchHz.value}")
    }

    fun stop() {
        engine.stop()
    }

    // ── Selection handlers ────────────────────────────────────────────

    fun onLanguageSelected(index: Int) {
        selectedLanguageIndex.value = index
        val ld = languages[index].langDef
        engine.setLanguage(ld.espeakLang, ld.tgsbLang)
        Log.i(TAG, "Language selected: ${ld.espeakLang}")
    }

    fun onVoiceSelected(index: Int) {
        selectedVoiceIndex.value = index
        val voiceId = voices[index].id
        engine.setVoice(voiceId)
        prefs.edit().putString(TgsbTtsService.PREF_VOICE_PRESET, voiceId).apply()
    }

    // ── Language filter (Advanced tab) ────────────────────────────────

    fun toggleLocaleKey(localeKey: String, enabled: Boolean): Boolean {
        val current = _enabledLocaleKeys.value.toMutableSet()
        if (enabled) {
            current.add(localeKey)
        } else {
            if (current.size <= 1) return false
            current.remove(localeKey)
        }
        _enabledLocaleKeys.value = current.toSet()
        saveEnabledKeys(current)
        return true
    }

    // ── Helpers ───────────────────────────────────────────────────────

    private fun buildLanguageList(): List<LanguageItem> {
        val seen = mutableSetOf<String>()
        val items = mutableListOf<LanguageItem>()
        for (ld in TgsbTtsService.LANGUAGES) {
            val key = ld.displayLocale.toString()
            if (key in seen) continue
            seen.add(key)
            items.add(LanguageItem(ld.displayLocale.getDisplayName(), ld))
        }
        items.sortBy { it.displayName }
        return items
    }

    private fun buildLocaleEntries(): List<Pair<String, String>> {
        val seen = mutableSetOf<String>()
        val entries = mutableListOf<Pair<String, String>>()
        for (ld in TgsbTtsService.LANGUAGES) {
            val key = ld.displayLocale.toString()
            if (key in seen) continue
            seen.add(key)
            entries.add(key to ld.displayLocale.getDisplayName())
        }
        entries.sortBy { it.second }
        return entries
    }

    private fun loadEnabledKeys(): Set<String> {
        val saved = TgsbTtsService.getEnabledLocaleKeys(prefs)
        if (saved != null) return saved
        return buildLocaleEntries().map { it.first }.toSet()
    }

    private fun saveEnabledKeys(keys: Set<String>) {
        if (keys.size >= allLocaleEntries.size) {
            prefs.edit().remove(TgsbTtsService.PREF_SUPPORTED_LANGUAGES).apply()
        } else {
            prefs.edit()
                .putStringSet(TgsbTtsService.PREF_SUPPORTED_LANGUAGES, keys)
                .apply()
        }
    }
}
