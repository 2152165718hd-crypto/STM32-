const config = require('../config/onenet')

const SENSOR_VALID_BITS = {
  DHT11: 0x01,
  PM25: 0x02,
  MQ135: 0x04,
  BH1750: 0x08
}

const PROPERTY_DEFS = [
  { id: 'temperature', title: '温度', unit: '°C', icon: '/image/icon-temperature.svg', min: 0, max: 50, validBit: SENSOR_VALID_BITS.DHT11 },
  { id: 'humidity', title: '湿度', unit: '%RH', icon: '/image/icon-humidity.svg', min: 0, max: 100, validBit: SENSOR_VALID_BITS.DHT11 },
  { id: 'pm25', title: 'PM2.5', unit: 'ug/m3', icon: '/image/icon-pm25.svg', min: 0, max: 999, validBit: SENSOR_VALID_BITS.PM25 },
  { id: 'gas_percent', title: '气体', unit: '%', icon: '/image/icon-gas.svg', min: 0, max: 100, validBit: SENSOR_VALID_BITS.MQ135 },
  { id: 'light_lux', title: '光照', unit: 'lx', icon: '/image/icon-light.svg', min: 0, max: 65535, validBit: SENSOR_VALID_BITS.BH1750 }
]

const THRESHOLD_DEFS = [
  { id: 'temperature_threshold', title: '温度上限', unit: '°C', icon: '/image/icon-temperature.svg', min: 0, max: 50, step: 1, defaultValue: 30 },
  { id: 'humidity_threshold', title: '湿度上限', unit: '%', icon: '/image/icon-humidity.svg', min: 0, max: 100, step: 1, defaultValue: 80 },
  { id: 'pm25_threshold', title: 'PM2.5上限', unit: 'ug/m3', icon: '/image/icon-pm25.svg', min: 0, max: 999, step: 5, defaultValue: 75 },
  { id: 'gas_threshold', title: '气体上限', unit: '%', icon: '/image/icon-gas.svg', min: 0, max: 100, step: 1, defaultValue: 60 },
  { id: 'light_threshold', title: '光照上限', unit: 'lx', icon: '/image/icon-light.svg', min: 0, max: 9999, step: 50, defaultValue: 1000 }
]

const TELEMETRY_PROPERTY_IDS = PROPERTY_DEFS
  .map((item) => item.id)
  .concat(['valid_bits', 'slave_online', 'alarm_active', 'alarm_mask'])

let latestPropertiesInFlight = null
let latestPropertiesStartedAt = 0
let cloudCommandCoolingUntil = 0

function isPlaceholder(value) {
  return !value || /^YOUR_/.test(value)
}

function hasOwn(target, key) {
  return Object.prototype.hasOwnProperty.call(target, key)
}

function isConfigured() {
  return !isPlaceholder(config.productId) &&
    !isPlaceholder(config.deviceName) &&
    !isPlaceholder(config.authorization)
}

function isEmptyValue(value) {
  return value === null || value === undefined || value === ''
}

function toNumber(value, fallback = 0) {
  if (isEmptyValue(value)) {
    return fallback
  }
  if (typeof value === 'boolean') {
    return value ? 1 : 0
  }
  const numberValue = Number(value)
  return Number.isFinite(numberValue) ? numberValue : fallback
}

function toBoolean(value, fallback = false) {
  if (isEmptyValue(value)) {
    return fallback
  }
  if (typeof value === 'boolean') {
    return value
  }
  if (typeof value === 'number') {
    return value !== 0
  }

  const text = String(value).trim().toLowerCase()
  if (text === 'true' || text === '1' || text === 'yes' || text === 'on') {
    return true
  }
  if (text === 'false' || text === '0' || text === 'no' || text === 'off') {
    return false
  }
  return fallback
}

function clampNumber(value, min, max, fallback = min) {
  const numberValue = toNumber(value, fallback)
  return Math.max(min, Math.min(max, numberValue))
}

