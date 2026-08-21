package com.stm32.envmonitor

import com.google.common.truth.Truth.assertThat
import com.stm32.envmonitor.data.OneNetApi
import com.stm32.envmonitor.data.OneNetConfig
import kotlinx.coroutines.test.runTest
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Before
import org.junit.Test

class OneNetApiTest {
    private lateinit var server: MockWebServer
    private lateinit var api: OneNetApi

    @Before
    fun setUp() {
        server = MockWebServer()
        server.start()
        api = OneNetApi(
            config = OneNetConfig(
                baseUrl = server.url("/").toString().removeSuffix("/"),
                productId = "product-demo",
                deviceName = "device-demo",
                authorization = "demo-token",
                refreshIntervalMs = 3_000L,
                staleDataMs = 12_000L,
                historyHours = 6,
                historyLimit = 60,
                requestTimeoutMs = 6_000L,
                requestRetryCount = 1,
            ),
        )
    }

    @After
    fun tearDown() {
        server.shutdown()
    }

    @Test
    fun queryLatestProperties_sendsExpectedRequest() = runTest {
        server.enqueue(MockResponse().setBody("""{"code":0,"data":[]}"""))

        api.queryLatestProperties()

        val request = server.takeRequest()
        assertThat(request.method).isEqualTo("GET")
        assertThat(request.getHeader("authorization")).isEqualTo("demo-token")
        assertThat(request.path).contains("/thingmodel/query-device-property")
        assertThat(request.path).contains("product_id=product-demo")
        assertThat(request.path).contains("device_name=device-demo")
    }

    @Test
    fun setDeviceProperties_postsChangedPayload() = runTest {
        server.enqueue(MockResponse().setBody("""{"code":0,"data":{}}"""))

        api.setDeviceProperties(
            mapOf(
                "temperature_threshold" to 35,
                "light_threshold" to 1200,
            ),
        )

        val request = server.takeRequest()
        val body = request.body.readUtf8()
        assertThat(request.method).isEqualTo("POST")
        assertThat(body).contains("\"temperature_threshold\":35")
        assertThat(body).contains("\"light_threshold\":1200")
    }

    @Test
    fun queryDeviceDetail_sendsExpectedRequest() = runTest {
        server.enqueue(MockResponse().setBody("""{"code":0,"data":{"status":1}}"""))

        api.queryDeviceDetail()

        val request = server.takeRequest()
        assertThat(request.method).isEqualTo("GET")
        assertThat(request.path).contains("/device/detail")
        assertThat(request.path).contains("product_id=product-demo")
        assertThat(request.path).contains("device_name=device-demo")
    }

    @Test
    fun queryLatestProperties_retriesAfterServerError() = runTest {
        server.enqueue(MockResponse().setResponseCode(500).setBody("""{"msg":"server error"}"""))
        server.enqueue(MockResponse().setBody("""{"code":0,"data":[]}"""))

        api.queryLatestProperties()

        assertThat(server.requestCount).isEqualTo(2)
    }
}
