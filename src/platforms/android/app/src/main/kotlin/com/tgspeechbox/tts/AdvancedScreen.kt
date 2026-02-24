/*
 * AdvancedScreen — Tab 2: "Advanced".
 *
 * Voice quality sliders (VoicingTone + FrameEx) and a language filter
 * dialog behind a button.  All TalkBack-accessible.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

@Composable
fun AdvancedScreen(
    viewModel: TgsbViewModel,
    snackbarHostState: SnackbarHostState
) {
    var showLanguageDialog by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp)
    ) {
        // ── Voice Quality section ───────────────────────────────────
        Text(
            text = stringResource(R.string.voice_quality_title),
            style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.semantics { heading() }
        )

        Spacer(Modifier.height(12.dp))

        VoicingToneSlider(
            label = stringResource(R.string.voice_tilt_label),
            flow = viewModel.voiceTilt,
            onChange = { viewModel.onVoiceTiltChanged(it) },
            format = { v -> "${(v - 50).roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.speed_quotient_label),
            flow = viewModel.speedQuotient,
            onChange = { viewModel.onSpeedQuotientChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.aspiration_tilt_label),
            flow = viewModel.aspirationTilt,
            onChange = { viewModel.onAspirationTiltChanged(it) },
            format = { v -> "${(v - 50).roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.cascade_bw_label),
            flow = viewModel.cascadeBwScale,
            onChange = { viewModel.onCascadeBwScaleChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.noise_glottal_mod_label),
            flow = viewModel.noiseGlottalMod,
            onChange = { viewModel.onNoiseGlottalModChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.pitch_sync_f1_label),
            flow = viewModel.pitchSyncF1,
            onChange = { viewModel.onPitchSyncF1Changed(it) },
            format = { v -> "${(v - 50).roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.pitch_sync_b1_label),
            flow = viewModel.pitchSyncB1,
            onChange = { viewModel.onPitchSyncB1Changed(it) },
            format = { v -> "${(v - 50).roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.voice_tremor_label),
            flow = viewModel.voiceTremor,
            onChange = { viewModel.onVoiceTremorChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )

        Spacer(Modifier.height(20.dp))

        // ── Per-Frame Voice Quality section ─────────────────────────
        Text(
            text = stringResource(R.string.frame_quality_title),
            style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.semantics { heading() }
        )

        Spacer(Modifier.height(12.dp))

        VoicingToneSlider(
            label = stringResource(R.string.creakiness_label),
            flow = viewModel.creakiness,
            onChange = { viewModel.onCreakinessChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.breathiness_label),
            flow = viewModel.breathiness,
            onChange = { viewModel.onBreathinessChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.jitter_label),
            flow = viewModel.jitter,
            onChange = { viewModel.onJitterChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.shimmer_label),
            flow = viewModel.shimmer,
            onChange = { viewModel.onShimmerChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )
        VoicingToneSlider(
            label = stringResource(R.string.glottal_sharpness_label),
            flow = viewModel.glottalSharpness,
            onChange = { viewModel.onGlottalSharpnessChanged(it) },
            format = { v -> "${v.roundToInt()}" }
        )

        Spacer(Modifier.height(24.dp))

        // ── Engine Languages button ─────────────────────────────────
        OutlinedButton(
            onClick = { showLanguageDialog = true },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.choose_languages_button))
        }
    }

    // ── Language filter dialog ───────────────────────────────────────
    if (showLanguageDialog) {
        LanguageFilterDialog(
            viewModel = viewModel,
            snackbarHostState = snackbarHostState,
            onDismiss = { showLanguageDialog = false }
        )
    }
}

/**
 * Reusable slider with accessible label (single TalkBack swipe target).
 */
@Composable
private fun VoicingToneSlider(
    label: String,
    flow: MutableStateFlow<Float>,
    onChange: (Float) -> Unit,
    format: (Float) -> String
) {
    val value by flow.collectAsState()
    val displayValue = format(value)
    val fullLabel = "$label: $displayValue"

    Text(
        text = fullLabel,
        style = MaterialTheme.typography.bodyLarge,
        modifier = Modifier.clearAndSetSemantics {}
    )
    Slider(
        value = value,
        onValueChange = { onChange(it) },
        valueRange = 0f..100f,
        steps = 99,
        modifier = Modifier
            .fillMaxWidth()
            .semantics {
                contentDescription = fullLabel
                stateDescription = displayValue
            }
    )
    Spacer(Modifier.height(4.dp))
}

/**
 * Dialog with language checkboxes — moved out of the main scroll view.
 */
@Composable
private fun LanguageFilterDialog(
    viewModel: TgsbViewModel,
    snackbarHostState: SnackbarHostState,
    onDismiss: () -> Unit
) {
    val enabledKeys by viewModel.enabledLocaleKeys.collectAsState()
    val scope = rememberCoroutineScope()
    val errorMsg = stringResource(R.string.at_least_one_language)

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.supported_languages_title)) },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                Text(
                    text = stringResource(R.string.supported_languages_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))

                for ((localeKey, displayName) in viewModel.allLocaleEntries) {
                    val isChecked = localeKey in enabledKeys
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .toggleable(
                                value = isChecked,
                                onValueChange = { newValue ->
                                    val ok = viewModel.toggleLocaleKey(localeKey, newValue)
                                    if (!ok) {
                                        scope.launch {
                                            snackbarHostState.showSnackbar(errorMsg)
                                        }
                                    }
                                },
                                role = androidx.compose.ui.semantics.Role.Checkbox
                            )
                            .padding(vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(
                            checked = isChecked,
                            onCheckedChange = null
                        )
                        Text(
                            text = displayName,
                            style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier.padding(start = 12.dp)
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.done_button))
            }
        }
    )
}
