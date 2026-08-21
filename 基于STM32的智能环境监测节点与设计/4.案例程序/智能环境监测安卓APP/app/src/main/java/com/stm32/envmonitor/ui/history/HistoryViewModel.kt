package com.stm32.envmonitor.ui.history

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.stm32.envmonitor.data.OneNetRepository
import com.stm32.envmonitor.model.HistoryPoint
import com.stm32.envmonitor.model.MetricCatalog
import com.stm32.envmonitor.model.MetricDef
import com.stm32.envmonitor.util.formatClockTime
import com.stm32.envmonitor.util.formatMetricValue
import com.stm32.envmonitor.util.toUiMessage
import kotlin.math.max
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class HistoryViewModel(
    private val repository: OneNetRepository,
) : ViewModel() {
    private val _uiState = MutableStateFlow(HistoryUiState())
    val uiState: StateFlow<HistoryUiState> = _uiState.asStateFlow()

    private var loadJob: Job? = null

    init {
        loadHistory()
    }

    fun selectMetric(metricId: String) {
        if (metricId == _uiState.value.selectedMetricId) {
            return
        }
        val metric = MetricCatalog.metricById(metricId)
        _uiState.update {
            it.copy(
                selectedMetricId = metricId,
                points = emptyList(),
                recentPoints = emptyList(),
                statCards = emptyStats(metric),
                rangeText = "--",
                axisText = "",
                chartCountText = "0 条",
                chartRange = ChartRange(metric.min, metric.max),
            )
        }
        loadHistory()
    }

    fun loadHistory() {
        loadJob?.cancel()
        val metric = _uiState.value.selectedMetric
        loadJob = viewModelScope.launch {
            _uiState.update { it.copy(isLoading = true, errorMessage = null) }
            runCatching { repository.fetchHistory(metric.id) }
                .onSuccess { history ->
                    _uiState.value = buildSuccessState(metric, history)
                }
                .onFailure { error ->
                    _uiState.update {
                        it.copy(
                            isLoading = false,
                            errorMessage = error.toUiMessage(),
                            points = emptyList(),
                            recentPoints = emptyList(),
                            statCards = emptyStats(metric),
                            rangeText = "--",
                            axisText = "",
                            chartCountText = "0 条",
                            chartRange = ChartRange(metric.min, metric.max),
                        )
                    }
                }
            _uiState.update { it.copy(isLoading = false) }
        }
    }

    private fun buildSuccessState(
        metric: MetricDef,
        history: List<HistoryPoint>,
    ): HistoryUiState {
        val trimmed = history.takeLast(60)
        val axisInfo = buildAxisInfo(trimmed)
        val points = trimmed.mapIndexed { index, point ->
            HistoryChartPoint(
                key = "$index-${point.timeMillis}",
                value = point.value,
                valueText = formatMetricValue(point.value),
                timeText = formatClockTime(point.timeMillis),
                axisLabel = formatAxisLabel(point.timeMillis, axisInfo),
                timeMillis = point.timeMillis,
            )
        }

        if (points.isEmpty()) {
            return _uiState.value.copy(
                isLoading = false,
                errorMessage = null,
                points = emptyList(),
                recentPoints = emptyList(),
                statCards = emptyStats(metric),
                rangeText = "--",
                axisText = "",
                chartCountText = "0 条",
                chartRange = ChartRange(metric.min, metric.max),
            )
        }

        val values = points.map { it.value }
        val latest = values.last()
        val maximum = values.maxOrNull() ?: metric.max
        val minimum = values.minOrNull() ?: metric.min
        val average = values.average()

        return _uiState.value.copy(
            isLoading = false,
            errorMessage = null,
            points = points,
            recentPoints = points.takeLast(6).reversed(),
            statCards = listOf(
                HistoryStatCard("最新", formatMetricValue(latest), metric.unit),
                HistoryStatCard("最高", formatMetricValue(maximum), metric.unit),
                HistoryStatCard("最低", formatMetricValue(minimum), metric.unit),
                HistoryStatCard("平均", formatMetricValue(average), metric.unit),
            ),
            rangeText = "${points.first().timeText} - ${points.last().timeText}",
            axisText = axisInfo.label,
            chartCountText = if (axisInfo.label.isBlank()) {
                "${points.size} 条"
            } else {
                "横轴/${axisInfo.label} · ${points.size} 条"
            },
            chartRange = buildChartRange(values, metric),
        )
    }

    private fun emptyStats(metric: MetricDef): List<HistoryStatCard> {
        return listOf("最新", "最高", "最低", "平均").map { label ->
            HistoryStatCard(label = label, valueText = "--", unit = metric.unit)
        }
    }

    private fun buildAxisInfo(points: List<HistoryPoint>): AxisInfo {
        if (points.size < 2) {
            return AxisInfo(startTime = points.firstOrNull()?.timeMillis ?: 0L, unitMs = 1_000L, suffix = "秒", decimals = 0, label = "秒")
        }

        val start = points.minOf { it.timeMillis }
        val end = points.maxOf { it.timeMillis }
        val span = max(end - start, 0L)

        return when {
            span < 2 * 60 * 1_000L -> AxisInfo(start, 1_000L, "秒", 0, "秒")
            span < 2 * 60 * 60 * 1_000L -> AxisInfo(start, 60_000L, "分", if (span < 10 * 60 * 1_000L) 1 else 0, "分钟")
            else -> AxisInfo(start, 3_600_000L, "时", if (span < 6 * 60 * 60 * 1_000L) 1 else 0, "小时")
        }
    }

    private fun formatAxisLabel(timestamp: Long, axisInfo: AxisInfo): String {
        val value = (timestamp - axisInfo.startTime).toDouble() / axisInfo.unitMs.toDouble()
        val text = if (axisInfo.decimals > 0) {
            String.format("%.1f", value)
        } else {
            value.toInt().toString()
        }
        return "$text${axisInfo.suffix}"
    }

    private fun buildChartRange(values: List<Double>, metric: MetricDef): ChartRange {
        if (values.isEmpty()) {
            return ChartRange(metric.min, metric.max)
        }

        var min = values.minOrNull() ?: metric.min
        var max = values.maxOrNull() ?: metric.max
        if (min == max) {
            val pad = kotlin.math.max(1.0, kotlin.math.abs(max) * 0.05)
            min -= pad
            max += pad
        } else {
            val pad = (max - min) * 0.12
            min -= pad
            max += pad
        }

        min = maxOf(metric.min, min)
        max = minOf(metric.max, max)
        if (max <= min) {
            max = min + 1
        }
        return ChartRange(min, max)
    }

    companion object {
        fun factory(repository: OneNetRepository): ViewModelProvider.Factory {
            return object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T {
                    return HistoryViewModel(repository) as T
                }
            }
        }
    }
}

private data class AxisInfo(
    val startTime: Long,
    val unitMs: Long,
    val suffix: String,
    val decimals: Int,
    val label: String,
)
