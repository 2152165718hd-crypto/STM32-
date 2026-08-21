// index.js

// OneNET 平台配置 (OneNET Studio API)
const PRODUCT_ID = "1d2Ohi093C";
const DEVICE_NAME = "don1ng";
const TOKEN = "version=2018-10-31&res=products%2F1d2Ohi093C%2Fdevices%2Fdon1ng&et=1798736400&method=md5&sign=9YxVMH7qYa9IOiXVo16LQA%3D%3D";

// 接口地址
const API_URL = "https://iot-api.heclouds.com";
const POLL_INTERVAL_MS = 2000;
const ONLINE_STALE_THRESHOLD_MS = 7000;

Page({
  data: {
    deviceOnline: false,
    lastUpdateTime: "--",
    faceCount: 0,
    fingerprintCount: 0,
    rfidCount: 0,
    password: "",
    servoStatus: false,
    
    showPassword: false,         // 控制密码密文/明文开关
    showPasswordModal: false,    // 控制密码修改弹窗
    newPassword: "",             // 弹窗中输入的密码
  },

  timer: null,
  isUnloaded: false,
  isPullDown: false,
  cloudStatusOnline: false,
  lastTelemetryTimestamp: 0,

  onLoad() {
    this.isUnloaded = false;
    this.cloudStatusOnline = false;
    this.lastTelemetryTimestamp = 0;
    this.pollLoop();
  },

  onShow() {
    // 页面重新显示时，立刻抓取一次
    if (this.timer && !this.isUnloaded) {
      if (this.timer) clearTimeout(this.timer);
      this.pollLoop();
    }
  },

  onUnload() {
    this.isUnloaded = true;
    if (this.timer) clearTimeout(this.timer);
  },

  onPullDownRefresh() {
    this.isPullDown = true;
    if (this.timer) clearTimeout(this.timer);
    this.pollLoop();
  },

  stopPullDownIfNeeded() {
    if (this.isPullDown) {
      wx.stopPullDownRefresh();
      this.isPullDown = false;
      wx.showToast({ title: '刷新成功', icon: 'none' });
    }
  },

  pollLoop() {
    if (this.isUnloaded) return;
    this.fetchDeviceStatus();
    this.fetchDeviceData();
  },

  scheduleNextPoll(delay) {
    if (this.isUnloaded) return;
    if (this.timer) clearTimeout(this.timer);
    this.timer = setTimeout(() => {
      this.pollLoop();
    }, delay);
  },

  normalizeTimestamp(value) {
    const timestamp = Number(value);
    return Number.isFinite(timestamp) ? timestamp : 0;
  },

  getDeviceOnlineState(telemetryTimestamp = this.lastTelemetryTimestamp) {
    if (telemetryTimestamp > 0) {
      const age = Math.max(0, Date.now() - telemetryTimestamp);
      return age <= ONLINE_STALE_THRESHOLD_MS;
    }
    return this.cloudStatusOnline;
  },

  syncDeviceOnlineState() {
    this.setData({
      deviceOnline: this.getDeviceOnlineState()
    });
  },

  // 1. 获取设备在线/离线状态
  fetchDeviceStatus() {
    wx.request({
      url: `${API_URL}/device/detail`,
      method: "GET",
      header: {
        'Authorization': TOKEN
      },
      data: {
        product_id: PRODUCT_ID,
        device_name: DEVICE_NAME
      },
      success: (res) => {
        if (res.data && res.data.code === 0 && res.data.data) {
          // status: 1 在线, 2 离线
          this.cloudStatusOnline = res.data.data.status === 1;
          this.syncDeviceOnlineState();
        }
      },
      fail: (err) => {
        console.error("fetchDeviceStatus error", err);
        this.syncDeviceOnlineState();
      }
    });
  },

  // 2. 获取设备物模型属性值
  fetchDeviceData() {
    wx.request({
      url: `${API_URL}/thingmodel/query-device-property`,
      method: "GET",
      header: {
        'Authorization': TOKEN
      },
      data: {
        product_id: PRODUCT_ID,
        device_name: DEVICE_NAME
      },
      success: (res) => {
        if (res.data && res.data.code === 0 && res.data.data) {
          let latestTime = 0;
          let newFaceCount = this.data.faceCount;
          let newFingerprintCount = this.data.fingerprintCount;
          let newRfidCount = this.data.rfidCount;
          let newPassword = this.data.password;
          let newServo = this.data.servoStatus;

          // 遍历解析返回的物模型属性
          res.data.data.forEach(item => {
            const itemTimestamp = this.normalizeTimestamp(item.time);
            if (itemTimestamp > latestTime) {
              latestTime = itemTimestamp;
            }
            
            switch(item.identifier) {
              case "Face_count":
                newFaceCount = item.value;
                break;
              case "Fingerprint_count":
                newFingerprintCount = item.value;
                break;
              case "RFID_count":
                newRfidCount = item.value;
                break;
              case "Password":
                newPassword = item.value;
                break;
              case "Servo":
                newServo = (item.value === "true" || item.value === "1");
                break;
            }
          });

          if (latestTime > 0) {
            this.lastTelemetryTimestamp = latestTime;
          }

          // 格式化时间戳
          let timeStr = this.data.lastUpdateTime;
          if (latestTime > 0) {
            timeStr = this.formatTime(latestTime);
          }
          this.setData({
            faceCount: newFaceCount,
            fingerprintCount: newFingerprintCount,
            rfidCount: newRfidCount,
            password: newPassword,
            servoStatus: newServo,
            deviceOnline: this.getDeviceOnlineState(latestTime > 0 ? latestTime : this.lastTelemetryTimestamp),
            lastUpdateTime: timeStr
          }, () => {
            this.stopPullDownIfNeeded();
            this.scheduleNextPoll(POLL_INTERVAL_MS);
          });
        } else {
          this.setData({
            deviceOnline: this.getDeviceOnlineState()
          }, () => {
            this.stopPullDownIfNeeded();
            this.scheduleNextPoll(POLL_INTERVAL_MS);
          });
        }
      },
      fail: (err) => {
        console.error("fetchDeviceData error", err);
        this.setData({
          deviceOnline: this.getDeviceOnlineState()
        }, () => {
          this.stopPullDownIfNeeded();
          this.scheduleNextPoll(POLL_INTERVAL_MS);
        });
      }
    });
  },

  // === 交互逻辑 ===

  // 切换查看/隐藏密码
  togglePasswordHint() {
    this.setData({
      showPassword: !this.data.showPassword
    });
  },

  // 远程开门
  openDoor() {
    if (!this.data.deviceOnline) {
      wx.showToast({ title: '设备离线，无法操作', icon: 'none' });
      return;
    }
    wx.showLoading({ title: '开门中...' });
    
    // 下发 Servo = true
    this.sendControlCommand({ "Servo": true }, () => {
      wx.hideLoading();
      wx.showToast({ title: '指令已下发', icon: 'success' });
      if (this.timer) clearTimeout(this.timer);
      this.pollLoop();
    }, () => {
      wx.hideLoading();
      wx.showToast({ title: '开门失败', icon: 'none' });
    });
  },

  // 点击修改密码按钮 -> 显示弹窗
  changePassword() {
    if (!this.data.deviceOnline) {
      wx.showToast({ title: '设备离线，无法操作', icon: 'none' });
      return;
    }
    this.setData({
      showPasswordModal: true,
      newPassword: ""
    });
  },

  // 弹窗输入绑定
  onPasswordInput(e) {
    this.setData({
      newPassword: e.detail.value
    });
  },

  closePasswordModal() {
    this.setData({
      showPasswordModal: false,
      newPassword: ""
    });
  },

  // 提交新密码
  submitNewPassword() {
    const pwd = this.data.newPassword;
    if (!pwd || pwd.length === 0) {
      wx.showToast({ title: '密码不能为空', icon: 'none' });
      return;
    }
    
    wx.showLoading({ title: '设置中...' });
    
    // 下发 Password
    this.sendControlCommand({ "Password": pwd }, () => {
      wx.hideLoading();
      this.setData({ showPasswordModal: false });
      wx.showToast({ title: '修改成功', icon: 'success' });
      
      // 更新本地状态，以便立马见效
      this.setData({ password: pwd });
      if (this.timer) clearTimeout(this.timer);
      this.pollLoop();
    }, () => {
      wx.hideLoading();
      wx.showToast({ title: '修改失败', icon: 'none' });
    });
  },

  // API 封装：属性下发
  sendControlCommand(params, successCb, failCb) {
    wx.request({
      url: `${API_URL}/thingmodel/set-device-property`,
      method: "POST",
      header: {
        'Authorization': TOKEN,
        'Content-Type': 'application/json'
      },
      data: {
        product_id: PRODUCT_ID,
        device_name: DEVICE_NAME,
        params: params
      },
      success: (res) => {
        if (res.data && res.data.code === 0) {
          if (successCb) successCb();
        } else {
          console.error("set-device-property code not 0:", res.data);
          if (failCb) failCb();
        }
      },
      fail: (err) => {
        console.error("sendControlCommand error", err);
        if (failCb) failCb();
      }
    });
  },

  // 工具函数：格式化时间戳
  formatTime(timestamp) {
    const date = new Date(timestamp);
    const YY = date.getFullYear();
    const MM = (date.getMonth() + 1).toString().padStart(2, '0');
    const DD = date.getDate().toString().padStart(2, '0');
    const hh = date.getHours().toString().padStart(2, '0');
    const mm = date.getMinutes().toString().padStart(2, '0');
    const ss = date.getSeconds().toString().padStart(2, '0');
    return `${YY}-${MM}-${DD} ${hh}:${mm}:${ss}`;
  }
});
