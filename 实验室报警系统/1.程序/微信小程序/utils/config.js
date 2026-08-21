const onenetConfig = {
  baseUrl: 'https://iot-api.heclouds.com',
  productId: '3nJMD1yIKI',
  deviceName: 'don1ng',
  authorization: 'version=2018-10-31&res=products%2F3nJMD1yIKI%2Fdevices%2Fdon1ng&et=1861891200&method=md5&sign=g4bHKRMmlJzyls%2BP5qFgRw%3D%3D',
  pollInterval: 1000,
  pollIntervalMin: 1000,
  pollIntervalMax: 10000,
  deviceOnlineTimeout: 20000,
  requestTimeout: 10000,
  controlRequestTimeout: 4000,
  controlQueryTimeout: 2500,
  controlTotalTimeout: 6000,
  propertyKeys: {
    temperature: 'Temp',
    humidity: 'Hum',
    smoke: 'Smoke',
    flame: 'Flame',
    alarm: 'AV_ALM'
  }
};

module.exports = onenetConfig;
