package com.stm32.envmonitor.util

import java.text.DecimalFormat
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlin.math.abs
import kotlin.math.roundToInt

private val secondFormatter: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss")
private val decimalFormatter = DecimalFormat("0.#")

fun formatMetricValue(value: Double): String {
    if (!value.isFinite()) {
        return "--"
    }
    return if (abs(value - value.roundToInt()) < 0.0001) {
        value.roundToInt().toString()
    } else {
        decimalFormatter.format(value)
    }
}

fun formatClockTime(epochMillis: Long?): String {
    if (epochMillis == null) {
        return "--"
    }
    val localTime = LocalDateTime.ofInstant(Instant.ofEpochMilli(epochMillis), ZoneId.systemDefault())
    return localTime.format(secondFormatter)
}

fun clampInt(value: Int, min: Int, max: Int): Int {
    return value.coerceIn(min, max)
}

fun Throwable.toUiMessage(): String {
    return message?.takeIf { it.isNotBlank() } ?: "请求失败"
}
