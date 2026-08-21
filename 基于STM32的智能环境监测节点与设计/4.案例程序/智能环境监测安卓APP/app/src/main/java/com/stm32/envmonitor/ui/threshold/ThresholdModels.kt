package com.stm32.envmonitor.ui.threshold

import com.stm32.envmonitor.data.OneNetPayloadParser
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.OneNetDeviceStatus
import com.stm32.envmonitor.model.ThresholdDef
import com.stm32.envmonitor.util.clampInt

data class ThresholdFieldUiState(
    val definition: ThresholdDef,
    val value: Int,
)

data class ThresholdUiState(
    val isLoading: Boolean = false,
    val isSaving: Boolean = false,
    val errorMessage: String? = null,
    val hasChanges: Boolean = false,
    val isAutoSyncing: Boolean = false,
    val cloudDeviceStatus: OneNetDeviceStatus = OneNetDeviceStatus.Unknown,
    val cloudLastSeenText: String = "--",
    val latestTelemetryText: String = "--",
    val isTelemetryFresh: Boolean = true,
    val fields: List<ThresholdFieldUiState> = MetricCatalog.thresholds.map {
        ThresholdFieldUiState(it, it.defaultValue)
    },
) {
    val canSend: Boolean
        get() = cloudDeviceStatus != OneNetDeviceStatus.Offline
}

internal data class ThresholdCloudMergeResult(
    val fields: List<ThresholdFieldUiState>,
    val committedValues: Map<String, Int>,
    val remainingPendingValues: Map<String, Int>,
)

internal fun mergeThresholdFields(
    snapshot: LatestDeviceSnapshot,
    currentFields: List<ThresholdFieldUiState>,
    committedValues: Map<String, Int>,
    pendingValues: Map<String, Int>,
    preserveDirtyFields: Boolean,
): ThresholdCloudMergeResult {
    val nextFields = mutableListOf<ThresholdFieldUiState>()
    val nextCommittedValues = committedValues.toMutableMap()
    val nextPendingValues = linkedMapOf<String, Int>()

    currentFields.forEach { field ->
        val id = field.definition.id
        val committedValue = committedValues[id] ?: field.definition.defaultValue
        val cloudValue = clampInt(
            OneNetPayloadParser.toIntOrNull(snapshot.valueOf(id)) ?: field.definition.defaultValue,
            field.definition.min,
            field.definition.max,
        )
        val pendingValue = pendingValues[id]
        val isDirty = preserveDirtyFields && field.value != committedValue
        val keepPending = pendingValue != null && cloudValue != pendingValue

        val displayValue = when {
            isDirty -> field.value
            keepPending -> pendingValue
            else -> cloudValue
        }

        nextFields += field.copy(value = displayValue)
        nextCommittedValues[id] = when {
            isDirty -> committedValue
            keepPending -> committedValue
            else -> displayValue
        }
        if (keepPending) {
            nextPendingValues[id] = pendingValue
        }
    }

    return ThresholdCloudMergeResult(
        fields = nextFields,
        committedValues = nextCommittedValues,
        remainingPendingValues = nextPendingValues,
    )
}
