function toggleTheme() {
  const html = document.documentElement
  html.dataset.theme = html.dataset.theme === 'dark' ? 'light' : 'dark'
}

async function downloadFile(url, path) {
  try {
    const result = await window.api.download(url, path)
    console.log(result)
  } catch (err) {
    console.error(err)
  }
}


/*
document.addEventListener('DOMContentLoaded', () => {
  const searchInput = document.getElementById('search')
  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      const query = e.target.value.toLowerCase()
      document.querySelectorAll('.distro-card').forEach((card) => {
        const name = card.querySelector('.distro-card-name')?.textContent.toLowerCase() ?? ''
        card.style.display = name.includes(query) ? '' : 'none'
      })
    })
  }
})
*/




