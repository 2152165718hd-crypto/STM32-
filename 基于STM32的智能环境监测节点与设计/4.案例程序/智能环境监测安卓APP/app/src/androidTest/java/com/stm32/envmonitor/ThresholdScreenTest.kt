package com.stm32.envmonitor

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import com.stm32.envmonitor.model.OneNetDeviceStatus
import com.stm32.envmonitor.ui.theme.EnvMonitorTheme
import com.stm32.envmonitor.ui.threshold.ThresholdScreen
import com.stm32.envmonitor.ui.threshold.ThresholdUiState
import org.junit.Rule
import org.junit.Test

class ThresholdScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun thresholdScreen_enablesSaveWhenDirty() {
        composeRule.setContent {
            EnvMonitorTheme {
                ThresholdScreen(
                    uiState = ThresholdUiState(hasChanges = true),
                    onBack = {},
                    onRefresh = {},
                    onValueChange = { _, _ -> },
                    onSave = {},
                )
            }
        }

        composeRule.onNodeWithText("保存并下发").assertIsEnabled()
    }

    @Test
    fun thresholdScreen_disablesSaveWhenCloudOffline() {
        composeRule.setContent {
            EnvMonitorTheme {
                ThresholdScreen(
                    uiState = ThresholdUiState(
                        hasChanges = true,
                        cloudDeviceStatus = OneNetDeviceStatus.Offline,
                    ),
                    onBack = {},
                    onRefresh = {},
                    onValueChange = { _, _ -> },
                    onSave = {},
                )
            }
        }

        composeRule.onNodeWithText("设备离线，无法下发").assertIsNotEnabled()
    }
}
