const { app, BrowserWindow, ipcMain } = require('electron')
const path = require('node:path')
const request = require('request')
const fs = require('fs')

const createWindow = () => {
  const win = new BrowserWindow({
    width: 1920,
    height: 1080,
    webPreferences: {
      preload: path.join(__dirname, '../renderer/preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  win.loadFile(path.join(__dirname, '../../index.html'))
}


app.whenReady().then(() => {
  ipcMain.handle('download', async (_event, file_url, targetPath) => {
    return new Promise((resolve, reject) => {
      const req = request(file_url)
      const out = fs.createWriteStream(targetPath)

      req.pipe(out)

      req.on('end', () => {
        resolve("Download terminé")
      })

      req.on('error', (err) => {
        reject(err)
      })
    })
  })

  createWindow()
})
