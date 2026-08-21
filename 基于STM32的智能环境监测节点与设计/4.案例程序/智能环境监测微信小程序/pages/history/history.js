const layout = require('../../utils/layout')
const onenet = require('../../utils/onenet')

const CHART_HEIGHT = 230

Page({
  data: {
    safeTop: 64,
    loading: false,
    error: '',
    metrics: [],
    selectedId: 'temperature',
    selectedMetric: null,
    chartWidth: 320,
    chartHeight: CHART_HEIGHT,
    points: [],
    recentPoints: [],
    statCards: [],
    rangeText: '--',
    axisText: '',
    chartCountText: '0 条'
  },

  onLoad() {
    this.historyRequestId = 0
    const metrics = onenet.PROPERTY_DEFS
    const chartSize = this.getChartSize()
    this.setData({
      safeTop: layout.getPageTopPadding(),
      metrics,
      selectedId: metrics[0].id,
      selectedMetric: metrics[0],
      chartWidth: chartSize.width,
      chartHeight: chartSize.height,
      statCards: this.getEmptyStats(metrics[0])
    }, () => {
      this.loadHistory()
    })
  },

  getChartSize() {
    try {
      const systemInfo = wx.getSystemInfoSync ? wx.getSystemInfoSync() : {}
      const windowWidth = systemInfo.windowWidth || 375
      const rpx = windowWidth / 750
      const horizontalPadding = (28 * 2 + 24 * 2) * rpx
      return {
        width: Math.max(280, Math.floor(windowWidth - horizontalPadding)),
        height: CHART_HEIGHT
      }
    } catch (err) {
      return {
        width: 320,
        height: CHART_HEIGHT
      }
    }
  },

  selectMetric(event) {
    const id = event.currentTarget.dataset.id
    if (id === this.data.selectedId) {
      return
    }

    const selectedMetric = this.data.metrics.find((item) => item.id === id)
    this.setData({
      selectedId: id,
      selectedMetric,
      points: [],
      recentPoints: [],
      statCards: this.getEmptyStats(selectedMetric),
      rangeText: '--',
      axisText: '',
      chartCountText: '0 条'
    }, () => {
      this.loadHistory()
    })
  },

  loadHistory() {
    const metric = this.data.selectedMetric
    if (!metric) {
      return
    }

    const requestId = ++this.historyRequestId
    this.setData({ loading: true, error: '' })
    onenet.queryPropertyHistory(metric.id)
      .then((res) => {
        if (requestId !== this.historyRequestId) {
          return
        }

        const rawPoints = onenet.normalizeHistory(res, metric.id)
        const model = this.buildChartModel(rawPoints, metric)
        this.setData({
          points: model.points,
          recentPoints: model.recentPoints,
          statCards: model.statCards,
          rangeText: model.rangeText,
          axisText: model.axisText,
          chartCountText: model.chartCountText,
          error: ''
        }, () => {
          this.drawChart(model.points, metric, model.range)
        })
      })
      .catch((err) => {
        if (requestId !== this.historyRequestId) {
          return
        }
        this.setData({
          error: onenet.normalizeError(err),
          points: [],
          recentPoints: [],
          statCards: this.getEmptyStats(metric),
          rangeText: '--',
          axisText: '',
          chartCountText: '0 条'
        }, () => {
          this.drawChart([], metric, this.getChartRange([], metric))
        })
      })
      .then(() => {
        if (requestId === this.historyRequestId) {
          this.setData({ loading: false })
        }
      })
  },

  buildChartModel(rawPoints, metric) {
    const points = rawPoints.slice(-60).map((item, index) => {
      const timestamp = onenet.parseTimeValue(item.time)
      return {
        key: `${index}-${item.time || timestamp}`,
        value: item.value,
        valueText: this.formatNumber(item.value),
        time: this.formatPointTime(item.time, timestamp),
        timestamp
      }
    })
    const axisInfo = this.getAxisInfo(points)
    const axisPoints = points.map((item) => ({
      ...item,
      axisLabel: this.formatAxisLabel(item, axisInfo)
    }))

    if (!axisPoints.length) {
      return {
        points: [],
        recentPoints: [],
        statCards: this.getEmptyStats(metric),
        rangeText: '--',
        axisText: '',
        chartCountText: '0 条',
        range: this.getChartRange([], metric)
      }
    }

    const values = axisPoints.map((item) => item.value)
    const latest = axisPoints[axisPoints.length - 1].value
    const max = Math.max(...values)
    const min = Math.min(...values)
    const avg = values.reduce((sum, value) => sum + value, 0) / values.length

    return {
      points: axisPoints,
      recentPoints: axisPoints.slice(-6).reverse(),
      statCards: [
        { label: '最新', value: this.formatNumber(latest), unit: metric.unit },
        { label: '最高', value: this.formatNumber(max), unit: metric.unit },
        { label: '最低', value: this.formatNumber(min), unit: metric.unit },
        { label: '平均', value: this.formatNumber(avg), unit: metric.unit }
      ],
      rangeText: `${axisPoints[0].time} - ${axisPoints[axisPoints.length - 1].time}`,
      axisText: axisInfo.text,
      chartCountText: `${axisInfo.text ? `横轴/${axisInfo.text} · ` : ''}${axisPoints.length} 条`,
      range: this.getChartRange(values, metric)
    }
  },

  getAxisInfo(points) {
    const timestamps = points
      .map((item) => item.timestamp)
      .filter((item) => Number.isFinite(item))

    if (timestamps.length < 2) {
      return { start: NaN, unitMs: 1000, suffix: '秒', decimals: 0, text: '秒' }
    }

    const start = Math.min(...timestamps)
    const end = Math.max(...timestamps)
    const span = Math.max(0, end - start)

    if (span < 2 * 60 * 1000) {
      return { start, unitMs: 1000, suffix: '秒', decimals: 0, text: '秒' }
    }

    if (span < 2 * 60 * 60 * 1000) {
      return {
        start,
        unitMs: 60 * 1000,
        suffix: '分',
        decimals: span < 10 * 60 * 1000 ? 1 : 0,
        text: '分'
      }
    }

    return {
      start,
      unitMs: 60 * 60 * 1000,
      suffix: '时',
      decimals: span < 6 * 60 * 60 * 1000 ? 1 : 0,
      text: '时'
    }
  },

  formatAxisLabel(point, axisInfo) {
    if (!point || !Number.isFinite(point.timestamp) || !Number.isFinite(axisInfo.start)) {
      return point ? point.time : ''
    }

    const offset = Math.max(0, point.timestamp - axisInfo.start)
    const value = offset / axisInfo.unitMs
    const text = axisInfo.decimals > 0 && !Number.isInteger(value)
      ? value.toFixed(axisInfo.decimals)
      : String(Math.round(value))
    return `${text}${axisInfo.suffix}`
  },

  formatPointTime(rawTime, timestamp) {
    if (!Number.isFinite(timestamp)) {
      return onenet.formatTime(rawTime)
    }

    const date = new Date(timestamp)
    const pad = (num) => (num < 10 ? `0${num}` : String(num))
    return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
  },

  getEmptyStats(metric) {
    const unit = metric ? metric.unit : ''
    return ['最新', '最高', '最低', '平均'].map((label) => ({
      label,
      value: '--',
      unit
    }))
  },

  getChartRange(values, metric) {
    if (!values.length) {
      return {
        min: metric.min,
        max: metric.max
      }
    }

    let min = Math.min(...values)
    let max = Math.max(...values)
    if (min === max) {
      const pad = Math.max(1, Math.abs(max) * 0.05)
      min -= pad
      max += pad
    } else {
      const pad = (max - min) * 0.12
      min -= pad
      max += pad
    }

    if (Number.isFinite(metric.min)) {
      min = Math.max(metric.min, min)
    }
    if (Number.isFinite(metric.max)) {
      max = Math.min(metric.max, max)
    }
    if (max <= min) {
      max = min + 1
    }

    return { min, max }
  },

  drawChart(points, metric, range) {
    const ctx = wx.createCanvasContext('historyChart', this)
    const width = this.data.chartWidth
    const height = this.data.chartHeight
    const padding = { left: 42, right: 12, top: 18, bottom: 34 }
    const plotLeft = padding.left
    const plotTop = padding.top
    const plotWidth = width - padding.left - padding.right
    const plotHeight = height - padding.top - padding.bottom
    const plotBottom = plotTop + plotHeight
    const span = Math.max(1, range.max - range.min)

    ctx.clearRect(0, 0, width, height)
    ctx.setFillStyle('#ffffff')
    ctx.fillRect(0, 0, width, height)

    ctx.setFontSize(10)
    ctx.setFillStyle('#8a96a8')
    ctx.setStrokeStyle('#e6edf5')
    ctx.setLineWidth(1)

    for (let i = 0; i <= 4; i++) {
      const y = plotTop + (plotHeight / 4) * i
      const value = range.max - (span / 4) * i
      ctx.beginPath()
      ctx.moveTo(plotLeft, y)
      ctx.lineTo(plotLeft + plotWidth, y)
      ctx.stroke()
      ctx.fillText(this.formatAxisValue(value), 0, y + 3)
    }

    ctx.beginPath()
    ctx.moveTo(plotLeft, plotTop)
    ctx.lineTo(plotLeft, plotBottom)
    ctx.lineTo(plotLeft + plotWidth, plotBottom)
    ctx.stroke()

    if (!points.length) {
      ctx.draw()
      return
    }

    const chartPoints = points.map((item, index) => {
      const x = points.length === 1
        ? plotLeft + plotWidth / 2
        : plotLeft + (plotWidth * index) / (points.length - 1)
      const y = plotTop + ((range.max - item.value) / span) * plotHeight
      return { x, y }
    })

    ctx.beginPath()
    this.traceCurve(ctx, chartPoints)
    ctx.lineTo(chartPoints[chartPoints.length - 1].x, plotBottom)
    ctx.lineTo(chartPoints[0].x, plotBottom)
    ctx.closePath()
    ctx.setFillStyle('rgba(22, 119, 255, 0.12)')
    ctx.fill()

    ctx.beginPath()
    this.traceCurve(ctx, chartPoints)
    ctx.setStrokeStyle('#1677ff')
    ctx.setLineWidth(2)
    ctx.stroke()

    if (chartPoints.length <= 30) {
      chartPoints.forEach((point) => {
        ctx.beginPath()
        ctx.arc(point.x, point.y, 2.5, 0, Math.PI * 2)
        ctx.setFillStyle('#ffffff')
        ctx.fill()
        ctx.setStrokeStyle('#1677ff')
        ctx.setLineWidth(1)
        ctx.stroke()
      })
    }

    this.drawTimeLabels(ctx, points, chartPoints, plotBottom)
    ctx.draw()
  },

  traceCurve(ctx, chartPoints) {
    if (!chartPoints.length) {
      return
    }

    ctx.moveTo(chartPoints[0].x, chartPoints[0].y)
    if (chartPoints.length === 1) {
      return
    }

    for (let i = 1; i < chartPoints.length - 1; i++) {
      const midX = (chartPoints[i].x + chartPoints[i + 1].x) / 2
      const midY = (chartPoints[i].y + chartPoints[i + 1].y) / 2
      ctx.quadraticCurveTo(chartPoints[i].x, chartPoints[i].y, midX, midY)
    }

    const beforeLast = chartPoints[chartPoints.length - 2]
    const last = chartPoints[chartPoints.length - 1]
    ctx.quadraticCurveTo(beforeLast.x, beforeLast.y, last.x, last.y)
  },

  drawTimeLabels(ctx, points, chartPoints, plotBottom) {
    const labels = []
    const used = {}
    const baseLabels = [
      { index: 0, align: 'left' },
      { index: Math.floor((points.length - 1) / 2), align: 'center' },
      { index: points.length - 1, align: 'right' }
    ]

    baseLabels.forEach((item) => {
      if (!used[item.index]) {
        labels.push(item)
        used[item.index] = true
      }
    })

    ctx.setFillStyle('#8a96a8')
    ctx.setFontSize(10)
    labels.forEach((item) => {
      const point = chartPoints[item.index]
      if (!point) {
        return
      }
      if (ctx.setTextAlign) {
        ctx.setTextAlign(item.align)
      }
      ctx.fillText(points[item.index].axisLabel || points[item.index].time, point.x, plotBottom + 22)
    })
    if (ctx.setTextAlign) {
      ctx.setTextAlign('left')
    }
  },

  formatNumber(value) {
    if (!Number.isFinite(value)) {
      return '--'
    }
    return Number.isInteger(value) ? String(value) : value.toFixed(1)
  },

  formatAxisValue(value) {
    if (Math.abs(value) >= 100) {
      return String(Math.round(value))
    }
    return Number.isInteger(value) ? String(value) : value.toFixed(1)
  },

  goBack() {
    wx.navigateBack()
  }
})