function getRefreshIntervalMs() {
  return Math.max(1000, toNumber(config.refreshIntervalMs, 3000))
}

function getRequestTimeoutMs() {
  return Math.max(3000, toNumber(config.requestTimeoutMs, 8000))
}

function getRequestRetryCount() {
  return Math.max(0, toNumber(config.requestRetry, 0))
}

function getStaleDataMs() {
  return Math.max(getRefreshIntervalMs() * 2, toNumber(config.staleDataMs, 12000))
}

function getCommandCooldownMs() {
  return Math.max(0, toNumber(config.commandCooldownMs, 1500))
}

function markCloudCommandCooldown(durationMs = getCommandCooldownMs()) {
  cloudCommandCoolingUntil = Math.max(cloudCommandCoolingUntil, Date.now() + durationMs)
}

function isCloudCommandCoolingDown() {
  return Date.now() < cloudCommandCoolingUntil
}

function normalizeError(err) {
  if (!err) {
    return '请求失败'
  }
  if (typeof err === 'string') {
    return err
  }
  if (err.errMsg) {
    return err.errMsg
  }
  if (err.msg) {
    return err.msg
  }
  return '请求失败'
}

function request(path, options = {}) {
  if (!isConfigured()) {
    return Promise.reject(new Error('请先在 config/onenet.js 填写 OneNET 产品ID、设备名和 API 鉴权信息'))
  }

  const method = options.method || 'GET'
  const retryLimit = Math.max(0, toNumber(hasOwn(options, 'retry') ? options.retry : getRequestRetryCount(), 0))

  function doRequest(attempt) {
    return new Promise((resolve, reject) => {
      wx.request({
      url: `${config.baseUrl}${path}`,
      method,
      data: options.data || {},
      timeout: hasOwn(options, 'timeout') ? options.timeout : getRequestTimeoutMs(),
      header: {
        authorization: config.authorization,
        'Content-Type': 'application/json'
      },
      success(res) {
        const body = res.data || {}
        const code = body.code !== undefined ? body.code : body.errno
        const ok = res.statusCode >= 200 && res.statusCode < 300 &&
          (code === undefined || Number(code) === 0 || Number(code) === 200)

        if (ok) {
          resolve(body)
          return
        } else {
          const err = new Error(body.msg || body.message || `OneNET request failed: ${res.statusCode}`)
          err.transient = res.statusCode === 0 || res.statusCode >= 500
          reject(err)
        }
      },
      fail(err) {
        const normalized = new Error(normalizeError(err))
        normalized.transient = true
        reject(normalized)
      }
    })
    }).catch((err) => {
      if (method === 'GET' && attempt < retryLimit && err && err.transient) {
        return new Promise((resolve) => {
          setTimeout(resolve, 300 * (attempt + 1))
        }).then(() => doRequest(attempt + 1))
      }
      throw err
    })
  }

  return doRequest(0)
}

function queryLatestProperties(options = {}) {
  const now = Date.now()
  if (latestPropertiesInFlight && (now - latestPropertiesStartedAt) < (getRequestTimeoutMs() + 1000)) {
    return latestPropertiesInFlight
  }

  latestPropertiesStartedAt = now
  latestPropertiesInFlight = request('/thingmodel/query-device-property', {
    retry: hasOwn(options, 'retry') ? options.retry : getRequestRetryCount(),
    data: {
      product_id: config.productId,
      device_name: config.deviceName
    }
  })
    .then((res) => {
      latestPropertiesInFlight = null
      return res
    })
    .catch((err) => {
      latestPropertiesInFlight = null
      throw err
    })

  return latestPropertiesInFlight
}

function setDeviceProperties(params, options = {}) {
  markCloudCommandCooldown()
  return request('/thingmodel/set-device-property', {
    method: 'POST',
    retry: 0,
    timeout: hasOwn(options, 'timeout') ? options.timeout : getRequestTimeoutMs(),
    data: {
      product_id: config.productId,
      device_name: config.deviceName,
      params
    }
  })
}

