(() => {
  const root=document.documentElement;
  let saved; try {saved=localStorage.getItem('cyd-theme');} catch (_) {}
  root.dataset.theme=saved==='light'||saved==='dark'?saved:'dark';
  const button=document.getElementById('theme');
  const refresh=()=>{button.textContent=root.dataset.theme==='light'?'☾ Dark mode':'☀ Light mode';button.setAttribute('aria-label','Switch to '+(root.dataset.theme==='light'?'dark':'light')+' mode');};
  button.addEventListener('click',()=>{root.dataset.theme=root.dataset.theme==='light'?'dark':'light';try{localStorage.setItem('cyd-theme',root.dataset.theme);}catch(_){}refresh();});refresh();
})();
