package com.stm32.envmonitor.ui.history

import android.graphics.Paint
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.stm32.envmonitor.R
import com.stm32.envmonitor.data.OneNetRepository
import com.stm32.envmonitor.ui.components.AppIcon
import com.stm32.envmonitor.ui.components.AppTopBar
import com.stm32.envmonitor.ui.components.CardSurface
import com.stm32.envmonitor.ui.components.CircleIconButton
import com.stm32.envmonitor.ui.components.InfoBanner
import com.stm32.envmonitor.ui.components.SectionTitle
import com.stm32.envmonitor.ui.theme.Background
import com.stm32.envmonitor.ui.theme.BrandBlue
import com.stm32.envmonitor.ui.theme.Danger
import com.stm32.envmonitor.ui.theme.DangerSurface
import com.stm32.envmonitor.ui.theme.SurfaceWhite
import com.stm32.envmonitor.ui.theme.TextPrimary
import com.stm32.envmonitor.ui.theme.TextSecondary

@Composable
fun HistoryRoute(
    repository: OneNetRepository,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    viewModel: HistoryViewModel = viewModel(factory = HistoryViewModel.factory(repository)),
) {
    val uiState = viewModel.uiState.collectAsStateWithLifecycle().value
    HistoryScreen(
        uiState = uiState,
        onBack = onBack,
        onRefresh = viewModel::loadHistory,
        onMetricSelected = viewModel::selectMetric,
        modifier = modifier,
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HistoryScreen(
    uiState: HistoryUiState,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    onMetricSelected: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .background(Background),
        contentPadding = PaddingValues(start = 20.dp, top = 16.dp, end = 20.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        item {
            AppTopBar(
                title = "历史曲线",
                subtitle = "最近 ${uiState.points.size.coerceAtLeast(1)} 个采样点 · ${uiState.rangeText}",
                leading = {
                    CircleIconButton(
                        iconRes = R.drawable.ic_back,
                        contentDescription = "返回",
                        onClick = onBack,
                    )
                },
                trailing = {
                    CircleIconButton(
                        iconRes = R.drawable.ic_refresh,
                        contentDescription = "刷新",
                        onClick = onRefresh,
                    )
                },
            )
        }

        item {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                uiState.metrics.forEach { metric ->
                    FilterChip(
                        selected = uiState.selectedMetricId == metric.id,
                        onClick = { onMetricSelected(metric.id) },
                        label = { Text(metric.title) },
                        leadingIcon = {
                            AppIcon(
                                painter = painterResource(metric.iconRes),
                                contentDescription = null,
                                tint = Color.Unspecified,
                                size = 16.dp,
                            )
                        },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = BrandBlue,
                            selectedLabelColor = Color.White,
                            selectedLeadingIconColor = Color.White,
                            containerColor = SurfaceWhite,
                        ),
                    )
                }
            }
        }

        if (!uiState.errorMessage.isNullOrBlank()) {
            item {
                InfoBanner(
                    text = uiState.errorMessage,
                    textColor = Danger,
                    containerColor = DangerSurface,
                )
            }
        }

        item {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                maxItemsInEachRow = 2,
            ) {
                uiState.statCards.forEach { stat ->
                    StatCard(
                        stat = stat,
                        modifier = Modifier.fillMaxWidth(0.48f),
                    )
                }
            }
        }

        item {
            CardSurface(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column {
                        Text(
                            text = uiState.selectedMetric.title,
                            color = TextPrimary,
                            fontSize = 22.sp,
                            fontWeight = FontWeight.Bold,
                        )
                        Text(
                            text = uiState.selectedMetric.unit,
                            color = TextSecondary,
                            fontSize = 13.sp,
                        )
                    }
                    Text(
                        text = uiState.chartCountText,
                        color = TextSecondary,
                        fontSize = 13.sp,
                    )
                }

                HistoryChart(
                    points = uiState.points,
                    range = uiState.chartRange,
                    accentColor = uiState.selectedMetric.accentColor,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 16.dp),
                )
            }
        }

        item {
            SectionTitle("最近记录")
        }

        if (uiState.recentPoints.isEmpty()) {
            item {
                InfoBanner(
                    text = "暂无历史数据",
                    textColor = TextSecondary,
                    containerColor = SurfaceWhite,
                )
            }
        } else {
            items(uiState.recentPoints, key = { it.key }) { point ->
                RecentHistoryRow(
                    point = point,
                    unit = uiState.selectedMetric.unit,
                )
            }
        }
    }
}