function queryPropertyHistory(identifier, options = {}) {
  const now = Date.now()
  const hours = Math.max(1, toNumber(hasOwn(options, 'hours') ? options.hours : config.historyHours, 6))
  const limit = Math.max(1, toNumber(hasOwn(options, 'limit') ? options.limit : config.historyLimit, 60))
  const start = now - hours * 60 * 60 * 1000

  return request('/thingmodel/query-device-property-history', {
    data: {
      product_id: config.productId,
      device_name: config.deviceName,
      identifier,
      start_time: options.startTime || start,
      end_time: options.endTime || now,
      limit
    }
  })
}

function readValue(node) {
  if (node === null || node === undefined) {
    return null
  }
  if (typeof node !== 'object') {
    return node
  }
  if (node.value && typeof node.value === 'object' && hasOwn(node.value, 'value')) {
    return node.value.value
  }
  if (hasOwn(node, 'value')) {
    return node.value
  }
  if (hasOwn(node, 'property_value')) {
    return node.property_value
  }
  return null
}

function readTime(node) {
  if (!node || typeof node !== 'object') {
    return ''
  }
  const valueNode = node.value && typeof node.value === 'object' ? node.value : null
  return node.time ||
    node.update_time ||
    node.last_time ||
    node.create_time ||
    node.event_time ||
    node.timestamp ||
    node.ts ||
    (valueNode && (valueNode.time ||
      valueNode.update_time ||
      valueNode.last_time ||
      valueNode.create_time ||
      valueNode.event_time ||
      valueNode.timestamp ||
      valueNode.ts)) ||
    ''
}

function hasPropertySnapshot(node) {
  if (node === null || node === undefined) {
    return false
  }
  if (typeof node !== 'object') {
    return true
  }
  if (hasOwn(node, 'value') || hasOwn(node, 'property_value')) {
    return true
  }
  if (node.value && typeof node.value === 'object' && hasOwn(node.value, 'value')) {
    return true
  }
  return !isEmptyValue(readTime(node))
}

function parseTimeValue(value) {
  if (isEmptyValue(value)) {
    return NaN
  }
  if (typeof value === 'number') {
    return value > 0 && value < 1000000000000 ? value * 1000 : value
  }

  const numberValue = Number(value)
  if (Number.isFinite(numberValue)) {
    return numberValue > 0 && numberValue < 1000000000000 ? numberValue * 1000 : numberValue
  }

  const text = String(value).trim()
  let date = new Date(text)
  if (Number.isNaN(date.getTime())) {
    date = new Date(text.replace(/-/g, '/'))
  }
  return date.getTime()
}

function addProperty(map, id, node) {
  if (!id || !hasPropertySnapshot(node)) {
    return
  }
  map[id] = {
    value: readValue(node),
    time: readTime(node)
  }
}

function getPropertyValue(map, id, fallback = null) {
  const node = map && map[id]
  if (!node) {
    return fallback
  }

  const value = node.value
  return isEmptyValue(value) ? fallback : value
}

function getValidBits(map) {
  const value = getPropertyValue(map, 'valid_bits', null)
  if (isEmptyValue(value)) {
    return null
  }
  return toNumber(value, 0)
}

function isPropertyActive(def, map) {
  const value = getPropertyValue(map, def.id, null)
  if (isEmptyValue(value)) {
    return false
  }

  const validBits = getValidBits(map)
  if (validBits === null || !def.validBit) {
    return true
  }

  return (validBits & def.validBit) !== 0
}

function getDisplayValue(def, map) {
  return isPropertyActive(def, map) ? getPropertyValue(map, def.id, '--') : '--'
}

