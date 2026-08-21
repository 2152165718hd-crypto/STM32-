package com.stm32.envmonitor.ui.dashboard

import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.MetricDef
import com.stm32.envmonitor.model.ThresholdDef

enum class DashboardStatusMode {
    Waiting,
    Normal,
    Alarm,
    Offline,
    Stale,
    Error,
}

data class MetricCardUiState(
    val metric: MetricDef,
    val valueText: String,
    val active: Boolean,
)

data class ThresholdChipUiState(
    val threshold: ThresholdDef,
    val valueText: String,
)

data class DashboardUiState(
    val configured: Boolean = true,
    val hasData: Boolean = false,
    val isLoading: Boolean = false,
    val errorMessage: String? = null,
    val lastUpdateText: String = "--",
    val slaveOnline: Boolean = false,
    val dataFresh: Boolean = true,
    val statusMode: DashboardStatusMode = DashboardStatusMode.Waiting,
    val statusTitle: String = "等待上报",
    val statusText: String = "设备暂无已上报数据",
    val statusIconRes: Int = com.stm32.envmonitor.R.drawable.ic_cloud,
    val alarmActive: Boolean = false,
    val alarmList: List<String> = emptyList(),
    val cards: List<MetricCardUiState> = MetricCatalog.metrics.map {
        MetricCardUiState(metric = it, valueText = "--", active = false)
    },
    val thresholds: List<ThresholdChipUiState> = MetricCatalog.thresholds.map {
        ThresholdChipUiState(threshold = it, valueText = "${it.defaultValue}${it.unit}")
    },
)