@Composable
private fun StatCard(stat: HistoryStatCard, modifier: Modifier = Modifier) {
    CardSurface(modifier = modifier) {
        Text(
            text = stat.label,
            color = TextSecondary,
            fontSize = 13.sp,
        )
        Row(
            modifier = Modifier.padding(top = 8.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            Text(
                text = stat.valueText,
                color = TextPrimary,
                fontSize = 26.sp,
                fontWeight = FontWeight.ExtraBold,
            )
            Text(
                text = stat.unit,
                modifier = Modifier.padding(start = 6.dp, bottom = 3.dp),
                color = TextSecondary,
                fontSize = 12.sp,
            )
        }
    }
}

@Composable
private fun RecentHistoryRow(point: HistoryChartPoint, unit: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(SurfaceWhite)
            .padding(horizontal = 16.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = point.timeText,
            color = TextSecondary,
            fontSize = 14.sp,
        )
        Text(
            text = "${point.valueText} $unit",
            color = TextPrimary,
            fontSize = 15.sp,
            fontWeight = FontWeight.Bold,
        )
    }
}

@Composable
private fun HistoryChart(
    points: List<HistoryChartPoint>,
    range: ChartRange,
    accentColor: Color,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .height(250.dp)
            .clip(RoundedCornerShape(18.dp))
            .background(Color.White),
    ) {
        androidx.compose.foundation.Canvas(
            modifier = Modifier.fillMaxSize(),
        ) {
            val left = 42.dp.toPx()
            val right = 12.dp.toPx()
            val top = 18.dp.toPx()
            val bottom = 34.dp.toPx()
            val plotWidth = size.width - left - right
            val plotHeight = size.height - top - bottom
            val plotBottom = top + plotHeight
            val span = (range.max - range.min).coerceAtLeast(1.0)
            val gridPaint = Paint().apply {
                color = Color(0xFFE6EDF5).toArgb()
                strokeWidth = 1.dp.toPx()
                isAntiAlias = true
            }
            val textPaint = Paint().apply {
                color = TextSecondary.toArgb()
                textSize = 11.sp.toPx()
                isAntiAlias = true
            }

            repeat(5) { index ->
                val y = top + (plotHeight / 4f) * index
                drawLine(
                    color = Color(0xFFE6EDF5),
                    start = Offset(left, y),
                    end = Offset(left + plotWidth, y),
                    strokeWidth = 1.dp.toPx(),
                )
                val value = range.max - (span / 4.0) * index
                drawIntoCanvas { canvas ->
                    canvas.nativeCanvas.drawText(
                        if (kotlin.math.abs(value) >= 100) value.toInt().toString() else String.format("%.1f", value),
                        0f,
                        y + 4.dp.toPx(),
                        textPaint,
                    )
                }
            }

            if (points.isEmpty()) {
                return@Canvas
            }

            val coordinates = points.mapIndexed { index, point ->
                val x = if (points.size == 1) {
                    left + plotWidth / 2f
                } else {
                    left + (plotWidth * index / (points.size - 1).toFloat())
                }
                val y = top + (((range.max - point.value) / span).toFloat() * plotHeight)
                Offset(x, y)
            }

            val linePath = Path().apply {
                moveTo(coordinates.first().x, coordinates.first().y)
                for (i in 1 until coordinates.size) {
                    lineTo(coordinates[i].x, coordinates[i].y)
                }
            }
            val areaPath = Path().apply {
                addPath(linePath)
                lineTo(coordinates.last().x, plotBottom)
                lineTo(coordinates.first().x, plotBottom)
                close()
            }

            drawPath(
                path = areaPath,
                brush = Brush.verticalGradient(
                    colors = listOf(accentColor.copy(alpha = 0.18f), accentColor.copy(alpha = 0.03f)),
                    startY = top,
                    endY = plotBottom,
                ),
            )
            drawPath(
                path = linePath,
                color = accentColor,
                style = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round),
            )

            if (coordinates.size <= 30) {
                coordinates.forEach { point ->
                    drawCircle(
                        color = Color.White,
                        radius = 4.dp.toPx(),
                        center = point,
                    )
                    drawCircle(
                        color = accentColor,
                        radius = 3.dp.toPx(),
                        center = point,
                    )
                }
            }

            val labelIndexes = listOf(0, points.lastIndex / 2, points.lastIndex).distinct()
            labelIndexes.forEach { index ->
                val label = points[index].axisLabel
                val x = coordinates[index].x
                drawIntoCanvas { canvas ->
                    canvas.nativeCanvas.drawText(
                        label,
                        x - 12.dp.toPx(),
                        size.height - 8.dp.toPx(),
                        textPaint,
                    )
                }
            }
        }

        if (points.isEmpty()) {
            Text(
                text = "暂无历史数据",
                modifier = Modifier.align(Alignment.Center),
                color = TextSecondary,
                fontSize = 14.sp,
            )
        }
    }
}
