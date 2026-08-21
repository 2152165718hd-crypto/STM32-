package com.stm32.envmonitor.data

import com.stm32.envmonitor.model.HistoryPoint
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.OneNetDeviceStatus
import com.stm32.envmonitor.model.OneNetDeviceStatusSnapshot
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

class OneNetRepository(
    val config: OneNetConfig,
    private val api: OneNetApi,
) {
    suspend fun fetchLatestSnapshot(): LatestDeviceSnapshot {
        return OneNetPayloadParser.normalizeLatestProperties(api.queryLatestProperties())
    }

    suspend fun fetchHistory(identifier: String): List<HistoryPoint> {
        val endTime = System.currentTimeMillis()
        val startTime = endTime - (config.historyHours * 60L * 60L * 1_000L)
        return OneNetPayloadParser.normalizeHistory(
            body = api.queryPropertyHistory(
                identifier = identifier,
                startTime = startTime,
                endTime = endTime,
                limit = config.historyLimit,
            ),
            identifier = identifier,
        )
    }

    suspend fun setDeviceProperties(params: Map<String, Int>) {
        api.setDeviceProperties(params)
    }

    suspend fun fetchDeviceStatus(): OneNetDeviceStatusSnapshot {
        val data = api.queryDeviceDetail()["data"]?.jsonObject
        val statusValue = data?.get("status")?.jsonPrimitive?.intOrNull
        val lastSeenText = data?.get("last_time")?.jsonPrimitive?.contentOrNull

        return OneNetDeviceStatusSnapshot(
            status = when (statusValue) {
                null -> OneNetDeviceStatus.Unknown
                0 -> OneNetDeviceStatus.Offline
                else -> OneNetDeviceStatus.Online
            },
            lastSeenMillis = OneNetPayloadParser.parseTimeValue(lastSeenText),
        )
    }
}
