package com.stm32.envmonitor

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.stm32.envmonitor.ui.EnvMonitorApp
import com.stm32.envmonitor.ui.theme.EnvMonitorTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            EnvMonitorTheme {
                EnvMonitorApp()
            }
        }
    }
}
