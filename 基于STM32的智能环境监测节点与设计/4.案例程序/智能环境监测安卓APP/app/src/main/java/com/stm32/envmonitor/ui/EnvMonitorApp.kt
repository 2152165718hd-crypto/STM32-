package com.stm32.envmonitor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.stm32.envmonitor.EnvMonitorApplication
import com.stm32.envmonitor.ui.dashboard.DashboardRoute
import com.stm32.envmonitor.ui.history.HistoryRoute
import com.stm32.envmonitor.ui.theme.Background
import com.stm32.envmonitor.ui.threshold.ThresholdRoute

private object AppRoute {
    const val Dashboard = "dashboard"
    const val Threshold = "threshold"
    const val History = "history"
}

@Composable
fun EnvMonitorApp() {
    val context = LocalContext.current.applicationContext as EnvMonitorApplication
    val repository = context.appContainer.repository
    val navController = rememberNavController()

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Background)
            .windowInsetsPadding(WindowInsets.safeDrawing),
    ) {
        NavHost(
            navController = navController,
            startDestination = AppRoute.Dashboard,
        ) {
            composable(AppRoute.Dashboard) {
                DashboardRoute(
                    repository = repository,
                    onNavigateThreshold = { navController.navigate(AppRoute.Threshold) },
                    onNavigateHistory = { navController.navigate(AppRoute.History) },
                )
            }
            composable(AppRoute.Threshold) {
                ThresholdRoute(
                    repository = repository,
                    onBack = { navController.popBackStack() },
                )
            }
            composable(AppRoute.History) {
                HistoryRoute(
                    repository = repository,
                    onBack = { navController.popBackStack() },
                )
            }
        }
    }
}
