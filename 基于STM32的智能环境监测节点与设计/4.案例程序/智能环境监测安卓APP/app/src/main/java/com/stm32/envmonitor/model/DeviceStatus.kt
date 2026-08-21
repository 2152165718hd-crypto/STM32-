package com.stm32.envmonitor.model

enum class OneNetDeviceStatus {
    Online,
    Offline,
    Unknown,
}

data class OneNetDeviceStatusSnapshot(
    val status: OneNetDeviceStatus,
    val lastSeenMillis: Long?,
) {
    val isOnline: Boolean
        get() = status == OneNetDeviceStatus.Online
}
