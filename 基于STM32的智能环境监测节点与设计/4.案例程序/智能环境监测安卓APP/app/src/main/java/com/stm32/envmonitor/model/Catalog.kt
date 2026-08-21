package com.stm32.envmonitor.model

import androidx.annotation.DrawableRes
import androidx.compose.ui.graphics.Color
import com.stm32.envmonitor.R

data class MetricDef(
    val id: String,
    val title: String,
    val unit: String,
    @DrawableRes val iconRes: Int,
    val accentColor: Color,
    val surfaceColor: Color,
    val min: Double,
    val max: Double,
    val validBit: Int?,
)

data class ThresholdDef(
    val id: String,
    val title: String,
    val unit: String,
    @DrawableRes val iconRes: Int,
    val accentColor: Color,
    val surfaceColor: Color,
    val min: Int,
    val max: Int,
    val step: Int,
    val defaultValue: Int,
)

object MetricCatalog {
    const val VALID_DHT11 = 0x01
    const val VALID_PM25 = 0x02
    const val VALID_MQ135 = 0x04
    const val VALID_BH1750 = 0x08

    val metrics = listOf(
        MetricDef(
            id = "temperature",
            title = "温度",
            unit = "℃",
            iconRes = R.drawable.ic_temperature,
            accentColor = Color(0xFFE5483F),
            surfaceColor = Color(0xFFFFE9E5),
            min = 0.0,
            max = 50.0,
            validBit = VALID_DHT11,
        ),
        MetricDef(
            id = "humidity",
            title = "湿度",
            unit = "%RH",
            iconRes = R.drawable.ic_humidity,
            accentColor = Color(0xFF1677FF),
            surfaceColor = Color(0xFFE7F4FF),
            min = 0.0,
            max = 100.0,
            validBit = VALID_DHT11,
        ),
        MetricDef(
            id = "pm25",
            title = "PM2.5",
            unit = "ug/m3",
            iconRes = R.drawable.ic_pm25,
            accentColor = Color(0xFF5C6675),
            surfaceColor = Color(0xFFEEF1F5),
            min = 0.0,
            max = 999.0,
            validBit = VALID_PM25,
        ),
        MetricDef(
            id = "gas_percent",
            title = "气体",
            unit = "%",
            iconRes = R.drawable.ic_gas,
            accentColor = Color(0xFF17A36B),
            surfaceColor = Color(0xFFEAF8F0),
            min = 0.0,
            max = 100.0,
            validBit = VALID_MQ135,
        ),
        MetricDef(
            id = "light_lux",
            title = "光照",
            unit = "lx",
            iconRes = R.drawable.ic_light,
            accentColor = Color(0xFFE6A700),
            surfaceColor = Color(0xFFFFF7D8),
            min = 0.0,
            max = 65_535.0,
            validBit = VALID_BH1750,
        ),
    )

    val thresholds = listOf(
        ThresholdDef(
            id = "temperature_threshold",
            title = "温度上限",
            unit = "℃",
            iconRes = R.drawable.ic_temperature,
            accentColor = Color(0xFFE5483F),
            surfaceColor = Color(0xFFFFE9E5),
            min = 0,
            max = 50,
            step = 1,
            defaultValue = 30,
        ),
        ThresholdDef(
            id = "humidity_threshold",
            title = "湿度上限",
            unit = "%RH",
            iconRes = R.drawable.ic_humidity,
            accentColor = Color(0xFF1677FF),
            surfaceColor = Color(0xFFE7F4FF),
            min = 0,
            max = 100,
            step = 1,
            defaultValue = 80,
        ),
        ThresholdDef(
            id = "pm25_threshold",
            title = "PM2.5 上限",
            unit = "ug/m3",
            iconRes = R.drawable.ic_pm25,
            accentColor = Color(0xFF5C6675),
            surfaceColor = Color(0xFFEEF1F5),
            min = 0,
            max = 999,
            step = 5,
            defaultValue = 75,
        ),
        ThresholdDef(
            id = "gas_threshold",
            title = "气体上限",
            unit = "%",
            iconRes = R.drawable.ic_gas,
            accentColor = Color(0xFF17A36B),
            surfaceColor = Color(0xFFEAF8F0),
            min = 0,
            max = 100,
            step = 1,
            defaultValue = 60,
        ),
        ThresholdDef(
            id = "light_threshold",
            title = "光照上限",
            unit = "lx",
            iconRes = R.drawable.ic_light,
            accentColor = Color(0xFFE6A700),
            surfaceColor = Color(0xFFFFF7D8),
            min = 0,
            max = 9_999,
            step = 50,
            defaultValue = 1_000,
        ),
    )

    val telemetryIds = metrics.map(MetricDef::id) + listOf(
        "valid_bits",
        "slave_online",
        "alarm_active",
        "alarm_mask",
    )

    val alarmLabelMap = linkedMapOf(
        0x01 to "PM2.5 超限",
        0x02 to "气体超限",
        0x04 to "光照超限",
        0x08 to "温度超限",
        0x10 to "湿度超限",
    )

    fun metricById(id: String): MetricDef = metrics.first { it.id == id }
}
