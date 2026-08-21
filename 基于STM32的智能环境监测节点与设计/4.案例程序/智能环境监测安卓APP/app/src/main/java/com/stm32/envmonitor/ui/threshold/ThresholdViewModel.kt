package com.stm32.envmonitor.ui.threshold

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.stm32.envmonitor.data.OneNetApiException
import com.stm32.envmonitor.data.OneNetCommandTimeoutException
import com.stm32.envmonitor.data.OneNetRepository
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.OneNetDeviceStatus
import com.stm32.envmonitor.model.OneNetDeviceStatusSnapshot
import com.stm32.envmonitor.util.formatClockTime
import com.stm32.envmonitor.util.toUiMessage
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class ThresholdViewModel(
    private val repository: OneNetRepository,
) : ViewModel() {
    private val _uiState = MutableStateFlow(ThresholdUiState())
    val uiState: StateFlow<ThresholdUiState> = _uiState.asStateFlow()

    private val _events = MutableSharedFlow<String>(extraBufferCapacity = 1)
    val events: SharedFlow<String> = _events.asSharedFlow()

    private var savedValues: Map<String, Int> =
        MetricCatalog.thresholds.associate { it.id to it.defaultValue }
    private var pendingSyncValues: Map<String, Int> = emptyMap()
    private var autoSyncJob: Job? = null

    init {
        loadLatest()
    }

    fun loadLatest() {
        viewModelScope.launch {
            refreshFromCloud(
                showLoading = true,
                preserveLocalEdits = false,
                showErrors = true,
            )
        }
    }

    fun updateField(id: String, rawValue: Int) {
        val fields = _uiState.value.fields.map { field ->
            if (field.definition.id != id) {
                field
            } else {
                field.copy(
                    value = rawValue.coerceIn(field.definition.min, field.definition.max),
                )
            }
        }
        _uiState.update {
            it.copy(
                fields = fields,
                hasChanges = hasChanges(fields),
            )
        }
    }

    fun saveThresholds() {
        val currentState = _uiState.value
        if (currentState.isSaving) {
            return
        }

        if (currentState.cloudDeviceStatus == OneNetDeviceStatus.Offline) {
            _events.tryEmit("OneNET 当前判定主机离线，阈值无法下发")
            return
        }

        val params = buildMap {
            currentState.fields.forEach { field ->
                val value = field.value.coerceIn(field.definition.min, field.definition.max)
                if (savedValues[field.definition.id] != value) {
                    put(field.definition.id, value)
                }
            }
        }

        if (params.isEmpty()) {
            _events.tryEmit("阈值未变化")
            return
        }

        viewModelScope.launch {
            _uiState.update { it.copy(isSaving = true, errorMessage = null) }
            runCatching { repository.setDeviceProperties(params) }
                .onSuccess {
                    savedValues = _uiState.value.fields.associate { it.definition.id to it.value }
                    pendingSyncValues = pendingSyncValues + params.mapValues { (id, _) ->
                        savedValues.getValue(id)
                    }
                    _uiState.update {
                        it.copy(
                            isSaving = false,
                            hasChanges = false,
                            isAutoSyncing = pendingSyncValues.isNotEmpty(),
                        )
                    }
                    _events.emit("阈值已下发")
                    startAutoSync()
                }
                .onFailure { error ->
                    val message = when (error) {
                        is OneNetApiException -> {
                            if (error.code == 10411) {
                                "OneNET 返回设备不在线，当前无法下发阈值"
                            } else {
                                error.toUiMessage()
                            }
                        }

                        is OneNetCommandTimeoutException -> error.toUiMessage()
                        else -> error.toUiMessage()
                    }

                    _uiState.update {
                        it.copy(
                            isSaving = false,
                            errorMessage = message,
                            isAutoSyncing = pendingSyncValues.isNotEmpty(),
                        )
                    }
                    _events.emit(message)
                    loadLatest()
                }
        }
    }

    private suspend fun refreshFromCloud(
        showLoading: Boolean,
        preserveLocalEdits: Boolean,
        showErrors: Boolean,
    ): Boolean {
        if (showLoading) {
            _uiState.update { it.copy(isLoading = true, errorMessage = null) }
        }

        return runCatching { fetchCloudState() }
            .onSuccess { (snapshot, deviceStatus) ->
                applyCloudState(
                    snapshot = snapshot,
                    deviceStatus = deviceStatus,
                    preserveLocalEdits = preserveLocalEdits,
                )
            }
            .onFailure { error ->
                if (showErrors || showLoading) {
                    _uiState.update {
                        it.copy(
                            isLoading = false,
                            errorMessage = if (showErrors) error.toUiMessage() else it.errorMessage,
                            isAutoSyncing = pendingSyncValues.isNotEmpty(),
                        )
                    }
                }
            }
            .isSuccess
    }

    private suspend fun fetchCloudState(): Pair<LatestDeviceSnapshot, OneNetDeviceStatusSnapshot> =
        coroutineScope {
            val snapshotDeferred = async { repository.fetchLatestSnapshot() }
            val deviceStatusDeferred = async { repository.fetchDeviceStatus() }
            snapshotDeferred.await() to deviceStatusDeferred.await()
        }

    private fun applyCloudState(
        snapshot: LatestDeviceSnapshot,
        deviceStatus: OneNetDeviceStatusSnapshot,
        preserveLocalEdits: Boolean,
    ) {
        val latestTelemetryTime = MetricCatalog.telemetryIds
            .mapNotNull(snapshot::timeOf)
            .maxOrNull()
        val isTelemetryFresh = latestTelemetryTime == null ||
            (System.currentTimeMillis() - latestTelemetryTime) <= repository.config.staleDataMs
        val mergeResult = mergeThresholdFields(
            snapshot = snapshot,
            currentFields = _uiState.value.fields,
            committedValues = savedValues,
            pendingValues = pendingSyncValues,
            preserveDirtyFields = preserveLocalEdits,
        )

        pendingSyncValues = mergeResult.remainingPendingValues
        savedValues = mergeResult.committedValues

        _uiState.update {
            it.copy(
                isLoading = false,
                isSaving = false,
                errorMessage = null,
                hasChanges = hasChanges(mergeResult.fields),
                isAutoSyncing = pendingSyncValues.isNotEmpty(),
                cloudDeviceStatus = deviceStatus.status,
                cloudLastSeenText = formatClockTime(deviceStatus.lastSeenMillis),
                latestTelemetryText = formatClockTime(latestTelemetryTime),
                isTelemetryFresh = isTelemetryFresh,
                fields = mergeResult.fields,
            )
        }
    }

    private fun startAutoSync() {
        autoSyncJob?.cancel()
        if (pendingSyncValues.isEmpty()) {
            _uiState.update { it.copy(isAutoSyncing = false) }
            return
        }

        autoSyncJob = viewModelScope.launch {
            repeat(AUTO_SYNC_ATTEMPTS) {
                delay(repository.config.refreshIntervalMs)
                refreshFromCloud(
                    showLoading = false,
                    preserveLocalEdits = true,
                    showErrors = false,
                )
                if (pendingSyncValues.isEmpty()) {
                    return@launch
                }
            }
        }
    }

    private fun hasChanges(fields: List<ThresholdFieldUiState>): Boolean {
        return fields.any { savedValues[it.definition.id] != it.value }
    }

    override fun onCleared() {
        autoSyncJob?.cancel()
        super.onCleared()
    }

    companion object {
        private const val AUTO_SYNC_ATTEMPTS = 5

        fun factory(repository: OneNetRepository): ViewModelProvider.Factory {
            return object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T {
                    return ThresholdViewModel(repository) as T
                }
            }
        }
    }
}
