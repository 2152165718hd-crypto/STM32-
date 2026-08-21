package com.stm32.envmonitor

import com.google.common.truth.Truth.assertThat
import com.stm32.envmonitor.data.OneNetPayloadParser
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import org.junit.Test

class OneNetPayloadParserTest {
    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun normalizeLatestProperties_parsesDeviceSnapshot() {
        val payload = json.parseToJsonElement(
            """
            {
              "code": 0,
              "data": [
                {
                  "identifier": "temperature",
                  "time": 1777029280120,
                  "value": "23"
                },
                {
                  "identifier": "slave_online",
                  "time": 1777029280120,
                  "value": "true"
                }
              ]
            }
            """.trimIndent(),
        ).jsonObject

        val snapshot = OneNetPayloadParser.normalizeLatestProperties(payload)

        assertThat(snapshot.valueOf("temperature")).isEqualTo("23")
        assertThat(snapshot.timeOf("temperature")).isEqualTo(1_777_029_280_120L)
        assertThat(OneNetPayloadParser.toBoolean(snapshot.valueOf("slave_online"))).isTrue()
    }

    @Test
    fun normalizeHistory_sortsAscending() {
        val payload = json.parseToJsonElement(
            """
            {
              "code": 0,
              "data": {
                "list": [
                  { "time": 1777029280120, "value": "25" },
                  { "time": 1777029270120, "value": "23" },
                  { "time": 1777029275120, "value": "24" }
                ]
              }
            }
            """.trimIndent(),
        ).jsonObject

        val points = OneNetPayloadParser.normalizeHistory(payload, "temperature")

        assertThat(points.map { it.value }).containsExactly(23.0, 24.0, 25.0).inOrder()
    }

    @Test
    fun parseTimeValue_promotesSecondsToMillis() {
        assertThat(OneNetPayloadParser.parseTimeValue("1777029280")).isEqualTo(1_777_029_280_000L)
        assertThat(OneNetPayloadParser.parseTimeValue("1777029280120")).isEqualTo(1_777_029_280_120L)
    }
}
