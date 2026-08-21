const { config, queryDevicePropertyHistory } = require('../../utils/onenet');

const HISTORY_LIMIT = 30;
const HISTORY_WINDOW_MS = 24 * 60 * 60 * 1000;
const CHART_WIDTH = 690;
const CHART_HEIGHT = 260;

const CHART_DEFS = [
  {
    key: 'temperature',
    identifier: config.propertyKeys.temperature,
    title: '温度曲线',
    unit: '°C',
    color: '#ea7e47',
    fillColor: 'rgba(234, 126, 71, 0.12)',
    gridColor: 'rgba(234, 126, 71, 0.15)',
    theme: 'warm',
    canvasId: 'tempHistoryChart'
  },
  {
    key: 'humidity',
    identifier: config.propertyKeys.humidity,
    title: '湿度曲线',
    unit: '%',
    color: '#2b9f94',
    fillColor: 'rgba(43, 159, 148, 0.12)',
    gridColor: 'rgba(43, 159, 148, 0.15)',
    theme: 'cool',
    canvasId: 'humiHistoryChart'
  },
  {
    key: 'smoke',
    identifier: config.propertyKeys.smoke,
    title: '烟雾浓度曲线',
    unit: '%',
    color: '#d9a137',
    fillColor: 'rgba(217, 161, 55, 0.12)',
    gridColor: 'rgba(217, 161, 55, 0.15)',
    theme: 'amber',
    canvasId: 'smokeHistoryChart'
  }
];

function formatValue(value, unit) {
  return value === null ? '--' : `${value}${unit}`;
}

