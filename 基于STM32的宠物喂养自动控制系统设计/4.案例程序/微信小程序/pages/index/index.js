const { ONENET_CONFIG, PROPERTY_META, queryDeviceDetail, queryLatestProperties } = require('../../api/iot-onenet');

const CARD_IDENTIFIERS = [
  'temperature',
  'humidity',
  'air_quality',
  'food_weight',
  'water_level',
  'illuminance',
  'pet_distance'
];

const AUTO_REFRESH_INTERVAL = 2000;

function pad(value) {
  return value < 10 ? `0${value}` : `${value}`;
}

function formatTime(timestamp) {
  const numericTime = Number(timestamp);
  if (!numericTime) {
    return '--';
  }

  const milliseconds = numericTime < 10000000000 ? numericTime * 1000 : numericTime;
  const date = new Date(milliseconds);
  if (Number.isNaN(date.getTime())) {
    return '--';
  }

  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function formatDateTime(value) {
  if (!value) {
    return '--';
  }

  if (typeof value === 'number' || /^\d+$/.test(`${value}`)) {
    return formatTime(value);
  }

  const date = new Date(value);
  if (Number.isNaN(date.getTime()) || date.getFullYear() <= 1) {
    return '--';
  }

  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function parseBool(value) {
  return value === true || value === 'true' || value === '1' || value === 1;
}

function normalizeNumber(value, precision) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return {
      numeric: null,
      text: value === undefined || value === null || value === '' ? '--' : `${value}`
    };
  }

  return {
    numeric: numberValue,
    text: numberValue.toFixed(precision)
  };
}

function clampProgress(value, min, max) {
  if (!Number.isFinite(value) || !Number.isFinite(min) || !Number.isFinite(max) || max <= min) {
    return 0;
  }

  const percent = ((value - min) / (max - min)) * 100;
  return Math.max(0, Math.min(100, Math.round(percent)));
}

function formatMetric(identifier, propertyMap) {
  const meta = PROPERTY_META[identifier];
  const property = propertyMap[identifier];
  const formatted = normalizeNumber(property && property.value, meta.precision || 0);
  const hasValue = formatted.text !== '--';
  const progress = hasValue ? clampProgress(formatted.numeric, meta.min, meta.max) : 0;

  return {
    identifier,
    name: meta.name,
    image: meta.image,
    unit: meta.unit,
    valueText: formatted.text,
    timeText: property ? formatTime(property.time) : '--',
    rangeText: `${meta.min}-${meta.max}${meta.unit}`,
    progress,
    hasValue
  };
}

function buildPropertyMap(list) {
  return list.reduce((result, item) => {
    if (item && item.identifier) {
      result[item.identifier] = item;
    }
    return result;
  }, {});
}

function getLatestTimestamp(list) {
  return list.reduce((latest, item) => {
    const current = Number(item && item.time);
    return Number.isFinite(current) && current > latest ? current : latest;
  }, 0);
}

function buildPetStatus(propertyMap, latestTimestamp) {
  const petMeta = PROPERTY_META.pet_near;
  const petNear = propertyMap.pet_near;
  const petDistance = propertyMap.pet_distance;
  const known = Boolean(petNear);
  const near = known ? parseBool(petNear.value) : false;
  const distance = normalizeNumber(petDistance && petDistance.value, PROPERTY_META.pet_distance.precision);
  const distanceText = distance.text === '--' ? '' : `，距离 ${distance.text}${PROPERTY_META.pet_distance.unit}`;

  if (!known) {
    return {
      known: false,
      near: false,
      stateClass: 'is-unknown',
      image: petMeta.imageAway,
      label: '等待设备上报',
      description: '暂无宠物靠近状态',
      timeText: latestTimestamp ? formatTime(latestTimestamp) : '--'
    };
  }

  return {
    known: true,
    near,
    stateClass: near ? 'is-near' : 'is-away',
    image: near ? petMeta.imageNear : petMeta.imageAway,
    label: near ? '宠物在范围内' : '宠物不在范围内',
    description: near ? `已检测到宠物靠近${distanceText}` : `当前未检测到宠物靠近${distanceText}`,
    timeText: formatTime(petNear.time)
  };
}

function buildDashboard(list) {
  const propertyMap = buildPropertyMap(list);
  const latestTimestamp = getLatestTimestamp(list);
  const cards = CARD_IDENTIFIERS.map((identifier) => formatMetric(identifier, propertyMap));

  return {
    hasData: list.length > 0,
    empty: list.length === 0,
    updatedAt: latestTimestamp ? formatTime(latestTimestamp) : '--',
    petStatus: buildPetStatus(propertyMap, latestTimestamp),
    cards
  };
}

