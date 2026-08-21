package com.stm32.envmonitor.ui.threshold

import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.stm32.envmonitor.R
import com.stm32.envmonitor.data.OneNetRepository
import com.stm32.envmonitor.model.OneNetDeviceStatus
import com.stm32.envmonitor.ui.components.AppIcon
import com.stm32.envmonitor.ui.components.AppTopBar
import com.stm32.envmonitor.ui.components.CardSurface
import com.stm32.envmonitor.ui.components.CircleIconButton
import com.stm32.envmonitor.ui.components.InfoBanner
import com.stm32.envmonitor.ui.theme.Background
import com.stm32.envmonitor.ui.theme.BorderMuted
import com.stm32.envmonitor.ui.theme.BrandBlue
import com.stm32.envmonitor.ui.theme.Danger
import com.stm32.envmonitor.ui.theme.DangerSurface
import com.stm32.envmonitor.ui.theme.OfflineSurface
import com.stm32.envmonitor.ui.theme.SurfaceWhite
import com.stm32.envmonitor.ui.theme.TextPrimary
import com.stm32.envmonitor.ui.theme.TextSecondary

@Composable
fun ThresholdRoute(
    repository: OneNetRepository,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    viewModel: ThresholdViewModel = viewModel(factory = ThresholdViewModel.factory(repository)),
) {
    val uiState = viewModel.uiState.collectAsStateWithLifecycle().value
    val context = LocalContext.current

    LaunchedEffect(viewModel) {
        viewModel.events.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    ThresholdScreen(
        uiState = uiState,
        onBack = onBack,
        onRefresh = viewModel::loadLatest,
        onValueChange = viewModel::updateField,
        onSave = viewModel::saveThresholds,
        modifier = modifier,
    )
}

@Composable
fun ThresholdScreen(
    uiState: ThresholdUiState,
    onBack: () -> Unit,
    onRefresh: () -> Unit,
    onValueChange: (String, Int) -> Unit,
    onSave: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier.fillMaxSize(),
        containerColor = Background,
        bottomBar = {
            Button(
                onClick = onSave,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 16.dp)
                    .height(56.dp),
                enabled = uiState.hasChanges && !uiState.isSaving && uiState.canSend,
                shape = RoundedCornerShape(18.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = BrandBlue,
                    contentColor = Color.White,
                    disabledContainerColor = BorderMuted,
                    disabledContentColor = TextSecondary,
                ),
                elevation = ButtonDefaults.buttonElevation(defaultElevation = 0.dp),
            ) {
                AppIcon(
                    painter = painterResource(R.drawable.ic_save),
                    contentDescription = null,
                    tint = Color.Unspecified,
                    size = 18.dp,
                )
                Text(
                    text = when {
                        uiState.isSaving -> "下发中..."
                        !uiState.canSend -> "设备离线，无法下发"
                        else -> "保存并下发"
                    },
                    modifier = Modifier.padding(start = 8.dp),
                    fontWeight = FontWeight.Bold,
                )
            }
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .background(Background),
            contentPadding = PaddingValues(
                start = 20.dp,
                top = 16.dp + paddingValues.calculateTopPadding(),
                end = 20.dp,
                bottom = 110.dp,
            ),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item {
                AppTopBar(
                    title = "阈值设置",
                    subtitle = "设置后由 OneNET 下发到主机",
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
                ThresholdStatusBanner(uiState = uiState)
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

            items(uiState.fields, key = { it.definition.id }) { field ->
                ThresholdFieldCard(
                    field = field,
                    onValueChange = { value -> onValueChange(field.definition.id, value) },
                )
            }
        }
    }
}

@Composable
private fun ThresholdStatusBanner(uiState: ThresholdUiState) {
    val (message, containerColor, textColor) = when (uiState.cloudDeviceStatus) {
        OneNetDeviceStatus.Online -> Triple(
            "OneNET 当前判定主机在线，最近云端连接时间 ${uiState.cloudLastSeenText}，最新遥测 ${uiState.latestTelemetryText}",
            SurfaceWhite,
            TextSecondary,
        )
        OneNetDeviceStatus.Offline -> Triple(
            "OneNET 当前判定主机离线，阈值下发会失败。最近在线时间 ${uiState.cloudLastSeenText}，最新遥测 ${uiState.latestTelemetryText}",
            OfflineSurface,
            TextPrimary,
        )
        OneNetDeviceStatus.Unknown -> Triple(
            "暂未拿到主机在线状态，可先刷新后再下发。最新遥测 ${uiState.latestTelemetryText}",
            SurfaceWhite,
            TextSecondary,
        )
    }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        InfoBanner(
            text = message,
            textColor = textColor,
            containerColor = containerColor,
        )
        if (uiState.isAutoSyncing) {
            InfoBanner(
                text = "阈值已下发，正在自动同步云端回读，当前进度条会保持最新设置。",
                textColor = TextSecondary,
                containerColor = SurfaceWhite,
            )
        }
        if (!uiState.isTelemetryFresh) {
            InfoBanner(
                text = "最新遥测已超时，当前页面显示的是历史缓存值。即使还能看到旧数据，也不代表设备当前可接收下发。",
                textColor = Danger,
                containerColor = DangerSurface,
            )
        }
    }
}

@Composable
private fun ThresholdFieldCard(
    field: ThresholdFieldUiState,
    onValueChange: (Int) -> Unit,
) {
    CardSurface(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                androidx.compose.foundation.layout.Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(CircleShape)
                        .background(field.definition.surfaceColor),
                    contentAlignment = Alignment.Center,
                ) {
                    AppIcon(
                        painter = painterResource(field.definition.iconRes),
                        contentDescription = null,
                        tint = Color.Unspecified,
                        size = 20.dp,
                    )
                }
                Column(modifier = Modifier.padding(start = 12.dp)) {
                    Text(
                        text = field.definition.title,
                        color = TextPrimary,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = "${field.definition.min}${field.definition.unit} - ${field.definition.max}${field.definition.unit}",
                        color = TextSecondary,
                        fontSize = 12.sp,
                    )
                }
            }
            Text(
                text = "${field.value}${field.definition.unit}",
                color = BrandBlue,
                fontSize = 20.sp,
                fontWeight = FontWeight.Bold,
            )
        }

        Slider(
            value = field.value.toFloat(),
            onValueChange = { onValueChange(it.toInt()) },
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 16.dp),
            valueRange = field.definition.min.toFloat()..field.definition.max.toFloat(),
            steps = ((field.definition.max - field.definition.min) / field.definition.step).coerceAtLeast(1) - 1,
            colors = SliderDefaults.colors(
                activeTrackColor = BrandBlue,
                thumbColor = BrandBlue,
                inactiveTrackColor = BorderMuted,
            ),
        )

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                text = "${field.definition.min}${field.definition.unit}",
                color = TextSecondary,
                fontSize = 12.sp,
            )
            OutlinedTextField(
                value = field.value.toString(),
                onValueChange = { input ->
                    input.toIntOrNull()?.let(onValueChange)
                },
                modifier = Modifier
                    .padding(horizontal = 12.dp)
                    .height(60.dp),
                singleLine = true,
                shape = RoundedCornerShape(16.dp),
                textStyle = TextStyle(
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold,
                ),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            )
            Text(
                text = "${field.definition.max}${field.definition.unit}",
                color = TextSecondary,
                fontSize = 12.sp,
            )
        }
    }
}
