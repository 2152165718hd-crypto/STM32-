package com.stm32.envmonitor.data

import java.io.IOException
import java.net.SocketTimeoutException
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody

class OneNetApi(
    private val config: OneNetConfig,
    private val json: Json = Json { ignoreUnknownKeys = true },
    private val client: OkHttpClient = defaultClient(config),
) {
    suspend fun queryLatestProperties(): JsonObject {
        return requestJson(
            path = "/thingmodel/query-device-property",
            queryParams = mapOf(
                "product_id" to config.productId,
                "device_name" to config.deviceName,
            ),
            retryCount = config.requestRetryCount,
        )
    }

    suspend fun queryPropertyHistory(
        identifier: String,
        startTime: Long,
        endTime: Long,
        limit: Int,
    ): JsonObject {
        return requestJson(
            path = "/thingmodel/query-device-property-history",
            queryParams = mapOf(
                "product_id" to config.productId,
                "device_name" to config.deviceName,
                "identifier" to identifier,
                "start_time" to startTime.toString(),
                "end_time" to endTime.toString(),
                "limit" to limit.toString(),
            ),
            retryCount = config.requestRetryCount,
        )
    }

    suspend fun setDeviceProperties(params: Map<String, Int>): JsonObject {
        val payload = buildString {
            append("{\"product_id\":\"")
            append(config.productId)
            append("\",\"device_name\":\"")
            append(config.deviceName)
            append("\",\"params\":{")
            append(
                params.entries.joinToString(",") { entry ->
                    "\"${entry.key}\":${entry.value}"
                },
            )
            append("}}")
        }

        return requestJson(
            path = "/thingmodel/set-device-property",
            method = "POST",
            body = payload,
            timeoutMs = config.commandTimeoutMs,
            retryCount = 0,
        )
    }

    suspend fun queryDeviceDetail(): JsonObject {
        return requestJson(
            path = "/device/detail",
            queryParams = mapOf(
                "product_id" to config.productId,
                "device_name" to config.deviceName,
            ),
            retryCount = 0,
        )
    }

    private suspend fun requestJson(
        path: String,
        method: String = "GET",
        queryParams: Map<String, String> = emptyMap(),
        body: String? = null,
        timeoutMs: Long = config.requestTimeoutMs,
        retryCount: Int,
    ): JsonObject = withContext(Dispatchers.IO) {
        if (!config.isConfigured) {
            throw OneNetException("请先在 onenet.debug.properties 中配置 OneNET 设备参数")
        }

        val url = "${config.baseUrl}$path".toHttpUrl().newBuilder().apply {
            queryParams.forEach { (key, value) ->
                addQueryParameter(key, value)
            }
        }.build()

        var lastError: Throwable? = null
        repeat(retryCount + 1) { attempt ->
            try {
                val requestBuilder = Request.Builder()
                    .url(url)
                    .header("authorization", config.authorization)
                    .header("Content-Type", "application/json")

                if (method == "POST") {
                    val requestBody = (body ?: "{}").toRequestBody("application/json".toMediaType())
                    requestBuilder.post(requestBody)
                } else {
                    requestBuilder.get()
                }

                client.newBuilder()
                    .connectTimeout(timeoutMs, TimeUnit.MILLISECONDS)
                    .readTimeout(timeoutMs, TimeUnit.MILLISECONDS)
                    .writeTimeout(timeoutMs, TimeUnit.MILLISECONDS)
                    .build()
                    .newCall(requestBuilder.build())
                    .execute()
                    .use { response ->
                        val bodyText = response.body?.string().orEmpty()
                        if (!response.isSuccessful) {
                            throw IOException("OneNET 请求失败: HTTP ${response.code}")
                        }

                        val jsonObject = json.parseToJsonElement(bodyText).jsonObject
                        val code = jsonObject["code"]?.jsonPrimitive?.intOrNull
                        if (code != null && code != 0 && code != 200) {
                            val message = jsonObject["msg"]?.jsonPrimitive?.contentOrNull ?: "OneNET 请求失败"
                            throw OneNetApiException(code, message)
                        }
                        return@withContext jsonObject
                    }
            } catch (error: SocketTimeoutException) {
                lastError = if (method == "POST") {
                    OneNetCommandTimeoutException("阈值下发等待超时，请确认主机在线并已正确响应 OneNET")
                } else {
                    OneNetException("OneNET 请求超时，请稍后重试")
                }
                throw lastError as Throwable
            } catch (error: Throwable) {
                lastError = error
                val shouldRetry = method == "GET" && attempt < retryCount && error is IOException
                if (!shouldRetry) {
                    throw error
                }
            }
        }

        throw lastError ?: OneNetException("OneNET 请求失败")
    }

    companion object {
        fun defaultClient(config: OneNetConfig): OkHttpClient {
            return OkHttpClient.Builder()
                .connectTimeout(config.requestTimeoutMs, TimeUnit.MILLISECONDS)
                .readTimeout(config.requestTimeoutMs, TimeUnit.MILLISECONDS)
                .writeTimeout(config.requestTimeoutMs, TimeUnit.MILLISECONDS)
                .build()
        }
    }
}

class OneNetException(message: String) : IOException(message)

class OneNetApiException(
    val code: Int,
    message: String,
) : IOException(message)

class OneNetCommandTimeoutException(message: String) : IOException(message)