function buildDeviceStatus(detail, errorMessage) {
  if (errorMessage) {
    return {
      stateClass: 'is-unknown',
      label: '状态未知',
      description: errorMessage,
      lastTimeText: '--'
    };
  }

  const status = Number(detail && detail.status);
  const lastTimeText = formatDateTime(detail && detail.last_time);

  if (status === 1) {
    return {
      stateClass: 'is-online',
      label: '单片机在线',
      description: '云平台显示设备在线',
      lastTimeText
    };
  }

  if (status === 0) {
    return {
      stateClass: 'is-offline',
      label: '单片机离线',
      description: '云平台显示设备离线',
      lastTimeText
    };
  }

  if (status === 2) {
    return {
      stateClass: 'is-inactive',
      label: '单片机未激活',
      description: '设备还未完成激活',
      lastTimeText
    };
  }

  return {
    stateClass: 'is-unknown',
    label: '状态未知',
    description: '云平台未返回明确状态',
    lastTimeText
  };
}

function getErrorMessage(error) {
  if (!error) {
    return '数据获取失败，请稍后重试';
  }
  return error.message || error.errMsg || '数据获取失败，请稍后重试';
}

function settle(promise) {
  return promise
    .then((value) => ({ status: 'fulfilled', value }))
    .catch((reason) => ({ status: 'rejected', reason }));
}

Page({
  data: {
    productId: ONENET_CONFIG.productId,
    deviceName: ONENET_CONFIG.deviceName,
    tokenExpiresAt: '2028-01-01 01:00:00',
    loading: true,
    refreshing: false,
    hasData: false,
    empty: false,
    errorMessage: '',
    updatedAt: '--',
    deviceStatus: {
      stateClass: 'is-unknown',
      label: '状态查询中',
      description: '正在获取单片机在线状态',
      lastTimeText: '--'
    },
    petStatus: {
      known: false,
      near: false,
      stateClass: 'is-unknown',
      image: PROPERTY_META.pet_near.imageAway,
      label: '等待设备上报',
      description: '暂无宠物靠近状态',
      timeText: '--'
    },
    cards: CARD_IDENTIFIERS.map((identifier) => formatMetric(identifier, {}))
  },

  onLoad() {
    this.fetchDeviceData({ showLoading: true });
  },

  onShow() {
    this.startAutoRefresh();
  },

  onHide() {
    this.stopAutoRefresh();
  },

  onUnload() {
    this.stopAutoRefresh();
  },

  onPullDownRefresh() {
    this.fetchDeviceData({ showRefreshing: true, stopPullDown: true });
  },

  handleRefresh() {
    this.fetchDeviceData({ showRefreshing: true });
  },

  startAutoRefresh() {
    this.stopAutoRefresh();
    this.autoRefreshTimer = setInterval(() => {
      this.fetchDeviceData({ silent: true });
    }, AUTO_REFRESH_INTERVAL);
  },

  stopAutoRefresh() {
    if (this.autoRefreshTimer) {
      clearInterval(this.autoRefreshTimer);
      this.autoRefreshTimer = null;
    }
  },

  fetchDeviceData(options = {}) {
    if (this.requesting) {
      if (options.stopPullDown) {
        wx.stopPullDownRefresh();
      }
      return;
    }

    this.requesting = true;
    const nextState = {};
    if (options.showLoading || (!this.data.hasData && !options.silent)) {
      nextState.loading = true;
    }
    if (options.showRefreshing) {
      nextState.refreshing = true;
    }
    if (!options.silent) {
      nextState.errorMessage = '';
    }
    this.setData(nextState);

    const completeRefresh = () => {
      this.requesting = false;
      if (options.stopPullDown) {
        wx.stopPullDownRefresh();
      }
    };

    Promise.all([
      settle(queryLatestProperties()),
      settle(queryDeviceDetail())
    ])
      .then(([propertyResult, detailResult]) => {
        const nextData = {
          loading: false,
          refreshing: false
        };

        if (propertyResult.status === 'fulfilled') {
          Object.assign(nextData, buildDashboard(propertyResult.value.list), {
            errorMessage: ''
          });
        } else {
          nextData.errorMessage = getErrorMessage(propertyResult.reason);
          if (!this.data.hasData) {
            nextData.empty = false;
          }
        }

        if (detailResult.status === 'fulfilled') {
          nextData.deviceStatus = buildDeviceStatus(detailResult.value.data);
        } else {
          nextData.deviceStatus = buildDeviceStatus(null, getErrorMessage(detailResult.reason));
        }

        this.setData(nextData);
      }, (error) => {
        this.setData({
          loading: false,
          refreshing: false,
          errorMessage: getErrorMessage(error),
          empty: !this.data.hasData && this.data.cards.length === 0
        });
      })
      .then(completeRefresh, completeRefresh);
  }
});
