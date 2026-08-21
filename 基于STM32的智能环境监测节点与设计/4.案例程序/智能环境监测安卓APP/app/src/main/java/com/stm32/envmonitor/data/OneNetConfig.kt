package com.stm32.envmonitor.data

import com.stm32.envmonitor.BuildConfig

data class OneNetConfig(
    val baseUrl: String,
    val productId: String,
    val deviceName: String,
    val authorization: String,
    val refreshIntervalMs: Long,
    val staleDataMs: Long,
    val historyHours: Int,
    val historyLimit: Int,
    val requestTimeoutMs: Long = 6_000L,
    val commandTimeoutMs: Long = 15_000L,
    val requestRetryCount: Int = 1,
) {
    val isConfigured: Boolean
        get() = baseUrl.isNotBlank() &&
            productId.isNotBlank() &&
            deviceName.isNotBlank() &&
            authorization.isNotBlank() &&
            !productId.startsWith("YOUR_") &&
            !deviceName.startsWith("YOUR_") &&
            !authorization.startsWith("YOUR_")

    companion object {
        fun fromBuildConfig(): OneNetConfig {
            return OneNetConfig(
                baseUrl = BuildConfig.ONENET_BASE_URL,
                productId = BuildConfig.ONENET_PRODUCT_ID,
                deviceName = BuildConfig.ONENET_DEVICE_NAME,
                authorization = BuildConfig.ONENET_AUTHORIZATION,
                refreshIntervalMs = BuildConfig.ONENET_REFRESH_INTERVAL_MS.coerceAtLeast(1_000L),
                staleDataMs = BuildConfig.ONENET_STALE_DATA_MS.coerceAtLeast(4_000L),
                historyHours = BuildConfig.ONENET_HISTORY_HOURS.coerceAtLeast(1),
                historyLimit = BuildConfig.ONENET_HISTORY_LIMIT.coerceAtLeast(1),
            )
        }
    }
}
