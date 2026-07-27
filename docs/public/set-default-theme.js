try {
  const storageKey = 'starlight-theme';
  if (localStorage.getItem(storageKey) === null) {
    localStorage.setItem(storageKey, 'dark');
    document.documentElement.dataset.theme = 'dark';
  }
} catch {
  document.documentElement.dataset.theme = 'dark';
}
