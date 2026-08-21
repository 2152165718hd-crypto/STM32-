const layout = require('../../utils/layout')
const onenet = require('../../utils/onenet')

Page({
  data: {
    safeTop: 64,
    loading: false,
    saving: false,
    error: '',
    fields: []
  },

  onLoad() {
    this.savedValues = {}
    this.setData({ safeTop: layout.getPageTopPadding() })
    this.resetFields()
    this.loadLatest()
  },

  resetFields() {
    const fields = onenet.THRESHOLD_DEFS.map((item) => ({
      ...item,
      value: item.defaultValue
    }))
    this.savedValues = this.fieldsToValueMap(fields)
    this.setData({ fields })
  },

  fieldsToValueMap(fields) {
    const values = {}
    fields.forEach((item) => {
      values[item.id] = onenet.clampNumber(item.value, item.min, item.max, item.defaultValue)
    })
    return values
  },

  loadLatest() {
    if (this.loadingLatest) {
      return
    }

    this.loadingLatest = true
    this.setData({ loading: true, error: '' })
    onenet.queryLatestProperties()
      .then((res) => {
        const map = onenet.normalizeLatestProperties(res)
        const fields = this.data.fields.map((item) => {
          const value = (map[item.id] || {}).value
          return {
            ...item,
            value: onenet.isEmptyValue(value) ? item.value : onenet.toNumber(value, item.value)
          }
        })
        this.savedValues = this.fieldsToValueMap(fields)
        this.setData({ fields, error: '' })
      })
      .catch((err) => {
        this.setData({ error: onenet.normalizeError(err) })
      })
      .then(() => {
        this.loadingLatest = false
        this.setData({ loading: false })
      })
  },

  onSliderChange(event) {
    this.updateField(event.currentTarget.dataset.id, Number(event.detail.value))
  },

  onInputChange(event) {
    this.updateField(event.currentTarget.dataset.id, Number(event.detail.value))
  },

  updateField(id, rawValue) {
    const fields = this.data.fields.map((item) => {
      if (item.id !== id) {
        return item
      }
      const value = onenet.clampNumber(rawValue, item.min, item.max, item.defaultValue)
      return {
        ...item,
        value
      }
    })
    this.setData({ fields })
  },

  saveThresholds() {
    if (this.data.saving) {
      return
    }

    const params = {}
    const nextValues = {}
    this.data.fields.forEach((item) => {
      const value = onenet.clampNumber(item.value, item.min, item.max, item.defaultValue)
      nextValues[item.id] = value
      if (!this.savedValues || this.savedValues[item.id] !== value) {
        params[item.id] = value
      }
    })

    if (Object.keys(params).length === 0) {
      wx.showToast({ title: '阈值未变化', icon: 'none' })
      return
    }

    this.setData({ saving: true, error: '' })
    onenet.setDeviceProperties(params)
      .then(() => {
        this.savedValues = nextValues
        wx.showToast({ title: '已下发', icon: 'success' })
      })
      .catch((err) => {
        this.setData({ error: onenet.normalizeError(err) })
      })
      .then(() => {
        this.setData({ saving: false })
      })
  },

  goBack() {
    wx.navigateBack()
  }
})
