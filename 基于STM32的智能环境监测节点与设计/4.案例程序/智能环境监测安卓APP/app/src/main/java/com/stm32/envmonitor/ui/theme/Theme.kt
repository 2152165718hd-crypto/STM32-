package com.stm32.envmonitor.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val LightScheme = lightColorScheme(
    primary = BrandBlue,
    secondary = TextSecondary,
    background = Background,
    surface = SurfaceWhite,
    onPrimary = SurfaceWhite,
    onBackground = TextPrimary,
    onSurface = TextPrimary,
    error = Danger,
)

private val DarkScheme = darkColorScheme(
    primary = BrandBlue,
)

@Composable
fun EnvMonitorTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) DarkScheme else LightScheme,
        typography = AppTypography,
        content = content,
    )
}
