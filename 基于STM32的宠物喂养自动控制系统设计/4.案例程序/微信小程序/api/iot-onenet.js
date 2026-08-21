const ONENET_CONFIG = {
  baseUrl: 'https://iot-api.heclouds.com',
  propertyPath: '/thingmodel/query-device-property',
  deviceDetailPath: '/device/detail',
  productId: '55Mj71qz0o',
  deviceName: 'don1ng',
  token: 'version=2018-10-31&res=products%2F55Mj71qz0o%2Fdevices%2Fdon1ng&et=1830272400&method=md5&sign=CxDLmGSKEPN5c7iypwgyaA%3D%3D',
  timeout: 8000
};

const PROPERTY_META = {
  pet_near: {
    name: '宠物靠近状态',
    unit: '',
    imageNear: '../../image/宠物在范围内.png',
    imageAway: '../../image/宠物不在范围内.png'
  },
  temperature: {
    name: '环境温度',
    unit: '°C',
    image: '../../image/温度.png',
    min: 0,
    max: 100,
    precision: 0
  },
  humidity: {
    name: '环境湿度',
    unit: '%RH',
    image: '../../image/湿度.png',
    min: 0,
    max: 100,
    precision: 0
  },
  air_quality: {
    name: '空气质量浓度',
    unit: '%',
    image: '../../image/空气质量.png',
    min: 0,
    max: 100,
    precision: 0
  },
  food_weight: {
    name: '饲料剩余重量',
    unit: 'g',
    image: '../../image/重量.png',
    min: 0,
    max: 10000,
    precision: 1
  },
  water_level: {
    name: '水位比例',
    unit: '%',
    image: '../../image/水位传感器.png',
    min: 0,
    max: 100,
    precision: 0
  },
  illuminance: {
    name: '光照强度',
    unit: 'Lux',
    image: '../../image/光照强度.png',
    min: 0,
    max: 100,
    precision: 0
  },
  pet_distance: {
    name: '宠物距离',
    unit: 'cm',
    image: '../../image/距离.png',
    min: 0,
    max: 1000,
    precision: 1
  }
};

function getLatestPropertyUrl() {
  return `${ONENET_CONFIG.baseUrl}${ONENET_CONFIG.propertyPath}`;
}

function getDeviceDetailUrl() {
  return `${ONENET_CONFIG.baseUrl}${ONENET_CONFIG.deviceDetailPath}`;
}

function normalizePropertyList(payload) {
  if (!payload) {
    return [];
  }

  if (Array.isArray(payload.data)) {
    return payload.data;
  }

  if (payload.data && Array.isArray(payload.data.list)) {
    return payload.data.list;
  }

  return [];
}

function createRequestError(message, detail) {
  const error = new Error(message);
  if (detail) {
    error.detail = detail;
  }
  return error;
}

function queryLatestProperties() {
  return new Promise((resolve, reject) => {
    wx.request({
      url: getLatestPropertyUrl(),
      method: 'GET',
      timeout: ONENET_CONFIG.timeout,
      data: {
        product_id: ONENET_CONFIG.productId,
        device_name: ONENET_CONFIG.deviceName
      },
      header: {
        Authorization: ONENET_CONFIG.token,
        'content-type': 'application/json'
      },
      success(res) {
        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(createRequestError(`OneNET HTTP ${res.statusCode}`, res.data));
          return;
        }

        const payload = res.data || {};
        if (payload.code !== 0) {
          reject(createRequestError(payload.msg || 'OneNET 返回业务错误', payload));
          return;
        }

        resolve({
          code: payload.code,
          msg: payload.msg,
          requestId: payload.request_id,
          list: normalizePropertyList(payload)
        });
      },
      fail(err) {
        reject(createRequestError(err.errMsg || 'OneNET 请求失败', err));
      }
    });
  });
}

function queryDeviceDetail() {
  return new Promise((resolve, reject) => {
    wx.request({
      url: getDeviceDetailUrl(),
      method: 'GET',
      timeout: ONENET_CONFIG.timeout,
      data: {
        product_id: ONENET_CONFIG.productId,
        device_name: ONENET_CONFIG.deviceName
      },
      header: {
        Authorization: ONENET_CONFIG.token,
        'content-type': 'application/json'
      },
      success(res) {
        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(createRequestError(`OneNET HTTP ${res.statusCode}`, res.data));
          return;
        }

        const payload = res.data || {};
        if (payload.code !== 0) {
          reject(createRequestError(payload.msg || 'OneNET 返回业务错误', payload));
          return;
        }

        resolve({
          code: payload.code,
          msg: payload.msg,
          requestId: payload.request_id,
          data: payload.data || {}
        });
      },
      fail(err) {
        reject(createRequestError(err.errMsg || 'OneNET 请求失败', err));
      }
    });
  });
}

module.exports = {
  ONENET_CONFIG,
  PROPERTY_META,
  normalizePropertyList,
  queryDeviceDetail,
  queryLatestProperties
};
