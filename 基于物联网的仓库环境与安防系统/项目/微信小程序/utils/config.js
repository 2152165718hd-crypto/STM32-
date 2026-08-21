const onenetConfig = {
  baseUrl: 'https://iot-api.heclouds.com',
  productId: 'FU4VqqGAT7',
  deviceName: 'don1ng',
  authorization: 'version=2018-10-31&res=products%2FFU4VqqGAT7%2Fdevices%2Fdon1ng&et=1798736400&method=md5&sign=eMPJnRGGARuAqEBeifsuCw%3D%3D',
  pollInterval: 2000,
  deviceOnlineTimeout: 10000,
  requestTimeout: 10000,
  controlRequestTimeout: 4000,
  controlQueryTimeout: 2500,
  controlTotalTimeout: 6000,
  propertyKeys: {
    temperature: 'Temp',
    humidity: 'Hum',
    smoke: 'Smoke',
    pir: 'PIR',
    alarm: 'AV_ALM'
  }
};

module.exports = onenetConfig;
