// app.js
App({
  globalData: {
    statusBarHeight: 0,
    navOffsetTop: 88
  },

  onLaunch() {
    const systemInfo = wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync();
    const menuButton = wx.getMenuButtonBoundingClientRect ? wx.getMenuButtonBoundingClientRect() : null;
    const statusBarHeight = systemInfo.statusBarHeight || 0;

    let navOffsetTop = Math.max(statusBarHeight + 44, 88);
    if (menuButton && menuButton.bottom) {
      navOffsetTop = menuButton.bottom + 16;
    }

    this.globalData.statusBarHeight = statusBarHeight;
    this.globalData.navOffsetTop = navOffsetTop;
  }
})
