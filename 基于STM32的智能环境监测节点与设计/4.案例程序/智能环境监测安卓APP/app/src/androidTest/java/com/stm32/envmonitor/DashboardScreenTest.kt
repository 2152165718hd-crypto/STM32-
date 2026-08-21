package com.stm32.envmonitor

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import com.stm32.envmonitor.ui.dashboard.DashboardScreen
import com.stm32.envmonitor.ui.dashboard.DashboardStatusMode
import com.stm32.envmonitor.ui.dashboard.DashboardUiState
import com.stm32.envmonitor.ui.theme.EnvMonitorTheme
import org.junit.Rule
import org.junit.Test

class DashboardScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun dashboard_displaysPrimaryActionsAndAlarmSection() {
        composeRule.setContent {
            EnvMonitorTheme {
                DashboardScreen(
                    uiState = DashboardUiState(
                        hasData = true,
                        statusMode = DashboardStatusMode.Normal,
                        statusTitle = "运行正常",
                        statusText = "检测端在线，OneNET 数据刷新正常",
                    ),
                    onRefresh = {},
                    onNavigateThreshold = {},
                    onNavigateHistory = {},
                )
            }
        }

        composeRule.onNodeWithText("阈值设置").assertIsDisplayed()
        composeRule.onNodeWithText("历史曲线").assertIsDisplayed()
        composeRule.onNodeWithText("当前报警").assertIsDisplayed()
    }
}
