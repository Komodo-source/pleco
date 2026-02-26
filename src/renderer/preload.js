const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('api', {
  download: (url, path) => ipcRenderer.invoke('download', url, path)
})
