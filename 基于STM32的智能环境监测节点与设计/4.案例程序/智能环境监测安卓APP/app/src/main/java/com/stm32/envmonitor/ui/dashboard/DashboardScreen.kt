package com.stm32.envmonitor.ui.dashboard

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
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
import com.stm32.envmonitor.ui.theme.BrandBlueSurface
import com.stm32.envmonitor.ui.theme.Danger
import com.stm32.envmonitor.ui.theme.DangerSurface
import com.stm32.envmonitor.ui.theme.OfflineSurface
import com.stm32.envmonitor.ui.theme.SurfaceWhite
import com.stm32.envmonitor.ui.theme.TextPrimary
import com.stm32.envmonitor.ui.theme.TextSecondary

@Composable
fun DashboardRoute(
    repository: OneNetRepository,
    onNavigateThreshold: () -> Unit,
    onNavigateHistory: () -> Unit,
    modifier: Modifier = Modifier,
    viewModel: DashboardViewModel = viewModel(factory = DashboardViewModel.factory(repository)),
) {
    val uiState = viewModel.uiState.collectAsStateWithLifecycle().value

    DisposableEffect(viewModel) {
        viewModel.startPolling()
        onDispose { viewModel.stopPolling() }
    }

    DashboardScreen(
        uiState = uiState,
        onRefresh = viewModel::manualRefresh,
        onNavigateThreshold = onNavigateThreshold,
        onNavigateHistory = onNavigateHistory,
        modifier = modifier,
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun DashboardScreen(
    uiState: DashboardUiState,
    onRefresh: () -> Unit,
    onNavigateThreshold: () -> Unit,
    onNavigateHistory: () -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier
            .fillMaxWidth()
            .background(Background),
        contentPadding = PaddingValues(start = 20.dp, top = 16.dp, end = 20.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp),
    ) {
        item {
            AppTopBar(
                title = "环境监测网关",
                subtitle = "OneNET 实时数据 · ${uiState.lastUpdateText}",
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
            StatusPanel(uiState = uiState)
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

        if (!uiState.isLoading && uiState.errorMessage.isNullOrBlank() && !uiState.hasData) {
            item {
                InfoBanner(
                    text = "云端已连通，但当前设备还没有可展示的最新属性，请确认主机已成功连接 OneNET 并完成属性上报。",
                    textColor = TextSecondary,
                    containerColor = SurfaceWhite,
                )
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                ActionButton(
                    iconRes = R.drawable.ic_threshold,
                    label = "阈值设置",
                    backgroundColor = BrandBlue,
                    contentColor = Color.White,
                    modifier = Modifier.weight(1f),
                    onClick = onNavigateThreshold,
                )
                ActionButton(
                    iconRes = R.drawable.ic_history,
                    label = "历史曲线",
                    backgroundColor = SurfaceWhite,
                    contentColor = TextPrimary,
                    modifier = Modifier.weight(1f),
                    onClick = onNavigateHistory,
                )
            }
        }

        item {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                maxItemsInEachRow = 2,
            ) {
                uiState.cards.forEach { card ->
                    MetricCard(
                        card = card,
                        modifier = Modifier.fillMaxWidth(0.48f),
                    )
                }
            }
        }

        item {
            SectionTitle("当前报警")
        }

        if (uiState.alarmList.isEmpty()) {
            item {
                InfoBanner(
                    text = "暂无报警",
                    textColor = TextSecondary,
                    containerColor = SurfaceWhite,
                )
            }
        } else {
            items(uiState.alarmList) { item ->
                AlarmRow(label = item)
            }
        }

        item {
            SectionTitle("报警阈值")
        }

        item {
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                uiState.thresholds.forEach { chip ->
                    ThresholdChip(chip = chip)
                }
            }
        }
    }
}

@Composable
private fun StatusPanel(uiState: DashboardUiState) {
    val containerColor = when (uiState.statusMode) {
        DashboardStatusMode.Alarm,
        DashboardStatusMode.Error,
        -> DangerSurface
        DashboardStatusMode.Offline,
        DashboardStatusMode.Stale,
        -> OfflineSurface
        else -> SurfaceWhite
    }

    CardSurface(
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(22.dp))
                .background(containerColor)
                .padding(18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(54.dp)
                    .clip(CircleShape)
                    .background(Color.White.copy(alpha = 0.72f)),
                contentAlignment = Alignment.Center,
            ) {
                AppIcon(
                    painter = painterResource(uiState.statusIconRes),
                    contentDescription = null,
                    tint = Color.Unspecified,
                    size = 24.dp,
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = uiState.statusTitle,
                    color = TextPrimary,
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold,
                )
                Text(
                    text = uiState.statusText,
                    color = TextSecondary,
                    fontSize = 14.sp,
                    lineHeight = 20.sp,
                )
            }
            Box(
                modifier = Modifier
                    .clip(CircleShape)
                    .background(BrandBlueSurface)
                    .padding(horizontal = 12.dp, vertical = 8.dp),
            ) {
                Text(
                    text = if (uiState.configured) "已配置" else "待配置",
                    color = BrandBlue,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium,
                )
            }
        }
    }
}

