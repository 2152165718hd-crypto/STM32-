const layout = require('../../utils/layout')
const onenet = require('../../utils/onenet')

const ALARM_LABELS = [
  { bit: 0x01, text: 'PM2.5超限' },
  { bit: 0x02, text: '气体超限' },
  { bit: 0x04, text: '光照超限' },
  { bit: 0x08, text: '温度超限' },
  { bit: 0x10, text: '湿度超限' }
]

Page({
  data: {
    safeTop: 64,
    configured: onenet.isConfigured(),
    hasData: false,
    loading: false,
    error: '',
    lastUpdate: '--',
    slaveOnline: false,
    dataFresh: true,
    statusMode: 'waiting',
    statusTitle: '等待上报',
    statusText: '设备暂无已上报数据',
    statusIcon: '/image/icon-cloud.svg',
    alarmActive: false,
    alarmList: [],
    cards: [],
    thresholds: []
  },

  onLoad() {
    this.refreshing = false
    this.pageVisible = false
    this.timer = null
    this.setData({ safeTop: layout.getPageTopPadding() })
    this.buildInitialState()
    this.refresh()
  },

  onShow() {
    this.pageVisible = true
    this.startTimer()
  },

  onHide() {
    this.pageVisible = false
    this.stopTimer()
  },

  onUnload() {
    this.pageVisible = false
    this.stopTimer()
  },

  buildInitialState() {
    this.setData({
      cards: onenet.PROPERTY_DEFS.map((item) => ({
        ...item,
        value: '--',
        active: false
      })),
      thresholds: onenet.THRESHOLD_DEFS.map((item) => ({
        ...item,
        value: item.defaultValue
      }))
    })
  },

  buildStatusState(hasData, dataFresh, slaveOnline, alarmActive) {
    if (!hasData) {
      return {
        statusMode: 'waiting',
        statusTitle: '等待上报',
        statusText: '设备暂无已上报数据',
        statusIcon: '/image/icon-cloud.svg'
      }
    }

    if (!dataFresh) {
      return {
        statusMode: 'offline',
        statusTitle: '数据超时',
        statusText: '云端仍是旧数据，设备可能已离线',
        statusIcon: '/image/icon-offline.svg'
      }
    }

    if (!slaveOnline) {
      return {
        statusMode: 'offline',
        statusTitle: '检测端离线',
        statusText: '主机在线，但检测端未返回新数据',
        statusIcon: '/image/icon-offline.svg'
      }
    }

    if (alarmActive) {
      return {
        statusMode: 'alarm',
        statusTitle: '报警中',
        statusText: '检测端在线',
        statusIcon: '/image/icon-alarm.svg'
      }
    }

    return {
      statusMode: 'normal',
      statusTitle: '运行正常',
      statusText: '检测端在线',
      statusIcon: '/image/icon-cloud.svg'
    }
  },

  startTimer() {
    this.stopTimer()
    this.scheduleNextRefresh()
  },

  scheduleNextRefresh(delay = onenet.getRefreshIntervalMs()) {
    if (!this.pageVisible) {
      return
    }

    this.stopTimer()
    this.timer = setTimeout(() => {
      this.timer = null
      if (!this.pageVisible) {
        return
      }
      this.refresh(false).then(() => {
        this.scheduleNextRefresh()
      })
    }, delay)
  },

  stopTimer() {
    if (this.timer) {
      clearInterval(this.timer)
      this.timer = null
    }
  },

  refresh(showLoading = true) {
    if (typeof showLoading !== 'boolean') {
      showLoading = true
    }

    if (this.refreshing) {
      return Promise.resolve(false)
    }

    if (!showLoading && onenet.isCloudCommandCoolingDown()) {
      return Promise.resolve(false)
    }

    this.refreshing = true
    if (showLoading) {
      this.setData({ loading: true, error: '' })
    }

    return onenet.queryLatestProperties()
      .then((res) => {
        const map = onenet.normalizeLatestProperties(res)
        this.applyLatestMap(map)
      })
      .catch((err) => {
        this.applyRefreshError(onenet.normalizeError(err))
      })
      .then(() => {
        this.refreshing = false
        this.setData({ loading: false })
        return true
      })
  },

  applyLatestMap(map) {
    const hasData = Object.keys(map).length > 0
    const latestTimestamp = onenet.getLatestTimestamp(map)
    const telemetryTimestamp = onenet.getLatestTelemetryTimestamp(map)
    const dataFresh = hasData ? onenet.isTimestampFresh(telemetryTimestamp) : true
    const slaveOnline = hasData && dataFresh ? onenet.toBoolean((map.slave_online || {}).value, false) : false
    const cards = onenet.PROPERTY_DEFS.map((item) => {
      const active = dataFresh && slaveOnline && onenet.isPropertyActive(item, map)
      return {
        ...item,
        value: active ? onenet.getDisplayValue(item, map) : '--',
        active
      }
    })

    const thresholds = onenet.THRESHOLD_DEFS.map((item) => {
      const node = map[item.id] || {}
      const value = node.value
      return {
        ...item,
        value: onenet.isEmptyValue(value) ? item.defaultValue : onenet.toNumber(value, item.defaultValue)
      }
    })

    const alarmMask = dataFresh && slaveOnline ? onenet.toNumber((map.alarm_mask || {}).value, 0) : 0
    const alarmActive = dataFresh && slaveOnline &&
      (onenet.toBoolean((map.alarm_active || {}).value, false) || alarmMask !== 0)
    const alarmList = ALARM_LABELS
      .filter((item) => (alarmMask & item.bit) !== 0)
      .map((item) => item.text)
    const status = this.buildStatusState(hasData, dataFresh, slaveOnline, alarmActive)

    this.setData({
      hasData,
      dataFresh,
      cards,
      thresholds,
      alarmActive: hasData && dataFresh ? alarmActive : false,
      alarmList: hasData && dataFresh ? (alarmList.length > 0 ? alarmList : (alarmActive ? ['当前存在报警'] : [])) : [],
      slaveOnline,
      lastUpdate: hasData ? onenet.formatTime(latestTimestamp) : '--',
      ...status,
      error: ''
    })
  },

  applyRefreshError(message) {
    const hasCachedData = this.data.hasData && this.data.cards.length > 0
    const cards = this.data.cards.map((item) => ({
      ...item,
      value: hasCachedData ? item.value : '--',
      active: false
    }))

    this.setData({
      cards,
      slaveOnline: false,
      dataFresh: false,
      alarmActive: false,
      alarmList: [],
      statusMode: 'offline',
      statusTitle: '数据未刷新',
      statusText: hasCachedData ? '刷新失败，已保留上次数据' : '无法读取 OneNET 最新数据',
      statusIcon: '/image/icon-offline.svg',
      error: message
    })
  },

  goThreshold() {
    wx.navigateTo({ url: '/pages/threshold/threshold' })
  },

  goHistory() {
    wx.navigateTo({ url: '/pages/history/history' })
  }
})
