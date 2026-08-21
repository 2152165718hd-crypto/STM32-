package com.stm32.envmonitor.ui

import com.stm32.envmonitor.data.OneNetApi
import com.stm32.envmonitor.data.OneNetConfig
import com.stm32.envmonitor.data.OneNetRepository

class AppContainer {
    val config: OneNetConfig = OneNetConfig.fromBuildConfig()
    private val api: OneNetApi = OneNetApi(config)
    val repository: OneNetRepository = OneNetRepository(config = config, api = api)
}
