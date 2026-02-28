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
import kotlin.math.roundToInt

/** One entry in the language dropdown (deduplicated). */
data class LanguageItem(
    val displayName: String,
    val langDef: TgsbTtsService.Companion.LangDef
)

class TgsbViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "TgsbUI"
        private const val PREF_PREFIX = "adv_"
        val SAMPLE_RATES = listOf(11025, 16000, 22050, 44100)
    }

    private val prefs: SharedPreferences =
        application.getSharedPreferences(TgsbTtsService.PREFS_NAME, 0)

    private val engine = TgsbSpeakEngine(application)

    // ── UI state (Speak tab) ────────────────────────────────────────

    val textToSpeak = MutableStateFlow("Hello world. This is TGSpeechBox.")
    val selectedLanguageIndex = MutableStateFlow(0)
    val selectedVoiceIndex = MutableStateFlow(0)
    val speedRate = MutableStateFlow(1.0f)        // 0.3 – 3.0
    val pitchHz = MutableStateFlow(110f)           // 40 – 300 Hz
    val isSpeaking = MutableStateFlow(false)
    val engineReady = MutableStateFlow(false)
    val errorMessage = MutableStateFlow<String?>(null)

    // ── VoicingTone sliders (0–100, mapped to real ranges) ──────────

    val voiceTilt = MutableStateFlow(loadSlider("voiceTilt", 50f))
    val noiseGlottalMod = MutableStateFlow(loadSlider("noiseGlottalMod", 0f))
    val pitchSyncF1 = MutableStateFlow(loadSlider("pitchSyncF1", 50f))
    val pitchSyncB1 = MutableStateFlow(loadSlider("pitchSyncB1", 50f))
    val speedQuotient = MutableStateFlow(loadSlider("speedQuotient", 50f))
    val aspirationTilt = MutableStateFlow(loadSlider("aspirationTilt", 50f))
    val cascadeBwScale = MutableStateFlow(loadSlider("cascadeBwScale", 50f))
    val voiceTremor = MutableStateFlow(loadSlider("voiceTremor", 0f))

    // ── Pitch settings ──────────────────────────────────────────────

    val pitchMode = MutableStateFlow(loadString("pitchMode", "espeak_style"))
    val inflectionScale = MutableStateFlow(loadSlider("inflectionScale", 58f))
    val inflection = MutableStateFlow(loadSlider("inflection", 50f))

    // ── System rate override ────────────────────────────────────────

    val overrideSystemRate = MutableStateFlow(loadBool("overrideSystemRate", false))
    val globalRate = MutableStateFlow(loadSlider("globalRate", 1.0f))

    // ── Output ───────────────────────────────────────────────────────

    val pauseMode = MutableStateFlow(loadInt("pauseMode", 1))  // 0=off, 1=short, 2=long
    val systemVolume = MutableStateFlow(loadSlider("systemVolume", 1.0f))
    val sampleRateIndex = MutableStateFlow(loadInt("sampleRate", 22050).let { rate ->
        SAMPLE_RATES.indexOfFirst { it == rate }.coerceAtLeast(0).toFloat()
    })

    // ── FrameEx sliders (0–100) ─────────────────────────────────────

    val creakiness = MutableStateFlow(loadSlider("creakiness", 0f))
    val breathiness = MutableStateFlow(loadSlider("breathiness", 0f))
    val jitter = MutableStateFlow(loadSlider("jitter", 0f))
    val shimmer = MutableStateFlow(loadSlider("shimmer", 0f))
    val glottalSharpness = MutableStateFlow(loadSlider("glottalSharpness", 50f))

    // ── Data lists ────────────────────────────────────────────────────

    val languages: List<LanguageItem> = buildLanguageList()
    val voices: List<TgsbTtsService.Companion.VoiceDef> = TgsbTtsService.VOICES

    // ── Language filter state ────────────────────────────────────────

    private val _enabledLocaleKeys = MutableStateFlow(loadEnabledKeys())
    val enabledLocaleKeys: StateFlow<Set<String>> = _enabledLocaleKeys
    val allLocaleEntries: List<Pair<String, String>> = buildLocaleEntries()

    // ── Init ────────────────────────────────────────────────────────

    init {
        val savedPreset = prefs.getString(
            TgsbTtsService.PREF_VOICE_PRESET,
            TgsbTtsService.DEFAULT_PRESET
        ) ?: TgsbTtsService.DEFAULT_PRESET
        val idx = voices.indexOfFirst { it.id == savedPreset }
        if (idx >= 0) selectedVoiceIndex.value = idx

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

        if (engine.start()) {
            engineReady.value = true
            errorMessage.value = null

            val ld = languages[selectedLanguageIndex.value].langDef
            engine.setLanguage(ld.espeakLang, ld.tgsbLang)
            engine.setVoice(voices[selectedVoiceIndex.value].id)
            applyVoicingTone()
            applyFrameExDefaults()
            applyPitchSettings()
            engine.setVolume(systemVolume.value)
            val savedRate = SAMPLE_RATES[sampleRateIndex.value.roundToInt().coerceIn(0, SAMPLE_RATES.size - 1)]
            engine.setSampleRate(savedRate)
            engine.setPauseMode(pauseMode.value)

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

    // ── Speak / Stop ────────────────────────────────────────────────

    fun speak() {
        val text = textToSpeak.value
        if (text.isBlank()) return

        val ld = languages[selectedLanguageIndex.value].langDef
        engine.setLanguage(ld.espeakLang, ld.tgsbLang)
        engine.setVoice(voices[selectedVoiceIndex.value].id)
        applyVoicingTone()
        applyFrameExDefaults()
        applyPitchSettings()
        engine.setPauseMode(pauseMode.value)

        errorMessage.value = null
        engine.speak(text, speedRate.value.toDouble(), pitchHz.value.toDouble())
        Log.i(TAG, "speak: lang=${ld.espeakLang} speed=${speedRate.value} pitch=${pitchHz.value}")
    }

    fun stop() {
        engine.stop()
    }

    // ── Selection handlers ──────────────────────────────────────────

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
        applyVoicingTone()
        prefs.edit().putString(TgsbTtsService.PREF_VOICE_PRESET, voiceId).apply()
    }

    // ── Voice quality: slider change handlers ───────────────────────

    fun onVoiceTiltChanged(v: Float)       { voiceTilt.value = v;       saveSlider("voiceTilt", v);       applyVoicingTone() }
    fun onNoiseGlottalModChanged(v: Float) { noiseGlottalMod.value = v; saveSlider("noiseGlottalMod", v); applyVoicingTone() }
    fun onPitchSyncF1Changed(v: Float)     { pitchSyncF1.value = v;     saveSlider("pitchSyncF1", v);     applyVoicingTone() }
    fun onPitchSyncB1Changed(v: Float)     { pitchSyncB1.value = v;     saveSlider("pitchSyncB1", v);     applyVoicingTone() }
    fun onSpeedQuotientChanged(v: Float)   { speedQuotient.value = v;   saveSlider("speedQuotient", v);   applyVoicingTone() }
    fun onAspirationTiltChanged(v: Float)  { aspirationTilt.value = v;  saveSlider("aspirationTilt", v);  applyVoicingTone() }
    fun onCascadeBwScaleChanged(v: Float)  { cascadeBwScale.value = v;  saveSlider("cascadeBwScale", v);  applyVoicingTone() }
    fun onVoiceTremorChanged(v: Float)     { voiceTremor.value = v;     saveSlider("voiceTremor", v);     applyVoicingTone() }

    fun onCreakinessChanged(v: Float)      { creakiness.value = v;      saveSlider("creakiness", v);      applyFrameExDefaults() }
    fun onBreathinessChanged(v: Float)     { breathiness.value = v;     saveSlider("breathiness", v);     applyFrameExDefaults() }
    fun onJitterChanged(v: Float)          { jitter.value = v;          saveSlider("jitter", v);          applyFrameExDefaults() }
    fun onShimmerChanged(v: Float)         { shimmer.value = v;         saveSlider("shimmer", v);         applyFrameExDefaults() }
    fun onGlottalSharpnessChanged(v: Float){ glottalSharpness.value = v; saveSlider("glottalSharpness", v); applyFrameExDefaults() }

    fun onPitchModeChanged(mode: String)       { pitchMode.value = mode;       saveString("pitchMode", mode);       applyPitchSettings() }
    fun onInflectionScaleChanged(v: Float)     { inflectionScale.value = v;    saveSlider("inflectionScale", v);    applyPitchSettings() }
    fun onInflectionChanged(v: Float)          { inflection.value = v;         saveSlider("inflection", v);         applyPitchSettings() }
    fun onOverrideSystemRateChanged(v: Boolean){ overrideSystemRate.value = v;  saveBool("overrideSystemRate", v) }
    fun onGlobalRateChanged(v: Float)          { globalRate.value = v;          saveSlider("globalRate", v) }
    fun onSystemVolumeChanged(v: Float)        { systemVolume.value = v;        saveSlider("systemVolume", v);       engine.setVolume(v) }
    fun onSampleRateChanged(index: Float) {
        sampleRateIndex.value = index
        val rate = SAMPLE_RATES[index.roundToInt().coerceIn(0, SAMPLE_RATES.size - 1)]
        saveInt("sampleRate", rate)
        engine.setSampleRate(rate)
    }
    fun onPauseModeChanged(mode: Int) {
        pauseMode.value = mode
        saveInt("pauseMode", mode)
        engine.setPauseMode(mode)
    }

    // ── Reset to defaults ──────────────────────────────────────────

    fun resetToDefaults() {
        // VoicingTone sliders
        voiceTilt.value = 50f;       onVoiceTiltChanged(50f)
        speedQuotient.value = 50f;   onSpeedQuotientChanged(50f)
        aspirationTilt.value = 50f;  onAspirationTiltChanged(50f)
        cascadeBwScale.value = 50f;  onCascadeBwScaleChanged(50f)
        noiseGlottalMod.value = 0f;  onNoiseGlottalModChanged(0f)
        pitchSyncF1.value = 50f;     onPitchSyncF1Changed(50f)
        pitchSyncB1.value = 50f;     onPitchSyncB1Changed(50f)
        voiceTremor.value = 0f;      onVoiceTremorChanged(0f)

        // FrameEx sliders
        creakiness.value = 0f;       onCreakinessChanged(0f)
        breathiness.value = 0f;      onBreathinessChanged(0f)
        jitter.value = 0f;           onJitterChanged(0f)
        shimmer.value = 0f;          onShimmerChanged(0f)
        glottalSharpness.value = 50f; onGlottalSharpnessChanged(50f)

        // Pitch
        onPitchModeChanged("espeak_style")
        inflectionScale.value = 58f; onInflectionScaleChanged(58f)
        inflection.value = 50f;      onInflectionChanged(50f)

        // System rate
        onOverrideSystemRateChanged(false)
        globalRate.value = 1.0f;     onGlobalRateChanged(1.0f)

        // Output
        onPauseModeChanged(1)  // short
        onSampleRateChanged(2f)  // 22050 Hz
        onSystemVolumeChanged(1.0f)

        // Reset language filter — all checked
        val allKeys = allLocaleEntries.map { it.first }.toSet()
        _enabledLocaleKeys.value = allKeys
        prefs.edit().remove(TgsbTtsService.PREF_SUPPORTED_LANGUAGES).apply()
    }

    // ── Slider → engine value mapping (matches NVDA driver math) ────

    private fun applyVoicingTone() {
        val tilt = (voiceTilt.value - 50f) * (24f / 50f)          // -24..+24 dB/oct
        val noiseMod = noiseGlottalMod.value / 100f               // 0..1
        val psF1 = (pitchSyncF1.value - 50f) * 1.2f               // -60..+60 Hz
        val psB1 = (pitchSyncB1.value - 50f) * 1.0f               // -50..+50 Hz

        val sqSlider = speedQuotient.value
        val sq = if (sqSlider <= 50f)
            0.5 + (sqSlider / 50.0) * 1.5                         // 0.5..2.0
        else
            2.0 + ((sqSlider - 50.0) / 50.0) * 2.0                // 2.0..4.0

        val aspTilt = (aspirationTilt.value - 50f) * 0.24f         // -12..+12 dB/oct

        val bwSlider = cascadeBwScale.value
        val bw = if (bwSlider <= 50f)
            2.0 - (bwSlider / 50.0) * 1.1                         // 2.0..0.9
        else
            0.9 - ((bwSlider - 50.0) / 50.0) * 0.6                // 0.9..0.3

        val tremor = (voiceTremor.value / 100f) * 0.4f             // 0..0.4

        engine.setVoicingTone(
            tilt.toDouble(), noiseMod.toDouble(),
            psF1.toDouble(), psB1.toDouble(),
            sq, aspTilt.toDouble(), bw, tremor.toDouble()
        )
    }

    private fun applyFrameExDefaults() {
        engine.setFrameExDefaults(
            (creakiness.value / 100f).toDouble(),
            (breathiness.value / 100f).toDouble(),
            (jitter.value / 100f).toDouble(),
            (shimmer.value / 100f).toDouble(),
            (glottalSharpness.value / 50f).toDouble()   // 0..2.0, 50=1.0
        )
    }

    private fun applyPitchSettings() {
        engine.setPitchMode(pitchMode.value)
        engine.setInflectionScale((inflectionScale.value / 100f).toDouble())
        engine.setInflection((inflection.value / 100f).toDouble())
    }

    // ── Language filter ─────────────────────────────────────────────

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

    // ── Helpers ─────────────────────────────────────────────────────

    private fun loadSlider(key: String, default: Float): Float =
        prefs.getFloat("${PREF_PREFIX}$key", default)

    private fun saveSlider(key: String, value: Float) {
        prefs.edit().putFloat("${PREF_PREFIX}$key", value).apply()
    }

    private fun loadString(key: String, default: String): String =
        prefs.getString("${PREF_PREFIX}$key", default) ?: default

    private fun saveString(key: String, value: String) {
        prefs.edit().putString("${PREF_PREFIX}$key", value).apply()
    }

    private fun loadBool(key: String, default: Boolean): Boolean =
        prefs.getBoolean("${PREF_PREFIX}$key", default)

    private fun saveBool(key: String, value: Boolean) {
        prefs.edit().putBoolean("${PREF_PREFIX}$key", value).apply()
    }

    private fun loadInt(key: String, default: Int): Int =
        prefs.getInt("${PREF_PREFIX}$key", default)

    private fun saveInt(key: String, value: Int) {
        prefs.edit().putInt("${PREF_PREFIX}$key", value).apply()
    }

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
