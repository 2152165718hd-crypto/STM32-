const { config, queryDeviceProperty, setAlarmState } = require('../../utils/onenet');

const CONTROL_CONFIRM_INTERVAL_MS = 800;
const CONTROL_CONFIRM_MAX_ATTEMPTS = 5;
const CONTROL_TOTAL_TIMEOUT_MS = config.controlTotalTimeout || 6000;

function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function buildMetricCards(snapshot) {
  return [
    {
      key: 'temperature',
      title: '温度',
      icon: '../../image/温度.png',
      value: snapshot.temperature === null ? '--' : `${snapshot.temperature} °C`,
      theme: 'warm'
    },
    {
      key: 'humidity',
      title: '湿度',
      icon: '../../image/湿度.png',
      value: snapshot.humidity === null ? '--' : `${snapshot.humidity} %`,
      theme: 'cool'
    },
    {
      key: 'smoke',
      title: '烟雾',
      icon: '../../image/烟雾.png',
      value: snapshot.smoke === null ? '--' : `${snapshot.smoke} %`,
      theme: 'amber'
    },
    {
      key: 'pir',
      title: '人体红外',
      icon: '../../image/红外传感器.png',
      value: snapshot.pir ? '检测到' : '正常',
      theme: 'danger'
    }
  ];
}