@Composable
private fun ActionButton(
    iconRes: Int,
    label: String,
    backgroundColor: Color,
    contentColor: Color,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    Button(
        onClick = onClick,
        modifier = modifier.height(54.dp),
        shape = RoundedCornerShape(18.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = backgroundColor,
            contentColor = contentColor,
        ),
        elevation = ButtonDefaults.buttonElevation(defaultElevation = 0.dp),
    ) {
        AppIcon(
            painter = painterResource(iconRes),
            contentDescription = null,
            tint = Color.Unspecified,
            size = 18.dp,
        )
        Text(
            text = label,
            modifier = Modifier.padding(start = 8.dp),
            fontWeight = FontWeight.SemiBold,
        )
    }
}

@Composable
private fun MetricCard(card: MetricCardUiState, modifier: Modifier = Modifier) {
    CardSurface(modifier = modifier.alpha(if (card.active) 1f else 0.55f)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .size(42.dp)
                    .clip(CircleShape)
                    .background(card.metric.surfaceColor),
                contentAlignment = Alignment.Center,
            ) {
                AppIcon(
                    painter = painterResource(card.metric.iconRes),
                    contentDescription = null,
                    tint = Color.Unspecified,
                    size = 20.dp,
                )
            }
            Text(
                text = card.metric.title,
                modifier = Modifier.padding(start = 10.dp),
                color = TextSecondary,
                fontSize = 14.sp,
            )
        }

        Text(
            text = card.valueText,
            modifier = Modifier.padding(top = 18.dp),
            color = TextPrimary,
            fontSize = 34.sp,
            fontWeight = FontWeight.ExtraBold,
        )
        Text(
            text = card.metric.unit,
            modifier = Modifier.padding(top = 4.dp),
            color = TextSecondary,
            fontSize = 13.sp,
        )
    }
}

@Composable
private fun AlarmRow(label: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(DangerSurface)
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        AppIcon(
            painter = painterResource(R.drawable.ic_alarm),
            contentDescription = null,
            tint = Color.Unspecified,
            size = 18.dp,
        )
        Text(
            text = label,
            color = Danger,
            fontSize = 15.sp,
            fontWeight = FontWeight.Medium,
        )
    }
}

@Composable
private fun ThresholdChip(chip: ThresholdChipUiState) {
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(18.dp))
            .background(SurfaceWhite)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(
            modifier = Modifier
                .size(28.dp)
                .clip(CircleShape)
                .background(chip.threshold.surfaceColor),
            contentAlignment = Alignment.Center,
        ) {
            AppIcon(
                painter = painterResource(chip.threshold.iconRes),
                contentDescription = null,
                tint = Color.Unspecified,
                size = 16.dp,
            )
        }
        Text(
            text = "${chip.threshold.title} ${chip.valueText}",
            color = TextPrimary,
            fontSize = 13.sp,
        )
    }
}
