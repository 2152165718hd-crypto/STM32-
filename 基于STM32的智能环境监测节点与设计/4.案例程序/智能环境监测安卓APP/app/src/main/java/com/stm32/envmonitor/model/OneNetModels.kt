package com.stm32.envmonitor.model

data class PropertySnapshot(
    val identifier: String,
    val rawValue: String?,
    val timeMillis: Long?,
)

data class LatestDeviceSnapshot(
    val properties: Map<String, PropertySnapshot>,
) {
    fun valueOf(identifier: String): String? = properties[identifier]?.rawValue

    fun timeOf(identifier: String): Long? = properties[identifier]?.timeMillis
}

data class HistoryPoint(
    val value: Double,
    val timeMillis: Long,
)
