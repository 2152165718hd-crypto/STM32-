package com.stm32.envmonitor

import com.google.common.truth.Truth.assertThat
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.PropertySnapshot
import com.stm32.envmonitor.ui.threshold.ThresholdFieldUiState
import com.stm32.envmonitor.ui.threshold.mergeThresholdFields
import org.junit.Test

class ThresholdMergeTest {
    @Test
    fun mergeThresholdFields_keepsPendingValueWhenCloudSnapshotIsStale() {
        val currentFields = fieldsOf("temperature_threshold" to 35)
        val committedValues = currentFields.associate { it.definition.id to it.value }

        val result = mergeThresholdFields(
            snapshot = snapshotOf("temperature_threshold" to 30),
            currentFields = currentFields,
            committedValues = committedValues,
            pendingValues = mapOf("temperature_threshold" to 35),
            preserveDirtyFields = false,
        )

        assertThat(result.fields.valueOf("temperature_threshold")).isEqualTo(35)
        assertThat(result.remainingPendingValues)
            .containsEntry("temperature_threshold", 35)
        assertThat(result.committedValues["temperature_threshold"]).isEqualTo(35)
    }

    @Test
    fun mergeThresholdFields_clearsPendingValueWhenCloudCatchesUp() {
        val currentFields = fieldsOf("temperature_threshold" to 35)
        val committedValues = currentFields.associate { it.definition.id to it.value }

        val result = mergeThresholdFields(
            snapshot = snapshotOf("temperature_threshold" to 35),
            currentFields = currentFields,
            committedValues = committedValues,
            pendingValues = mapOf("temperature_threshold" to 35),
            preserveDirtyFields = true,
        )

        assertThat(result.fields.valueOf("temperature_threshold")).isEqualTo(35)
        assertThat(result.remainingPendingValues).isEmpty()
        assertThat(result.committedValues["temperature_threshold"]).isEqualTo(35)
    }

    @Test
    fun mergeThresholdFields_preservesDirtyValueDuringBackgroundSync() {
        val currentFields = fieldsOf("temperature_threshold" to 36)
        val committedValues = fieldsOf("temperature_threshold" to 35)
            .associate { it.definition.id to it.value }

        val result = mergeThresholdFields(
            snapshot = snapshotOf("temperature_threshold" to 30),
            currentFields = currentFields,
            committedValues = committedValues,
            pendingValues = mapOf("temperature_threshold" to 35),
            preserveDirtyFields = true,
        )

        assertThat(result.fields.valueOf("temperature_threshold")).isEqualTo(36)
        assertThat(result.committedValues["temperature_threshold"]).isEqualTo(35)
        assertThat(result.remainingPendingValues)
            .containsEntry("temperature_threshold", 35)
    }

    private fun fieldsOf(vararg overrides: Pair<String, Int>): List<ThresholdFieldUiState> {
        val overrideMap = overrides.toMap()
        return MetricCatalog.thresholds.map { definition ->
            ThresholdFieldUiState(
                definition = definition,
                value = overrideMap[definition.id] ?: definition.defaultValue,
            )
        }
    }

    private fun snapshotOf(vararg properties: Pair<String, Int>): LatestDeviceSnapshot {
        return LatestDeviceSnapshot(
            properties = properties.associate { (id, value) ->
                id to PropertySnapshot(
                    identifier = id,
                    rawValue = value.toString(),
                    timeMillis = null,
                )
            },
        )
    }

    private fun List<ThresholdFieldUiState>.valueOf(id: String): Int {
        return first { it.definition.id == id }.value
    }
}
