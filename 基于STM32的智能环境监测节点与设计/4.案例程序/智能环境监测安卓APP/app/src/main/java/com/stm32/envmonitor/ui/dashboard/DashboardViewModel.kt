package com.stm32.envmonitor.ui.dashboard

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.stm32.envmonitor.R
import com.stm32.envmonitor.data.OneNetPayloadParser
import com.stm32.envmonitor.data.OneNetRepository
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.MetricDef
import com.stm32.envmonitor.util.AlarmMaskParser
import com.stm32.envmonitor.util.formatClockTime
import com.stm32.envmonitor.util.formatMetricValue
import com.stm32.envmonitor.util.toUiMessage
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

class DashboardViewModel(
    private val repository: OneNetRepository,
) : ViewModel() {
    private val _uiState = MutableStateFlow(
        DashboardUiState(configured = repository.config.isConfigured),
    )
    val uiState: StateFlow<DashboardUiState> = _uiState.asStateFlow()

    private var pollingJob: Job? = null
    private var refreshJob: Job? = null

    fun startPolling() {
        if (pollingJob?.isActive == true) {
            return
        }
        pollingJob = viewModelScope.launch {
            refresh(showLoading = !_uiState.value.hasData)
            while (isActive) {
                delay(repository.config.refreshIntervalMs)
                refresh(showLoading = false)
            }
        }
    }

    fun stopPolling() {
        pollingJob?.cancel()
        pollingJob = null
    }

    fun manualRefresh() {
        viewModelScope.launch {
            refresh(showLoading = true)
        }
    }

    private suspend fun refresh(showLoading: Boolean) {
        if (refreshJob?.isActive == true) {
            return
        }

        refreshJob = viewModelScope.launch {
            if (showLoading) {
                _uiState.update { it.copy(isLoading = true, errorMessage = null) }
            }

            runCatching { repository.fetchLatestSnapshot() }
                .onSuccess { snapshot ->
                    _uiState.value = buildSuccessState(snapshot)
                }
                .onFailure { error ->
                    _uiState.update { current ->
                        val hasCachedData = current.hasData
                        current.copy(
                            isLoading = false,
                            errorMessage = error.toUiMessage(),
                            statusMode = DashboardStatusMode.Error,
                            statusTitle = if (hasCachedData) "刷新失败" else "请求失败",
                            statusText = if (hasCachedData) {
                                "OneNET 刷新失败，已保留上次数据"
                            } else {
                                "无法读取 OneNET 最新数据"
                            },
                            statusIconRes = R.drawable.ic_offline,
                        )
                    }
                }

            _uiState.update { it.copy(isLoading = false) }
        }

        refreshJob?.join()
    }

    private fun buildSuccessState(snapshot: LatestDeviceSnapshot): DashboardUiState {
        val hasData = snapshot.properties.isNotEmpty()
        val latestTimestamp = snapshot.properties.values.mapNotNull { it.timeMillis }.maxOrNull()
        val telemetryTimestamp = MetricCatalog.telemetryIds
            .mapNotNull(snapshot::timeOf)
            .maxOrNull()

        val dataFresh = !hasData || telemetryTimestamp == null ||
            (System.currentTimeMillis() - telemetryTimestamp) <= repository.config.staleDataMs
        val slaveOnline = hasData && dataFresh &&
            OneNetPayloadParser.toBoolean(snapshot.valueOf("slave_online"), false)

        val cards = MetricCatalog.metrics.map { metric ->
            val active = dataFresh && slaveOnline && isMetricActive(metric, snapshot)
            val valueText = if (active) {
                formatMetricValue(
                    OneNetPayloadParser.toDoubleOrNull(snapshot.valueOf(metric.id)) ?: Double.NaN,
                )
            } else {
                "--"
            }
            MetricCardUiState(metric = metric, valueText = valueText, active = active)
        }

        val thresholds = MetricCatalog.thresholds.map { threshold ->
            val value = OneNetPayloadParser.toIntOrNull(snapshot.valueOf(threshold.id)) ?: threshold.defaultValue
            ThresholdChipUiState(
                threshold = threshold,
                valueText = "${value}${threshold.unit}",
            )
        }

        val alarmMask = if (dataFresh && slaveOnline) {
            OneNetPayloadParser.toIntOrNull(snapshot.valueOf("alarm_mask")) ?: 0
        } else {
            0
        }
        val alarmActive = dataFresh && slaveOnline &&
            (OneNetPayloadParser.toBoolean(snapshot.valueOf("alarm_active"), false) || alarmMask != 0)
        val alarmList = if (hasData && dataFresh) {
            AlarmMaskParser.parse(alarmMask, fallbackActive = alarmActive)
        } else {
            emptyList()
        }

        val status = buildStatus(
            hasData = hasData,
            dataFresh = dataFresh,
            slaveOnline = slaveOnline,
            alarmActive = alarmActive,
        )

        return DashboardUiState(
            configured = repository.config.isConfigured,
            hasData = hasData,
            isLoading = false,
            errorMessage = null,
            lastUpdateText = formatClockTime(latestTimestamp),
            slaveOnline = slaveOnline,
            dataFresh = dataFresh,
            statusMode = status.mode,
            statusTitle = status.title,
            statusText = status.text,
            statusIconRes = status.iconRes,
            alarmActive = alarmActive,
            alarmList = alarmList,
            cards = cards,
            thresholds = thresholds,
        )
    }

    private fun isMetricActive(metric: MetricDef, snapshot: LatestDeviceSnapshot): Boolean {
        val value = snapshot.valueOf(metric.id)
        if (value.isNullOrBlank()) {
            return false
        }

        val validBits = OneNetPayloadParser.toIntOrNull(snapshot.valueOf("valid_bits"))
        val requiredBit = metric.validBit ?: return true
        return validBits == null || validBits and requiredBit != 0
    }

    private fun buildStatus(
        hasData: Boolean,
        dataFresh: Boolean,
        slaveOnline: Boolean,
        alarmActive: Boolean,
    ): StatusDescriptor {
        if (!hasData) {
            return StatusDescriptor(
                mode = DashboardStatusMode.Waiting,
                title = "等待上报",
                text = "云端已连通，但当前还没有可展示的最新属性",
                iconRes = R.drawable.ic_cloud,
            )
        }

        if (!dataFresh) {
            return StatusDescriptor(
                mode = DashboardStatusMode.Stale,
                title = "数据超时",
                text = "云端仍是旧数据，设备可能已经离线",
                iconRes = R.drawable.ic_offline,
            )
        }

        if (!slaveOnline) {
            return StatusDescriptor(
                mode = DashboardStatusMode.Offline,
                title = "检测端离线",
                text = "主机在线，但检测端未返回新数据",
                iconRes = R.drawable.ic_offline,
            )
        }

        if (alarmActive) {
            return StatusDescriptor(
                mode = DashboardStatusMode.Alarm,
                title = "报警中",
                text = "检测端在线，请尽快处理当前报警",
                iconRes = R.drawable.ic_alarm,
            )
        }

        return StatusDescriptor(
            mode = DashboardStatusMode.Normal,
            title = "运行正常",
            text = "检测端在线，OneNET 数据刷新正常",
            iconRes = R.drawable.ic_cloud,
        )
    }

    companion object {
        fun factory(repository: OneNetRepository): ViewModelProvider.Factory {
            return object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T {
                    return DashboardViewModel(repository) as T
                }
            }
        }
    }
}

private data class StatusDescriptor(
    val mode: DashboardStatusMode,
    val title: String,
    val text: String,
    val iconRes: Int,
)
