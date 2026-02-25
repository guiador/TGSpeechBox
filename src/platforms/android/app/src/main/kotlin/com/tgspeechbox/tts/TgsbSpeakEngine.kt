/*
 * TgsbSpeakEngine — Standalone TTS engine for the consumer Speak UI.
 *
 * Drives the full pipeline (eSpeak → nvspFrontend → speechPlayer)
 * directly via JNI, bypassing the Android TextToSpeechService.
 * This avoids TalkBack fighting our utterances for the shared TTS
 * audio path.  Audio is played via AudioTrack on a background thread.
 *
 * Mirrors iOS TgsbEngine.swift.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.content.Context
import android.content.res.AssetManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

class TgsbSpeakEngine(private val context: Context) {

    companion object {
        private const val TAG = "TgsbSpeak"
        private const val SAMPLE_RATE = 22050

        init {
            System.loadLibrary("tgspeechbox_jni")
        }
    }

    private var nativeHandle: Long = 0L
    @Volatile
    private var stopRequested = false
    private var synthThread: Thread? = null
    private var audioTrack: AudioTrack? = null

    var isSpeaking: Boolean = false
        private set

    var onSpeakingChanged: ((Boolean) -> Unit)? = null

    // ── JNI declarations ─────────────────────────────────────────────

    private external fun nativeCreate(
        espeakDataPath: String, packDirPath: String, sampleRate: Int
    ): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeSetVoice(handle: Long, voiceName: String)
    private external fun nativeSetLanguage(
        handle: Long, espeakLang: String, tgsbLang: String
    ): Int
    private external fun nativeQueueText(
        handle: Long, text: String, speed: Double, pitchHz: Double
    )
    private external fun nativePullAudio(
        handle: Long, outBuffer: ShortArray, maxSamples: Int
    ): Int
    private external fun nativeStop(handle: Long)
    private external fun nativeSetVoicingTone(
        handle: Long,
        voicedTiltDbPerOct: Double,
        noiseGlottalModDepth: Double,
        pitchSyncF1DeltaHz: Double,
        pitchSyncB1DeltaHz: Double,
        speedQuotient: Double,
        aspirationTiltDbPerOct: Double,
        cascadeBwScale: Double,
        tremorDepth: Double
    )
    private external fun nativeSetFrameExDefaults(
        handle: Long,
        creakiness: Double,
        breathiness: Double,
        jitter: Double,
        shimmer: Double,
        sharpness: Double
    )
    private external fun nativeSetPitchMode(handle: Long, mode: String): Int
    private external fun nativeSetInflectionScale(handle: Long, scale: Double)

    // ── Lifecycle ────────────────────────────────────────────────────

    fun start(): Boolean {
        if (nativeHandle != 0L) return true

        extractAssets()

        val filesDir = context.filesDir
        val espeakDataPath = filesDir.absolutePath
        val packDirPath = File(filesDir, "tgsb").absolutePath

        if (!File(espeakDataPath, "espeak-ng-data").exists()) {
            Log.e(TAG, "espeak-ng-data not found at $espeakDataPath")
            return false
        }

        nativeHandle = nativeCreate(espeakDataPath, packDirPath, SAMPLE_RATE)
        if (nativeHandle == 0L) {
            Log.e(TAG, "nativeCreate failed")
            return false
        }

        Log.i(TAG, "Engine started (handle=$nativeHandle)")
        return true
    }

    fun shutdown() {
        stop()
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    // ── Voice quality settings ─────────────────────────────────────

    fun setVoicingTone(
        voicedTiltDbPerOct: Double,
        noiseGlottalModDepth: Double,
        pitchSyncF1DeltaHz: Double,
        pitchSyncB1DeltaHz: Double,
        speedQuotient: Double,
        aspirationTiltDbPerOct: Double,
        cascadeBwScale: Double,
        tremorDepth: Double
    ) {
        if (nativeHandle == 0L) return
        nativeSetVoicingTone(
            nativeHandle,
            voicedTiltDbPerOct, noiseGlottalModDepth,
            pitchSyncF1DeltaHz, pitchSyncB1DeltaHz,
            speedQuotient, aspirationTiltDbPerOct,
            cascadeBwScale, tremorDepth
        )
    }

    fun setFrameExDefaults(
        creakiness: Double,
        breathiness: Double,
        jitter: Double,
        shimmer: Double,
        sharpness: Double
    ) {
        if (nativeHandle == 0L) return
        nativeSetFrameExDefaults(
            nativeHandle,
            creakiness, breathiness, jitter, shimmer, sharpness
        )
    }

    fun setPitchMode(mode: String): Boolean {
        if (nativeHandle == 0L) return false
        return nativeSetPitchMode(nativeHandle, mode) == 1
    }

    fun setInflectionScale(scale: Double) {
        if (nativeHandle == 0L) return
        nativeSetInflectionScale(nativeHandle, scale)
    }

    // ── Speak / Stop ─────────────────────────────────────────────────

    fun setLanguage(espeakLang: String, tgsbLang: String): Boolean {
        if (nativeHandle == 0L) return false
        val result = nativeSetLanguage(nativeHandle, espeakLang, tgsbLang)
        Log.i(TAG, "setLanguage($espeakLang, $tgsbLang) = $result")
        return result == 0
    }

    fun setVoice(voiceName: String) {
        if (nativeHandle == 0L) return
        nativeSetVoice(nativeHandle, voiceName)
    }

    fun speak(text: String, speed: Double, pitchHz: Double) {
        if (nativeHandle == 0L) return
        stop()

        stopRequested = false
        setSpeaking(true)

        synthThread = Thread({
            try {
                // Queue text (fast: eSpeak IPA + frontend frames)
                Log.i(TAG, "Queuing text: '${text.take(40)}' speed=$speed pitch=$pitchHz")
                nativeQueueText(nativeHandle, text, speed, pitchHz)

                // Pull all PCM into a buffer
                val chunk = ShortArray(4096)
                val allSamples = mutableListOf<Short>()

                while (!stopRequested) {
                    val n = nativePullAudio(nativeHandle, chunk, chunk.size)
                    if (n <= 0) break
                    for (i in 0 until n) allSamples.add(chunk[i])
                }

                Log.i(TAG, "Pulled ${allSamples.size} samples (stopRequested=$stopRequested)")

                if (stopRequested || allSamples.isEmpty()) {
                    setSpeaking(false)
                    return@Thread
                }

                // Play via AudioTrack (MODE_STREAM — blocks until done)
                val pcmArray = ShortArray(allSamples.size) { allSamples[it] }
                playPcm(pcmArray)
            } catch (e: Exception) {
                Log.e(TAG, "Synthesis error: ${e.message}", e)
                setSpeaking(false)
            }
        }, "TgsbSynth").also { it.start() }
    }

    fun stop() {
        stopRequested = true
        if (nativeHandle != 0L) nativeStop(nativeHandle)
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        synthThread?.join(500)
        synthThread = null
        setSpeaking(false)
    }

    // ── Audio playback ───────────────────────────────────────────────

    private fun playPcm(samples: ShortArray) {
        val minBuf = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT
        )

        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build()
            )
            .setBufferSizeInBytes(minBuf)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        audioTrack = track
        track.play()
        Log.i(TAG, "Playing ${samples.size} samples via MODE_STREAM")

        // Blocking write — feeds audio to the track in chunks.
        // write() in MODE_STREAM blocks when the buffer is full,
        // so this naturally paces playback.
        var offset = 0
        while (offset < samples.size && !stopRequested) {
            val written = track.write(samples, offset, samples.size - offset)
            if (written < 0) {
                Log.e(TAG, "AudioTrack.write error: $written")
                break
            }
            offset += written
        }

        // Wait for the track to finish playing the buffered audio
        if (!stopRequested) {
            // Drain: write silence equal to the internal buffer to flush
            // all real audio through the hardware.
            val silence = ShortArray(minBuf / 2)
            track.write(silence, 0, silence.size)
        }

        track.stop()
        track.release()
        audioTrack = null
        setSpeaking(false)
        Log.i(TAG, "Playback finished")
    }

    private fun setSpeaking(value: Boolean) {
        isSpeaking = value
        onSpeakingChanged?.invoke(value)
    }

    // ── Asset extraction (same logic as TgsbTtsService) ─────────────

    private fun extractAssets() {
        val assetVersion = 4
        val marker = File(context.filesDir, ".assets_v$assetVersion")
        if (marker.exists()) return

        context.filesDir.listFiles()
            ?.filter { it.name.startsWith(".assets_") }
            ?.forEach { it.delete() }
        File(context.filesDir, "espeak-ng-data").deleteRecursively()
        File(context.filesDir, "tgsb").deleteRecursively()

        Log.i(TAG, "Extracting assets to ${context.filesDir.absolutePath}")
        copyAssetsDir("espeak-ng-data", File(context.filesDir, "espeak-ng-data"))
        copyAssetsDir("tgsb", File(context.filesDir, "tgsb"))
        marker.createNewFile()
    }

    private fun copyAssetsDir(assetPath: String, targetDir: File) {
        val entries = context.assets.list(assetPath) ?: return
        if (entries.isEmpty()) {
            copyAssetFile(assetPath, targetDir)
            return
        }
        targetDir.mkdirs()
        for (entry in entries) {
            val childAsset = "$assetPath/$entry"
            val childTarget = File(targetDir, entry)
            val subEntries = context.assets.list(childAsset)
            if (subEntries != null && subEntries.isNotEmpty()) {
                copyAssetsDir(childAsset, childTarget)
            } else {
                copyAssetFile(childAsset, childTarget)
            }
        }
    }

    private fun copyAssetFile(assetPath: String, target: File) {
        try {
            target.parentFile?.mkdirs()
            context.assets.open(assetPath).use { input ->
                FileOutputStream(target).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: IOException) {
            Log.e(TAG, "Failed to copy asset $assetPath: ${e.message}")
        }
    }
}
