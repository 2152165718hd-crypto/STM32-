package com.stm32.envmonitor.util

import com.stm32.envmonitor.model.MetricCatalog

object AlarmMaskParser {
    fun parse(mask: Int, fallbackActive: Boolean): List<String> {
        val labels = MetricCatalog.alarmLabelMap
            .filterKeys { bit -> mask and bit != 0 }
            .values
            .toList()

        if (labels.isNotEmpty()) {
            return labels
        }

        return if (fallbackActive) listOf("当前存在报警") else emptyList()
    }
}