function normalizeLatestProperties(body) {
  const map = {}
  const source = body && (body.data || body.result || body)

  if (Array.isArray(source)) {
    source.forEach((item) => {
      addProperty(map, item.identifier || item.property_id || item.id || item.name || item.key, item)
    })
    return map
  }

  if (!source || typeof source !== 'object') {
    return map
  }

  const list = source.list || source.properties || source.property || source.params
  if (Array.isArray(list)) {
    list.forEach((item) => {
      addProperty(map, item.identifier || item.property_id || item.id || item.name || item.key, item)
    })
    return map
  }

  if (list && typeof list === 'object') {
    Object.keys(list).forEach((key) => addProperty(map, key, list[key]))
    return map
  }

  Object.keys(source).forEach((key) => {
    if (key !== 'code' && key !== 'msg' && key !== 'request_id') {
      addProperty(map, key, source[key])
    }
  })
  return map
}

function normalizeHistory(body, identifier) {
  const source = body && (body.data || body.result || body)
  const rawList = []

  function pushItem(item) {
    const value = readValue(item)
    if (isEmptyValue(value)) {
      return
    }
    const numberValue = toNumber(value, NaN)
    rawList.push({
      value: numberValue,
      time: readTime(item)
    })
  }

  if (Array.isArray(source)) {
    source.forEach(pushItem)
  } else if (source && typeof source === 'object') {
    const list = source.list || source.records || source.items || source.data || source[identifier]
    if (Array.isArray(list)) {
      list.forEach(pushItem)
    } else if (list && typeof list === 'object') {
      Object.keys(list).forEach((key) => pushItem({ value: list[key], time: key }))
    }
  }

  return rawList
    .filter((item) => Number.isFinite(item.value))
    .sort((a, b) => {
      const left = parseTimeValue(a.time)
      const right = parseTimeValue(b.time)

      if (Number.isFinite(left) && Number.isFinite(right)) {
        return left - right
      }

      return String(a.time).localeCompare(String(b.time))
    })
}

function getLatestTimestamp(map) {
  let latest = NaN

  Object.keys(map || {}).forEach((key) => {
    const current = parseTimeValue(map[key] && map[key].time)
    if (Number.isFinite(current) && (!Number.isFinite(latest) || current > latest)) {
      latest = current
    }
  })

  return latest
}

function getLatestTelemetryTimestamp(map) {
  let latest = NaN

  TELEMETRY_PROPERTY_IDS.forEach((key) => {
    const current = parseTimeValue(map && map[key] && map[key].time)
    if (Number.isFinite(current) && (!Number.isFinite(latest) || current > latest)) {
      latest = current
    }
  })

  return latest
}

function isTimestampFresh(timestamp, staleMs = getStaleDataMs()) {
  if (!Number.isFinite(timestamp)) {
    return true
  }

  return (Date.now() - timestamp) <= staleMs
}

function formatTime(value) {
  if (!value) {
    return '--'
  }
  const timestamp = parseTimeValue(value)
  const text = String(value).trim()
  let date = Number.isFinite(timestamp) ? new Date(timestamp) : new Date(text)
  if (Number.isNaN(date.getTime())) {
    date = new Date(text.replace(/-/g, '/'))
  }
  if (Number.isNaN(date.getTime())) {
    return String(value)
  }
  const pad = (num) => (num < 10 ? `0${num}` : String(num))
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
}

module.exports = {
  config,
  SENSOR_VALID_BITS,
  PROPERTY_DEFS,
  THRESHOLD_DEFS,
  TELEMETRY_PROPERTY_IDS,
  isConfigured,
  isEmptyValue,
  toNumber,
  toBoolean,
  clampNumber,
  getRefreshIntervalMs,
  getRequestTimeoutMs,
  getCommandCooldownMs,
  markCloudCommandCooldown,
  isCloudCommandCoolingDown,
  normalizeError,
  queryLatestProperties,
  setDeviceProperties,
  queryPropertyHistory,
  getPropertyValue,
  getValidBits,
  isPropertyActive,
  getDisplayValue,
  normalizeLatestProperties,
  normalizeHistory,
  parseTimeValue,
  getLatestTimestamp,
  getLatestTelemetryTimestamp,
  getStaleDataMs,
  isTimestampFresh,
  formatTime
}
