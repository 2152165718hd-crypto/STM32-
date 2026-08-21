function getPageTopPadding(extra = 0) {
  const app = getApp()
  const contentTop = app && app.globalData ? app.globalData.contentTop : 64
  return Math.max(44, contentTop + extra)
}

module.exports = {
  getPageTopPadding
}
