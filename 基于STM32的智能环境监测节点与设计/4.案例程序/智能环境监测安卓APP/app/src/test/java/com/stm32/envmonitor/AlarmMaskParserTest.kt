package com.stm32.envmonitor

import com.google.common.truth.Truth.assertThat
import com.stm32.envmonitor.util.AlarmMaskParser
import org.junit.Test

class AlarmMaskParserTest {
    @Test
    fun parse_returnsMappedLabels() {
        val labels = AlarmMaskParser.parse(mask = 0x05, fallbackActive = false)

        assertThat(labels).containsExactly("PM2.5 超限", "光照超限").inOrder()
    }

    @Test
    fun parse_returnsFallbackWhenMaskEmptyAndAlarmActive() {
        val labels = AlarmMaskParser.parse(mask = 0, fallbackActive = true)

        assertThat(labels).containsExactly("当前存在报警")
    }
}
