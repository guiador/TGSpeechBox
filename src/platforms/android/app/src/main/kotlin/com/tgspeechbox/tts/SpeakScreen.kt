/*
 * SpeakScreen — Tab 1: "Speak & Basics".
 *
 * Text input, language/voice pickers, speed/pitch sliders, speak/stop.
 * All TalkBack-accessible.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SpeakScreen(viewModel: TgsbViewModel) {
    val text by viewModel.textToSpeak.collectAsState()
    val langIndex by viewModel.selectedLanguageIndex.collectAsState()
    val voiceIndex by viewModel.selectedVoiceIndex.collectAsState()
    val speed by viewModel.speedRate.collectAsState()
    val pitch by viewModel.pitchHz.collectAsState()
    val speaking by viewModel.isSpeaking.collectAsState()
    val ready by viewModel.engineReady.collectAsState()
    val errorMsg by viewModel.errorMessage.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(20.dp)
    ) {
        // ── Scrollable controls ─────────────────────────────────────
        Column(
            modifier = Modifier
                .weight(1f)
                .verticalScroll(rememberScrollState())
        ) {
            // Title
            Text(
                text = stringResource(R.string.app_name),
                style = MaterialTheme.typography.headlineLarge,
                modifier = Modifier.semantics { heading() }
            )
            Text(
                text = stringResource(R.string.speak_subtitle),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(Modifier.height(16.dp))

            // Text input
            OutlinedTextField(
                value = text,
                onValueChange = { viewModel.textToSpeak.value = it },
                label = { Text(stringResource(R.string.text_input_label)) },
                placeholder = { Text(stringResource(R.string.text_input_placeholder)) },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp),
                maxLines = 6
            )

            Spacer(Modifier.height(16.dp))

            // Language dropdown
            DropdownPicker(
                label = stringResource(R.string.language_label),
                items = viewModel.languages.map { it.displayName },
                selectedIndex = langIndex,
                onSelected = { viewModel.onLanguageSelected(it) }
            )

            Spacer(Modifier.height(12.dp))

            // Voice dropdown
            DropdownPicker(
                label = stringResource(R.string.voice_label),
                items = viewModel.voices.map { it.label },
                selectedIndex = voiceIndex,
                onSelected = { viewModel.onVoiceSelected(it) }
            )

            Spacer(Modifier.height(16.dp))

            // Speed slider — visual label hidden from TalkBack; slider carries
            // the full accessible name so it's a single swipe target.
            val speedText = stringResource(R.string.speed_value, speed)
            val speedLabel = stringResource(R.string.speed_label)
            Text(
                text = "$speedLabel: $speedText",
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.clearAndSetSemantics {}
            )
            Slider(
                value = speed,
                onValueChange = { viewModel.speedRate.value = it },
                valueRange = 0.3f..3.0f,
                steps = 26,
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics {
                        contentDescription = "$speedLabel: $speedText"
                        stateDescription = speedText
                    }
            )

            Spacer(Modifier.height(8.dp))

            // Pitch slider
            val pitchInt = pitch.roundToInt()
            val pitchText = stringResource(R.string.pitch_value, pitchInt)
            val pitchLabel = stringResource(R.string.pitch_label)
            Text(
                text = "$pitchLabel: $pitchText",
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.clearAndSetSemantics {}
            )
            Slider(
                value = pitch,
                onValueChange = { viewModel.pitchHz.value = it },
                valueRange = 40f..300f,
                steps = 51,
                modifier = Modifier
                    .fillMaxWidth()
                    .semantics {
                        contentDescription = "$pitchLabel: $pitchText"
                        stateDescription = pitchText
                    }
            )

        }

        // ── Pinned buttons at bottom ────────────────────────────────
        Spacer(Modifier.height(12.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Button(
                onClick = { viewModel.speak() },
                enabled = ready && !speaking && text.isNotBlank(),
                modifier = Modifier.weight(1f)
            ) {
                Text(stringResource(R.string.speak_button))
            }

            OutlinedButton(
                onClick = { viewModel.stop() },
                enabled = speaking,
                modifier = Modifier.weight(1f)
            ) {
                Text(stringResource(R.string.stop_button))
            }
        }

        // Error / status messages
        if (errorMsg != null) {
            Spacer(Modifier.height(8.dp))
            Text(
                text = errorMsg ?: "",
                color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodySmall
            )
        } else if (!ready) {
            Spacer(Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.tts_connecting),
                style = MaterialTheme.typography.bodySmall
            )
        }
    }
}

/**
 * Non-editable dropdown picker.
 *
 * Uses a clickable Surface instead of OutlinedTextField so TalkBack
 * announces it as a dropdown, not an "edit box".
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DropdownPicker(
    label: String,
    items: List<String>,
    selectedIndex: Int,
    onSelected: (Int) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    val selectedText = items.getOrElse(selectedIndex) { "" }

    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it }
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .menuAnchor(MenuAnchorType.PrimaryNotEditable)
                .semantics {
                    role = Role.DropdownList
                    contentDescription = "$label: $selectedText"
                },
            shape = MaterialTheme.shapes.small,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text(
                        text = label,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.clearAndSetSemantics {}
                    )
                    Text(
                        text = selectedText,
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.clearAndSetSemantics {}
                    )
                }
                ExposedDropdownMenuDefaults.TrailingIcon(expanded)
            }
        }

        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            items.forEachIndexed { index, item ->
                DropdownMenuItem(
                    text = { Text(item) },
                    onClick = {
                        onSelected(index)
                        expanded = false
                    },
                    contentPadding = ExposedDropdownMenuDefaults.ItemContentPadding
                )
            }
        }
    }
}
