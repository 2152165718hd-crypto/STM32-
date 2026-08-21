package com.stm32.envmonitor.ui.history

import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.MetricDef

data class HistoryChartPoint(
    val key: String,
    val value: Double,
    val valueText: String,
    val timeText: String,
    val axisLabel: String,
    val timeMillis: Long,
)

data class HistoryStatCard(
    val label: String,
    val valueText: String,
    val unit: String,
)

data class ChartRange(
    val min: Double,
    val max: Double,
)

data class HistoryUiState(
    val isLoading: Boolean = false,
    val errorMessage: String? = null,
    val metrics: List<MetricDef> = MetricCatalog.metrics,
    val selectedMetricId: String = MetricCatalog.metrics.first().id,
    val points: List<HistoryChartPoint> = emptyList(),
    val recentPoints: List<HistoryChartPoint> = emptyList(),
    val statCards: List<HistoryStatCard> = listOf(
        HistoryStatCard("最新", "--", MetricCatalog.metrics.first().unit),
        HistoryStatCard("最高", "--", MetricCatalog.metrics.first().unit),
        HistoryStatCard("最低", "--", MetricCatalog.metrics.first().unit),
        HistoryStatCard("平均", "--", MetricCatalog.metrics.first().unit),
    ),
    val rangeText: String = "--",
    val axisText: String = "",
    val chartCountText: String = "0 条",
    val chartRange: ChartRange = ChartRange(
        min = MetricCatalog.metrics.first().min,
        max = MetricCatalog.metrics.first().max,
    ),
) {
    val selectedMetric: MetricDef
        get() = metrics.first { it.id == selectedMetricId }
}
