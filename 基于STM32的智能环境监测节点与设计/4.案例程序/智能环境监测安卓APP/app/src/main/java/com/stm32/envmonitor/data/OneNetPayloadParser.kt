package com.stm32.envmonitor.data

import com.stm32.envmonitor.model.HistoryPoint
import com.stm32.envmonitor.model.LatestDeviceSnapshot
import com.stm32.envmonitor.model.PropertySnapshot
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

object OneNetPayloadParser {
    fun normalizeLatestProperties(body: JsonObject): LatestDeviceSnapshot {
        val properties = linkedMapOf<String, PropertySnapshot>()
        val source = body["data"] ?: body["result"] ?: JsonNull

        when (source) {
            is JsonArray -> source.forEach { addProperty(properties, it) }
            is JsonObject -> {
                val listNode = source["list"] ?: source["properties"] ?: source["property"] ?: source["params"]
                when (listNode) {
                    is JsonArray -> listNode.forEach { addProperty(properties, it) }
                    is JsonObject -> listNode.forEach { (key, value) ->
                        addProperty(properties, key, value)
                    }
                    else -> source.forEach { (key, value) ->
                        if (key !in setOf("code", "msg", "request_id")) {
                            addProperty(properties, key, value)
                        }
                    }
                }
            }
            else -> Unit
        }

        return LatestDeviceSnapshot(properties)
    }

    fun normalizeHistory(body: JsonObject, identifier: String): List<HistoryPoint> {
        val rawPoints = mutableListOf<HistoryPoint>()
        val source = body["data"] ?: body["result"] ?: JsonNull

        fun addPoint(node: JsonElement) {
            val value = readValue(node)?.let(::toDoubleOrNull) ?: return
            val time = readTime(node) ?: return
            rawPoints += HistoryPoint(value = value, timeMillis = time)
        }

        when (source) {
            is JsonArray -> source.forEach(::addPoint)
            is JsonObject -> {
                val listNode = source["list"] ?: source["records"] ?: source["items"] ?: source["data"] ?: source[identifier]
                when (listNode) {
                    is JsonArray -> listNode.forEach(::addPoint)
                    is JsonObject -> listNode.forEach { (timeKey, valueNode) ->
                        val value = readValue(valueNode)?.let(::toDoubleOrNull) ?: return@forEach
                        val time = parseTimeValue(timeKey) ?: return@forEach
                        rawPoints += HistoryPoint(value = value, timeMillis = time)
                    }
                    else -> Unit
                }
            }
            else -> Unit
        }

        return rawPoints.sortedBy { it.timeMillis }
    }

    fun readValue(node: JsonElement?): String? {
        return when (node) {
            null, JsonNull -> null
            is JsonPrimitive -> primitiveToString(node)
            is JsonObject -> {
                val valueNode = node["value"]
                when (valueNode) {
                    is JsonObject -> readValue(valueNode["value"])
                    null -> readValue(node["property_value"])
                    else -> readValue(valueNode)
                }
            }
            else -> null
        }
    }

    fun readTime(node: JsonElement?): Long? {
        if (node !is JsonObject) {
            return null
        }

        val directCandidates = listOf(
            node["time"],
            node["update_time"],
            node["last_time"],
            node["create_time"],
            node["event_time"],
            node["timestamp"],
            node["ts"],
        )

        directCandidates.firstNotNullOfOrNull(::parseTimeValue)?.let { return it }

        val nestedValue = node["value"] as? JsonObject ?: return null
        val nestedCandidates = listOf(
            nestedValue["time"],
            nestedValue["update_time"],
            nestedValue["last_time"],
            nestedValue["create_time"],
            nestedValue["event_time"],
            nestedValue["timestamp"],
            nestedValue["ts"],
        )

        return nestedCandidates.firstNotNullOfOrNull(::parseTimeValue)
    }

    fun parseTimeValue(value: JsonElement?): Long? {
        val raw = when (value) {
            null, JsonNull -> return null
            is JsonPrimitive -> primitiveToString(value)
            else -> return null
        } ?: return null

        return parseTimeValue(raw)
    }

    fun parseTimeValue(value: String?): Long? {
        val trimmed = value?.trim().orEmpty()
        if (trimmed.isEmpty()) {
            return null
        }

        trimmed.toLongOrNull()?.let { numeric ->
            return if (numeric in 1..999_999_999_999L) numeric * 1_000L else numeric
        }

        return try {
            Instant.parse(trimmed).toEpochMilli()
        } catch (_: DateTimeParseException) {
            val normalized = trimmed.replace('/', '-')
            val formatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
            runCatching {
                LocalDateTime.parse(normalized, formatter)
                    .atZone(ZoneId.systemDefault())
                    .toInstant()
                    .toEpochMilli()
            }.getOrNull()
        }
    }

    fun toIntOrNull(value: String?): Int? {
        return value?.trim()?.toDoubleOrNull()?.toInt()
    }

    fun toDoubleOrNull(value: String?): Double? {
        return value?.trim()?.toDoubleOrNull()
    }

    fun toBoolean(value: String?, fallback: Boolean = false): Boolean {
        val normalized = value?.trim()?.lowercase().orEmpty()
        return when (normalized) {
            "true", "1", "yes", "on" -> true
            "false", "0", "no", "off" -> false
            else -> fallback
        }
    }

    private fun addProperty(target: MutableMap<String, PropertySnapshot>, node: JsonElement) {
        if (node !is JsonObject) {
            return
        }

        val identifier = node["identifier"]?.jsonPrimitive?.contentOrNull
            ?: node["property_id"]?.jsonPrimitive?.contentOrNull
            ?: node["id"]?.jsonPrimitive?.contentOrNull
            ?: node["name"]?.jsonPrimitive?.contentOrNull
            ?: node["key"]?.jsonPrimitive?.contentOrNull

        addProperty(target, identifier, node)
    }

    private fun addProperty(target: MutableMap<String, PropertySnapshot>, identifier: String?, node: JsonElement) {
        if (identifier.isNullOrBlank()) {
            return
        }

        val value = readValue(node)
        val time = readTime(node)
        if (value == null && time == null) {
            return
        }

        target[identifier] = PropertySnapshot(
            identifier = identifier,
            rawValue = value,
            timeMillis = time,
        )
    }

    private fun primitiveToString(primitive: JsonPrimitive): String? {
        primitive.contentOrNull?.let { return it }
        primitive.intOrNull?.let { return it.toString() }
        primitive.doubleOrNull?.let { return it.toString() }
        primitive.booleanOrNull?.let { return it.toString() }
        return null
    }
}
