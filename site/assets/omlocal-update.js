(() => {
  if (location.protocol !== "http:" || !["localhost", "127.0.0.1", "[::1]"].includes(location.hostname)) return;

  const updateTopKey = "omlocal-update:top-on-reload";
  try {
    if (sessionStorage.getItem(updateTopKey) === location.href) {
      sessionStorage.removeItem(updateTopKey);
      history.scrollRestoration = "manual";
      const goToTop = () => window.scrollTo(0, 0);
      addEventListener("pageshow", goToTop, { once: true });
      requestAnimationFrame(goToTop);
    }
  } catch {
    // Storage may be unavailable in restricted browser contexts.
  }

  const button = document.createElement("button");
  const normalize = html => html.replace(/\s+/g, " ").trim();
  const pageMarkup = doc => normalize(doc.head.innerHTML + doc.body.innerHTML);
  const trackedResourceUrls = (doc, baseUrl) => [...doc.querySelectorAll(
    "img[src], source[src], video[poster], audio[src], script[src], object[data], link[href]"
  )].map(element => {
    const attribute = element.hasAttribute("src") ? "src" : element.hasAttribute("poster") ? "poster" : element.hasAttribute("data") ? "data" : "href";
    try {
      const url = new URL(element.getAttribute(attribute), baseUrl);
      if (url.origin !== location.origin) return null;
      url.hash = "";
      return url.href;
    } catch {
      return null;
    }
  }).filter(Boolean);
  const fingerprint = async response => {
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (globalThis.crypto?.subtle) {
      const digest = new Uint8Array(await globalThis.crypto.subtle.digest("SHA-256", bytes));
      return [...digest].map(byte => byte.toString(16).padStart(2, "0")).join("");
    }
    let hash = 2166136261;
    for (const byte of bytes) hash = Math.imul(hash ^ byte, 16777619);
    return `${bytes.length}:${hash >>> 0}`;
  };
  const pageSnapshot = async (doc, baseUrl) => {
    const markup = pageMarkup(doc);
    const urls = [...new Set(trackedResourceUrls(doc, baseUrl))].sort();
    const resources = await Promise.all(urls.map(async url => {
      try {
        const response = await fetch(url, { cache: "no-store", credentials: "same-origin" });
        return `${url}\t${response.status}\t${response.ok ? await fingerprint(response) : ""}`;
      } catch {
        return `${url}\tunavailable`;
      }
    }));
    return JSON.stringify([markup, resources]);
  };
  const initialSnapshot = pageSnapshot(document, location.href);
  button.type = "button";
  button.textContent = "↻ Update";
  button.setAttribute("aria-label", "Update page");
  Object.assign(button.style, {
    position: "fixed", top: "1rem", right: "1rem", zIndex: "9999",
    padding: ".5rem .75rem", border: "1px solid #76c4c2", borderRadius: ".55rem",
    background: "#172326", color: "#d9fffb", font: "600  .95rem system-ui, sans-serif",
    boxShadow: "0 4px 24px #0008", cursor: "pointer"
  });
  button.hidden = true;
  button.addEventListener("click", () => {
    try {
      sessionStorage.setItem(updateTopKey, location.href);
      history.scrollRestoration = "manual";
    } catch {
      // Reload still works if the browser blocks session storage.
    }
    window.scrollTo(0, 0);
    location.reload();
  });
  document.body.append(button);

  let checking = false;

  async function checkForUpdate() {
    if (checking || document.hidden) return;
    checking = true;
    try {
      const response = await fetch(location.href, { cache: "no-store", credentials: "same-origin" });
      if (!response.ok) return;
      const doc = new DOMParser().parseFromString(await response.text(), "text/html");
      const [initial, latest] = await Promise.all([initialSnapshot, pageSnapshot(doc, response.url || location.href)]);
      button.hidden = latest === initial;
    } catch {
      // A temporarily unavailable local server is not evidence of an update.
    } finally {
      checking = false;
    }
  }

  checkForUpdate();
  window.setInterval(checkForUpdate, 5000);
  document.addEventListener("visibilitychange", checkForUpdate);
})();
