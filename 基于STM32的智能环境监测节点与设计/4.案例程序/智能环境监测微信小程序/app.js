function calcLayout() {
  let statusBarHeight = 0
  let navBarHeight = 44

  try {
    const systemInfo = wx.getSystemInfoSync ? wx.getSystemInfoSync() : {}
    const menuButton = wx.getMenuButtonBoundingClientRect ? wx.getMenuButtonBoundingClientRect() : null
    statusBarHeight = systemInfo.statusBarHeight || 0

    if (menuButton && menuButton.top && menuButton.height) {
      navBarHeight = menuButton.height + (menuButton.top - statusBarHeight) * 2
    }
  } catch (err) {
    statusBarHeight = 20
    navBarHeight = 44
  }

  return {
    statusBarHeight,
    navBarHeight,
    contentTop: statusBarHeight + navBarHeight + 8
  }
}

App({
  onLaunch() {
    Object.assign(this.globalData, calcLayout())
  },

  globalData: {
    appName: '智能环境监测',
    statusBarHeight: 0,
    navBarHeight: 44,
    contentTop: 64
  }
})