Page({
  data: {
    topSafeHeight: 88,
    sensorCards: buildMetricCards({
      temperature: null,
      humidity: null,
      smoke: null,
      pir: false
    }),
    alarmOn: false,
    alarmStatusText: '--',
    updatedAtText: '--',
    statusText: '等待连接',
    online: false,
    loading: true,
    refreshing: false,
    controlLoading: false,
    pendingAlarmState: null,
    errorMessage: ''
  },

  onLoad() {
    const app = getApp();
    const systemInfo = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync();
    const fallbackTopSafeHeight = Math.max((systemInfo.statusBarHeight || 0) + 44, 88);

    this.setData({
      topSafeHeight: (app.globalData && app.globalData.navOffsetTop) || fallbackTopSafeHeight
    });

    this.refreshDashboard(true);
  },

  onShow() {
    this.startPolling();
    if (!this.data.loading) {
      this.refreshDashboard(false);
    }
  },

  onHide() {
    this.stopPolling();
  },

  onUnload() {
    this.stopPolling();
  },

  startPolling() {
    if (this.pollTimer) {
      return;
    }

    this.pollTimer = setInterval(() => {
      if (!this.data.controlLoading) {
        this.refreshDashboard(false);
      }
    }, config.pollInterval || 2000);
  },

  stopPolling() {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
  },

  applySnapshot(snapshot) {
    const hasPendingAlarmState = typeof this.data.pendingAlarmState === 'boolean';
    const pendingAlarmState = hasPendingAlarmState ? this.data.pendingAlarmState : null;
    const pendingAlarmMatched = hasPendingAlarmState && (snapshot.alarmOn === pendingAlarmState);
    const displayAlarmOn = (hasPendingAlarmState && !pendingAlarmMatched) ? pendingAlarmState : snapshot.alarmOn;

    this.setData({
      sensorCards: buildMetricCards(snapshot),
      alarmOn: displayAlarmOn,
      alarmStatusText: displayAlarmOn ? '开启' : '关闭',
      updatedAtText: snapshot.updatedAtText,
      statusText: snapshot.deviceOnline ? '单片机在线' : '单片机离线',
      online: snapshot.deviceOnline,
      loading: false,
      refreshing: false,
      pendingAlarmState: pendingAlarmMatched ? null : pendingAlarmState,
      errorMessage: ''
    });
  },

  confirmAlarmState(expectedState, attempt = 0) {
    return queryDeviceProperty({
      timeout: config.controlQueryTimeout || config.requestTimeout
    })
      .then((snapshot) => {
        this.applySnapshot(snapshot);

        if (snapshot.alarmOn === expectedState) {
          return snapshot;
        }

        if (attempt >= (CONTROL_CONFIRM_MAX_ATTEMPTS - 1)) {
          throw new Error('ALARM_STATE_NOT_CONFIRMED');
        }

        return delay(CONTROL_CONFIRM_INTERVAL_MS).then(() => this.confirmAlarmState(expectedState, attempt + 1));
      })
      .catch((error) => {
        if (attempt >= (CONTROL_CONFIRM_MAX_ATTEMPTS - 1)) {
          throw error;
        }

        return delay(CONTROL_CONFIRM_INTERVAL_MS).then(() => this.confirmAlarmState(expectedState, attempt + 1));
      });
  },

  waitAlarmControlResult(expectedState) {
    return new Promise((resolve, reject) => {
      let settled = false;
      let commandError = null;
      let confirmError = null;
      const timeoutId = setTimeout(() => {
        rejectOnce(new Error('CONTROL_TIMEOUT'));
      }, CONTROL_TOTAL_TIMEOUT_MS);

      const resolveOnce = (value) => {
        if (!settled) {
          settled = true;
          clearTimeout(timeoutId);
          resolve(value);
        }
      };

      const rejectOnce = (error) => {
        if (!settled) {
          settled = true;
          clearTimeout(timeoutId);
          reject(error);
        }
      };

      setAlarmState(expectedState, {
        timeout: config.controlRequestTimeout || config.requestTimeout
      })
        .then(() => {
          resolveOnce({ source: 'command' });
        })
        .catch((error) => {
          commandError = error;
          if (confirmError) {
            rejectOnce(commandError);
          }
        });

      this.confirmAlarmState(expectedState)
        .then((snapshot) => {
          resolveOnce(snapshot);
        })
        .catch((error) => {
          confirmError = error;
          if (commandError) {
            rejectOnce(commandError);
          }
        });
    });
  },

  handleManualRefresh() {
    this.refreshDashboard(false, true);
  },

  goToCurvePage() {
    wx.redirectTo({
      url: '/pages/curve/curve'
    });
  },

  onPullDownRefresh() {
    this.refreshDashboard(false, false, true);
  },

  refreshDashboard(showLoading, showToast, fromPullDown) {
    if (this.requesting) {
      if (fromPullDown) {
        wx.stopPullDownRefresh();
      }
      return;
    }

    this.requesting = true;
    this.setData({
      loading: showLoading ? true : this.data.loading,
      refreshing: showToast ? true : this.data.refreshing
    });

    queryDeviceProperty()
      .then((snapshot) => {
        this.applySnapshot(snapshot);

        if (showToast) {
          wx.showToast({
            title: '数据已刷新',
            icon: 'success'
          });
        }
      })
      .catch((error) => {
        this.setData({
          statusText: '云端连接异常',
          online: false,
          loading: false,
          refreshing: false,
          errorMessage: error.message || '数据加载失败'
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
  },

  onAlarmSwitchChange(event) {
    const nextState = Boolean(event.detail.value);
    const previousState = this.data.alarmOn;

    if (this.data.controlLoading) {
      return;
    }

    this.setData({
      alarmOn: nextState,
      alarmStatusText: nextState ? '开启' : '关闭',
      controlLoading: true,
      pendingAlarmState: nextState,
      errorMessage: ''
    });

    this.waitAlarmControlResult(nextState)
      .then(() => {
        wx.showToast({
          title: nextState ? '报警器已开启' : '报警器已关闭',
          icon: 'success'
        });

        setTimeout(() => {
          this.refreshDashboard(false);
        }, 1200);
      })
      .catch((error) => {
        this.setData({
          alarmOn: previousState,
          alarmStatusText: previousState ? '开启' : '关闭',
          pendingAlarmState: null,
          errorMessage: error.message || '控制失败'
        });

        wx.showToast({
          title: '控制失败',
          icon: 'none'
        });
      })
      .finally(() => {
        this.setData({
          controlLoading: false
        });
      });
  }
});
