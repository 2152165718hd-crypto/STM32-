package com.stm32.envmonitor

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import com.stm32.envmonitor.ui.history.HistoryScreen
import com.stm32.envmonitor.ui.history.HistoryUiState
import com.stm32.envmonitor.ui.theme.EnvMonitorTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class HistoryScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun historyScreen_invokesMetricSelectionCallback() {
        var selectedMetricId: String? = null

        composeRule.setContent {
            EnvMonitorTheme {
                HistoryScreen(
                    uiState = HistoryUiState(),
                    onBack = {},
                    onRefresh = {},
                    onMetricSelected = { selectedMetricId = it },
                )
            }
        }

        composeRule.onNodeWithText("湿度").performClick()

        assertEquals("humidity", selectedMetricId)
    }
}
