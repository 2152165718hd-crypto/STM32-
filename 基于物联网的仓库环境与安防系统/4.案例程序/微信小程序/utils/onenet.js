const config = require('./config');

function hasValidAuthorization() {
  return Boolean(config.authorization) && config.authorization.indexOf('REPLACE_WITH_') !== 0;
}

function request(options) {
  return new Promise((resolve, reject) => {
    if (!hasValidAuthorization()) {
      reject(new Error('请先在 utils/config.js 中配置 OneNet HTTP authorization'));
      return;
    }

    wx.request({
      url: `${config.baseUrl}${options.url}`,
      method: options.method || 'GET',
      timeout: options.timeout || config.requestTimeout,
      data: options.data || undefined,
      header: {
        authorization: config.authorization,
        'content-type': 'application/json'
      },
      success(res) {
        const responseData = res.data || {};
        const businessCode = Number(responseData.code);

        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(new Error(responseData.msg || responseData.message || `HTTP ${res.statusCode}`));
          return;
        }

        if (!Number.isNaN(businessCode) && businessCode !== 0 && businessCode !== 200) {
          reject(new Error(responseData.msg || responseData.message || `OneNet code ${responseData.code}`));
          return;
        }

        resolve(responseData);
      },
      fail(err) {
        reject(new Error(err.errMsg || '请求失败'));
      }
    });
  });
}

function getResponsePayload(source) {
  if (!source || typeof source !== 'object') {
    return null;
  }

  if (source.data !== undefined) {
    return source.data;
  }

  return source;
}

function getItemIdentifier(item, fallbackKey) {
  if (item && typeof item === 'object') {
    return item.identifier || item.id || item.name || item.code || item.key || fallbackKey;
  }
  return fallbackKey;
}

function getItemValue(item) {
  if (item && typeof item === 'object') {
    if (item.value !== undefined) {
      return item.value;
    }
    if (item.current_value !== undefined) {
      return item.current_value;
    }
    if (item.last_value !== undefined) {
      return item.last_value;
    }
    if (item.val !== undefined) {
      return item.val;
    }
  }

  return item;
}

function getItemTime(item) {
  if (item && typeof item === 'object') {
    return item.time || item.update_time || item.updated_at || item.ts || '';
  }
  return '';
}

function normalizePropertyCollection(source) {
  const payload = getResponsePayload(source);

  if (!payload) {
    return [];
  }

  if (Array.isArray(payload)) {
    return payload;
  }

  if (Array.isArray(payload.list)) {
    return payload.list;
  }

  if (Array.isArray(payload.data)) {
    return payload.data;
  }

  if (payload.params && typeof payload.params === 'object') {
    return Object.keys(payload.params).map((key) => ({
      identifier: key,
      value: getItemValue(payload.params[key]),
      time: getItemTime(payload.params[key])
    }));
  }

  if (typeof payload === 'object') {
    return Object.keys(payload).map((key) => ({
      identifier: getItemIdentifier(payload[key], key),
      value: getItemValue(payload[key]),
      time: getItemTime(payload[key])
    }));
  }

  return [];
}

function buildPropertyMap(response) {
  const items = normalizePropertyCollection(response);
  const propertyMap = {};
  let latestTime = '';

  items.forEach((item) => {
    const identifier = getItemIdentifier(item);
    if (!identifier) {
      return;
    }

    propertyMap[identifier] = getItemValue(item);

    const currentTime = getItemTime(item);
    if (currentTime && String(currentTime) > String(latestTime)) {
      latestTime = currentTime;
    }
  });

  return {
    propertyMap,
    latestTime
  };
}