function formatTimeText(timestamp) {
  if (!timestamp) {
    return '--';
  }

  const date = new Date(timestamp);
  const pad = (value) => String(value).padStart(2, '0');
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function createEmptyCharts() {
  return CHART_DEFS.map((item) => ({
    ...item,
    latestText: '--',
    minText: '--',
    maxText: '--',
    lastUpdatedText: '--',
    pointCount: 0,
    empty: true,
    points: []
  }));
}

function buildChartCard(definition, points) {
  const values = points.map((point) => point.value);
  const latest = values.length ? values[values.length - 1] : null;
  const minValue = values.length ? Math.min(...values) : null;
  const maxValue = values.length ? Math.max(...values) : null;
  const lastUpdated = points.length ? points[points.length - 1].time : 0;

  return {
    ...definition,
    latestText: formatValue(latest, definition.unit),
    minText: formatValue(minValue, definition.unit),
    maxText: formatValue(maxValue, definition.unit),
    lastUpdatedText: formatTimeText(lastUpdated),
    pointCount: points.length,
    empty: points.length === 0,
    points
  };
}

function drawChart(page, chart) {
  const context = wx.createCanvasContext(chart.canvasId, page);
  const width = CHART_WIDTH;
  const height = CHART_HEIGHT;
  const padding = {
    left: 58,
    right: 24,
    top: 20,
    bottom: 42
  };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;

  context.clearRect(0, 0, width, height);
  context.setFillStyle('#f7fbfb');
  context.fillRect(0, 0, width, height);

  if (!chart.points.length) {
    context.setFillStyle('#7a8891');
    context.setFontSize(20);
    context.setTextAlign('center');
    context.fillText('暂无历史数据', width / 2, height / 2);
    context.draw();
    return;
  }

  const values = chart.points.map((point) => point.value);
  let minValue = Math.min(...values);
  let maxValue = Math.max(...values);

  if (minValue === maxValue) {
    minValue -= 1;
    maxValue += 1;
  }

  const projectX = (index) => {
    if (chart.points.length === 1) {
      return padding.left + plotWidth / 2;
    }
    return padding.left + (plotWidth * index) / (chart.points.length - 1);
  };

  const projectY = (value) => padding.top + ((maxValue - value) / (maxValue - minValue)) * plotHeight;

  for (let index = 0; index < 4; index += 1) {
    const y = padding.top + (plotHeight * index) / 3;
    context.beginPath();
    context.setStrokeStyle(chart.gridColor);
    context.setLineWidth(1);
    context.moveTo(padding.left, y);
    context.lineTo(width - padding.right, y);
    context.stroke();
  }

  context.setFillStyle('#70808a');
  context.setFontSize(18);
  context.setTextAlign('left');
  context.fillText(`${maxValue}`, 8, padding.top + 6);
  context.fillText(`${minValue}`, 8, padding.top + plotHeight);

  context.beginPath();
  chart.points.forEach((point, index) => {
    const x = projectX(index);
    const y = projectY(point.value);

    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  });
  context.lineTo(projectX(chart.points.length - 1), height - padding.bottom);
  context.lineTo(projectX(0), height - padding.bottom);
  context.closePath();
  context.setFillStyle(chart.fillColor);
  context.fill();

  context.beginPath();
  chart.points.forEach((point, index) => {
    const x = projectX(index);
    const y = projectY(point.value);

    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  });
  context.setStrokeStyle(chart.color);
  context.setLineWidth(4);
  context.setLineCap('round');
  context.setLineJoin('round');
  context.stroke();

  chart.points.forEach((point, index) => {
    const x = projectX(index);
    const y = projectY(point.value);

    context.beginPath();
    context.setFillStyle('#ffffff');
    context.arc(x, y, 5, 0, Math.PI * 2);
    context.fill();
    context.beginPath();
    context.setFillStyle(chart.color);
    context.arc(x, y, 3, 0, Math.PI * 2);
    context.fill();
  });

  const firstPoint = chart.points[0];
  const middlePoint = chart.points[Math.floor((chart.points.length - 1) / 2)];
  const lastPoint = chart.points[chart.points.length - 1];

  context.setFillStyle('#70808a');
  context.setFontSize(18);
  context.setTextAlign('left');
  context.fillText(formatTimeText(firstPoint.time), padding.left, height - 10);
  context.setTextAlign('center');
  context.fillText(formatTimeText(middlePoint.time), padding.left + plotWidth / 2, height - 10);
  context.setTextAlign('right');
  context.fillText(formatTimeText(lastPoint.time), width - padding.right, height - 10);

  context.draw();
}

Page({
  data: {
    topSafeHeight: 88,
    charts: createEmptyCharts(),
    updatedAtText: '--',
    rangeText: `最近 ${HISTORY_LIMIT} 条云端历史数据`,
    loading: true,
    refreshing: false,
    errorMessage: ''
  },

  onLoad() {
    const app = getApp();
    const systemInfo = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync();
    const fallbackTopSafeHeight = Math.max((systemInfo.statusBarHeight || 0) + 44, 88);

    this.setData({
      topSafeHeight: (app.globalData && app.globalData.navOffsetTop) || fallbackTopSafeHeight
    });

    this.refreshCharts(true);
  },

  onShow() {
    this.startPolling();
    if (!this.data.loading) {
      this.refreshCharts(false);
    }
  },

  onHide() {
    this.stopPolling();
  },

  onUnload() {
    this.stopPolling();
  },

  onPullDownRefresh() {
    this.refreshCharts(false, false, true);
  },

  startPolling() {
    if (this.pollTimer) {
      return;
    }

    this.pollTimer = setInterval(() => {
      this.refreshCharts(false);
    }, config.pollInterval || 2000);
  },

  stopPolling() {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
  },

  handleManualRefresh() {
    this.refreshCharts(false, true);
  },

  goHomePage() {
    wx.redirectTo({
      url: '/pages/index/index'
    });
  },

  refreshCharts(showLoading, showToast, fromPullDown) {
    if (this.requesting) {
      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
      return;
    }

    this.requesting = true;
    this.setData({
      loading: showLoading ? true : this.data.loading,
      refreshing: showToast ? true : this.data.refreshing,
      errorMessage: ''
    });

    const endTime = Date.now();
    const startTime = endTime - HISTORY_WINDOW_MS;

    Promise.all(
      CHART_DEFS.map((item) => queryDevicePropertyHistory(item.identifier, {
        startTime,
        endTime,
        limit: HISTORY_LIMIT
      }))
    )
      .then((seriesList) => {
        const charts = CHART_DEFS.map((item, index) => buildChartCard(item, seriesList[index] || []));
        const latestTimes = charts
          .map((item) => (item.points.length ? item.points[item.points.length - 1].time : 0))
          .filter(Boolean);

        this.setData({
          charts,
          updatedAtText: latestTimes.length ? formatTimeText(Math.max(...latestTimes)) : '--',
          loading: false,
          refreshing: false,
          errorMessage: ''
        });

        wx.nextTick(() => {
          charts.forEach((chart) => {
            if (!chart.empty) {
              drawChart(this, chart);
            }
          });
        });

        if (showToast) {
          wx.showToast({
            title: '曲线已刷新',
            icon: 'success'
          });
        }
      })
      .catch((error) => {
        this.setData({
          loading: false,
          refreshing: false,
          errorMessage: error.message || '历史数据加载失败'
        });

        if (showToast) {
          wx.showToast({
            title: '刷新失败',
            icon: 'none'
          });
        }
      })
      .finally(() => {
        this.requesting = false;
        if (fromPullDown) {
          wx.stopPullDownRefresh();
        }
      });
  }
});
