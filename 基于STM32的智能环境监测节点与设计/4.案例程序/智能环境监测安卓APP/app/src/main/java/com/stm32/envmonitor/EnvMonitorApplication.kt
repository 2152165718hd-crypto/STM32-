package com.stm32.envmonitor

import android.app.Application
import com.stm32.envmonitor.ui.AppContainer

class EnvMonitorApplication : Application() {
    val appContainer: AppContainer by lazy { AppContainer() }
}