function normalizeNumber(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function normalizeBoolean(value) {
  if (typeof value === 'boolean') {
    return value;
  }

  if (typeof value === 'number') {
    return value !== 0;
  }

  if (typeof value === 'string') {
    const lowered = value.trim().toLowerCase();
    return lowered === '1' || lowered === 'true' || lowered === 'on';
  }

  return false;
}

function padNumber(value) {
  return String(value).padStart(2, '0');
}

function formatTime(input) {
  if (!input) {
    return '--';
  }

  let date = null;
  if (typeof input === 'number') {
    date = new Date(input > 9999999999 ? input : input * 1000);
  } else {
    const parsed = new Date(String(input).replace(/-/g, '/'));
    if (!Number.isNaN(parsed.getTime())) {
      date = parsed;
    }
  }

  if (!date || Number.isNaN(date.getTime())) {
    return String(input);
  }

  return `${padNumber(date.getMonth() + 1)}-${padNumber(date.getDate())} ${padNumber(date.getHours())}:${padNumber(date.getMinutes())}:${padNumber(date.getSeconds())}`;
}

function parseTimeMs(input) {
  if (!input) {
    return 0;
  }

  if (typeof input === 'number') {
    return input > 9999999999 ? input : input * 1000;
  }

  const parsed = new Date(String(input).replace(/-/g, '/'));
  if (!Number.isNaN(parsed.getTime())) {
    return parsed.getTime();
  }

  return 0;
}

function isDeviceOnline(updatedAtMs) {
  if (!updatedAtMs) {
    return false;
  }

  return (Date.now() - updatedAtMs) <= config.deviceOnlineTimeout;
}

function normalizeHistoryList(response) {
  const payload = getResponsePayload(response);
  const list = payload && Array.isArray(payload.list) ? payload.list : [];

  return list
    .map((item) => {
      const time = parseTimeMs(getItemTime(item));
      const value = normalizeNumber(getItemValue(item));

      if (!time || value === null) {
        return null;
      }

      return {
        time,
        value
      };
    })
    .filter(Boolean)
    .sort((left, right) => left.time - right.time);
}

function queryDeviceProperty(options = {}) {
  const query = `/thingmodel/query-device-property?product_id=${encodeURIComponent(config.productId)}&device_name=${encodeURIComponent(config.deviceName)}`;

  return request({
    url: query,
    timeout: options.timeout
  }).then((response) => {
    const { propertyMap, latestTime } = buildPropertyMap(response);

    const updatedAtMs = parseTimeMs(latestTime);

    return {
      temperature: normalizeNumber(propertyMap[config.propertyKeys.temperature]),
      humidity: normalizeNumber(propertyMap[config.propertyKeys.humidity]),
      smoke: normalizeNumber(propertyMap[config.propertyKeys.smoke]),
      pir: normalizeBoolean(propertyMap[config.propertyKeys.pir]),
      alarmOn: normalizeBoolean(propertyMap[config.propertyKeys.alarm]),
      updatedAtRaw: latestTime,
      updatedAtMs,
      updatedAtText: formatTime(latestTime),
      deviceOnline: isDeviceOnline(updatedAtMs),
      raw: propertyMap
    };
  });
}

function queryDevicePropertyHistory(identifier, options = {}) {
  const endTime = options.endTime || Date.now();
  const startTime = options.startTime || (endTime - 24 * 60 * 60 * 1000);
  const limit = options.limit || 30;
  const query = `/thingmodel/query-device-property-history?product_id=${encodeURIComponent(config.productId)}&device_name=${encodeURIComponent(config.deviceName)}&identifier=${encodeURIComponent(identifier)}&start_time=${encodeURIComponent(startTime)}&end_time=${encodeURIComponent(endTime)}&limit=${encodeURIComponent(limit)}`;

  return request({
    url: query,
    timeout: options.timeout
  }).then((response) => normalizeHistoryList(response));
}

function setAlarmState(enabled, options = {}) {
  const payload = {
    product_id: config.productId,
    device_name: config.deviceName,
    params: {
      [config.propertyKeys.alarm]: Boolean(enabled)
    }
  };

  return request({
    url: '/thingmodel/set-device-property',
    method: 'POST',
    data: payload,
    timeout: options.timeout || config.controlRequestTimeout
  });
}

module.exports = {
  config,
  queryDeviceProperty,
  queryDevicePropertyHistory,
  setAlarmState
};
