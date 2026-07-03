/* ============================================================================
   Raboseta Cam Studio - Front-end Logic
   ============================================================================ */

// 1. UI Elements Mapping
const views = {
    auth: document.getElementById('auth-view'),
    gallery: document.getElementById('gallery-view'),
    editor: document.getElementById('editor-view')
};

// Auth
const authPassword = document.getElementById('auth-password');
const btnAuthSubmit = document.getElementById('btn-auth-submit');
const authTitle = document.getElementById('auth-title');
const authDesc = document.getElementById('auth-desc');
const authError = document.getElementById('auth-error');

// Gallery
const photoCarousel = document.getElementById('photo-carousel');
const carouselDots = document.getElementById('carousel-dots');
const btnRefresh = document.getElementById('btn-refresh');
const btnPrev = document.getElementById('btn-prev');
const btnNext = document.getElementById('btn-next');

// Editor Page
const btnBack = document.getElementById('btn-back');
const canvas = document.getElementById('photo-canvas');
const ctx = canvas.getContext('2d');
const sourceImage = document.getElementById('source-image');
const photoDetails = document.getElementById('photo-details');

// Adjustments
const sliders = {
    brightness: document.getElementById('filter-brightness'),
    contrast: document.getElementById('filter-contrast'),
    saturation: document.getElementById('filter-saturation')
};

const sliderVals = {
    brightness: document.getElementById('val-brightness'),
    contrast: document.getElementById('val-contrast'),
    saturation: document.getElementById('val-saturation')
};

// Controls selectors
const presetBtns = document.querySelectorAll('.preset-card');
const frameBtns = document.querySelectorAll('.frame-btn');
const btnDownload = document.getElementById('btn-download');
const btnShare = document.getElementById('btn-share');
const btnReset = document.getElementById('btn-reset');

// Status Bar
const statusIndicator = document.getElementById('status-indicator');

// State variables
let isSetupMode = false;
let currentPhotoName = "";
let currentPreset = "natural";
let currentFrame = "none";
let isOfflineMode = false;
let activePhotoIndex = 0;
let photosList = [];

// Filter configurations for presets (additive offsets/effects)
const presetConfig = {
    natural:  { b: 0,   c: 0,   s: 0,   tint: null },
    warm:     { b: 5,   c: 5,   s: 20,  tint: 'rgba(255, 125, 0, 0.06)' },
    bw:       { b: 0,   c: 25,  s: -100, tint: null },
    softea:   { b: 12,  c: -15, s: -5,   tint: 'rgba(255, 200, 0, 0.05)' },
    kc100:    { b: 8,   c: 30,  s: 25,  tint: 'rgba(255, 0, 100, 0.02)' },
    vintage:  { b: 15,  c: -10, s: -20,  tint: 'rgba(139, 90, 43, 0.12)' }
};

// ============================================================================
// VIEW NAVIGATION
// ============================================================================
function switchView(viewName) {
    Object.keys(views).forEach(name => {
        views[name].classList.toggle('active', name === viewName);
    });
}

// ============================================================================
// SYSTEM & AUTHENTICATION API
// ============================================================================
async function checkAuth() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error("API responded with error code");
        const status = await res.json();
        
        isOfflineMode = false;
        updateStatusIndicator(true, "ON");
        
        if (!status.pwd_set) {
            isSetupMode = true;
            authTitle.textContent = "Configura Raboseta Cam";
            authDesc.textContent = "Crea una contraseña para proteger las fotos de tu cámara.";
            btnAuthSubmit.textContent = "Configurar";
            switchView('auth');
        } else if (!status.auth) {
            isSetupMode = false;
            authTitle.textContent = "Acceso Protegido";
            authDesc.textContent = "Introduce la contraseña de tu Raboseta Cam para entrar.";
            btnAuthSubmit.textContent = "Entrar";
            switchView('auth');
        } else {
            switchView('gallery');
            fetchPhotoList();
        }
    } catch (e) {
        console.error("Auth check failed, loading offline demonstration mode:", e);
        showOfflineDemoMode();
    }
}

function updateStatusIndicator(connected, text) {
    if (!statusIndicator) return;
    const dot = statusIndicator.querySelector('.dot');
    const label = statusIndicator.querySelector('.status-text');
    
    if (connected) {
        dot.className = "dot connected";
        label.textContent = text || "ON";
    } else {
        dot.className = "dot disconnected";
        label.textContent = text || "DEMO";
    }
}

function showOfflineDemoMode() {
    isOfflineMode = true;
    updateStatusIndicator(false, "DEMO");
    switchView('gallery');
    
    // Premium Mock Photos from Unsplash
    const mockFiles = [
        { 
            name: "FOTO_20260703_142055.jpg", 
            size: 215000, 
            date: "2026-07-03 14:20:55",
            url: "https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=1000&auto=format&fit=crop&q=85"
        },
        { 
            name: "FOTO_20260702_183210.jpg", 
            size: 184500, 
            date: "2026-07-02 18:32:10",
            url: "https://images.unsplash.com/photo-1472396961693-142e6e269027?w=1000&auto=format&fit=crop&q=85"
        },
        { 
            name: "FOTO_20260702_091104.jpg", 
            size: 198200, 
            date: "2026-07-02 09:11:04",
            url: "https://images.unsplash.com/photo-1470071459604-3b5ec3a7fe05?w=1000&auto=format&fit=crop&q=85"
        },
        { 
            name: "FOTO_20260701_164500.jpg", 
            size: 245000, 
            date: "2026-07-01 16:45:00",
            url: "https://images.unsplash.com/photo-1447752875215-b2761acb3c5d?w=1000&auto=format&fit=crop&q=85"
        }
    ];
    
    renderPhotoCarousel(mockFiles);
}

btnAuthSubmit.addEventListener('click', async () => {
    const pwd = authPassword.value;
    if (!pwd) return;
    
    authError.style.display = 'none';
    btnAuthSubmit.disabled = true;
    const endpoint = isSetupMode ? '/api/setup' : '/api/login';
    
    try {
        const res = await fetch(endpoint, {
            method: 'POST',
            body: pwd
        });
        
        if (res.ok) {
            authPassword.value = '';
            checkAuth(); // refresh auth status
        } else {
            authError.style.display = 'block';
            authError.textContent = isSetupMode ? "Error al configurar contraseña" : "Contraseña incorrecta";
        }
    } catch (e) {
        authError.style.display = 'block';
        authError.textContent = "Error de conexión";
    }
    btnAuthSubmit.disabled = false;
});

// ============================================================================
// GALLERY LOGIC
// ============================================================================
async function fetchPhotoList() {
    try {
        photoCarousel.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">↻</div>
                <p>Cargando lista de fotos...</p>
            </div>
        `;
        
        const response = await fetch('/list?t=' + Date.now());
        if (!response.ok) throw new Error("Error fetching photos");
        const files = await response.json();
        
        renderPhotoCarousel(files);
    } catch (error) {
        console.error("Error reading file list:", error);
        photoCarousel.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">⚠️</div>
                <p>Error de conexión con la SD: ${error.message}</p>
            </div>
        `;
    }
}

function renderPhotoCarousel(files) {
    if (!files || files.length === 0) {
        photoCarousel.innerHTML = `
            <div class="empty-state">
                <div class="empty-icon">📭</div>
                <p>No se encontraron fotos en la tarjeta SD.</p>
            </div>
        `;
        carouselDots.innerHTML = '';
        return;
    }

    // Sort newer photos first
    photosList = files.sort((a, b) => b.name.localeCompare(a.name));
    photoCarousel.innerHTML = '';
    carouselDots.innerHTML = '';
    activePhotoIndex = 0;

    photosList.forEach((f, index) => {
        const card = document.createElement('div');
        card.className = 'photo-card';
        card.dataset.index = index;
        
        const photoUrl = f.url ? f.url : `/photo?name=${encodeURIComponent(f.name)}&t=${Date.now()}`;
        const sizeStr = f.size > 1024*1024 ? 
            (f.size / (1024*1024)).toFixed(1) + ' MB' : 
            (f.size / 1024).toFixed(0) + ' KB';

        card.innerHTML = `
            <div class="photo-thumb-container">
                <img data-src="${photoUrl}" alt="${f.name}">
            </div>
            <div class="photo-info">
                <div class="name">${f.name}</div>
                <div class="meta">
                    <span>${sizeStr}</span>
                    <span>${f.date.split(' ')[0]}</span>
                </div>
            </div>
        `;

        card.addEventListener('click', () => {
            if (index === activePhotoIndex) {
                openEditor(f.name, photoUrl, sizeStr);
            } else {
                activePhotoIndex = index;
                updateCarousel();
            }
        });
        
        photoCarousel.appendChild(card);
        
        // Create dot indicator
        const dot = document.createElement('div');
        dot.className = 'dot-indicator' + (index === 0 ? ' active' : '');
        dot.addEventListener('click', () => {
            activePhotoIndex = index;
            updateCarousel();
        });
        carouselDots.appendChild(dot);
    });
    
    updateCarousel();
}

function updateCarousel() {
    const cards = photoCarousel.querySelectorAll('.photo-card');
    const dots = carouselDots.querySelectorAll('.dot-indicator');
    if (cards.length === 0) return;

    cards.forEach((card, idx) => {
        card.className = 'photo-card';
        
        let diff = idx - activePhotoIndex;
        const total = cards.length;

        if (diff < -Math.floor(total / 2)) {
            diff += total;
        } else if (diff > Math.floor(total / 2)) {
            diff -= total;
        }

        let isVisible = false;
        if (diff === 0) {
            card.classList.add('active');
            isVisible = true;
        } else if (diff === -1) {
            card.classList.add('left');
            isVisible = true;
        } else if (diff === 1) {
            card.classList.add('right');
            isVisible = true;
        } else if (diff < -1) {
            card.classList.add('hidden-left');
        } else if (diff > 1) {
            card.classList.add('hidden-right');
        }
        
        // Dynamic lazy loading to prevent overloading ESP32 sockets
        if (isVisible) {
            const img = card.querySelector('img');
            if (img && img.dataset.src && img.getAttribute('src') !== img.dataset.src) {
                img.onload = () => console.log("OK", img.src);
                img.onerror = () => console.log("ERROR", img.src);
                img.src = img.dataset.src;
            }
        }
    });

    dots.forEach((dot, idx) => {
        dot.classList.toggle('active', idx === activePhotoIndex);
    });
}

// ============================================================================
// CANVAS PHOTO EDITOR LOGIC
// ============================================================================
function openEditor(filename, url, sizeStr) {
    currentPhotoName = filename;
    
    // Display metadata while loading
    photoDetails.textContent = `${filename} — CARGANDO...`;
    
    sourceImage.onload = () => {
        const resolution = `${sourceImage.naturalWidth} × ${sourceImage.naturalHeight} px`;
        photoDetails.textContent = `${filename} — ${resolution} — ${sizeStr}`;
        
        resetSliders();
        currentPreset = "natural";
        currentFrame = "none";
        
        // Highlight 'natural' preset and 'no frame' border initially
        updatePresetsUI();
        updateFramesUI();
        
        // Draw the image
        renderCanvas();
        switchView('editor');
    };
    
    sourceImage.onerror = () => {
        alert("Fallo al descargar la fotografía desde la cámara");
    };
    
    sourceImage.src = url;
}

// Draw canvas helper
function renderCanvas() {
    if (!sourceImage.complete || sourceImage.naturalWidth === 0) return;

    const imgW = sourceImage.naturalWidth;
    const imgH = sourceImage.naturalHeight;

    // 1. Calculate Canvas dimensions according to active borders
    if (currentFrame === "none") {
        canvas.width = imgW;
        canvas.height = imgH;
    } else if (currentFrame === "film") {
        // Film border will draw sprocket holes, so keep full size but allocate margin
        canvas.width = imgW;
        canvas.height = imgH;
    } else if (currentFrame === "instant") {
        // Polaroid style requires white borders around the picture
        canvas.width = imgW * 1.15;
        canvas.height = imgH * 1.30;
    }

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // 2. Prepare visual filters
    const preset = presetConfig[currentPreset];
    const bManual = parseInt(sliders.brightness.value);
    const cManual = parseInt(sliders.contrast.value);
    const sManual = parseInt(sliders.saturation.value);

    // Additive offset combining: standard starts at 100%
    const totalBrightness = 100 + preset.b + bManual;
    const totalContrast = 100 + preset.c + cManual;
    const totalSaturation = 100 + preset.s + sManual;
    
    // Special black & white case (make sure saturate is forced to 0)
    const filterString = `brightness(${totalBrightness}%) contrast(${totalContrast}%) saturate(${currentPreset === 'bw' ? 0 : totalSaturation}%)`;

    // 3. Draw depending on selected frame layout
    if (currentFrame === "none") {
        ctx.filter = filterString;
        ctx.drawImage(sourceImage, 0, 0, canvas.width, canvas.height);
        
        // Apply wash color tint if preset requires it
        if (preset.tint) {
            ctx.filter = 'none';
            ctx.globalCompositeOperation = 'multiply';
            ctx.fillStyle = preset.tint;
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.globalCompositeOperation = 'source-over'; // restore
        }
    } else if (currentFrame === "film") {
        // Background - solid dark
        ctx.filter = 'none';
        ctx.fillStyle = '#0b0b0d';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Center space for filtered photo
        const padX = canvas.width * 0.12;
        const padY = canvas.height * 0.08;
        const drawW = canvas.width - (padX * 2);
        const drawH = canvas.height - (padY * 2);

        // Draw image inside black container
        ctx.filter = filterString;
        ctx.drawImage(sourceImage, padX, padY, drawW, drawH);

        // Apply photo tint
        if (preset.tint) {
            ctx.filter = 'none';
            ctx.globalCompositeOperation = 'multiply';
            ctx.fillStyle = preset.tint;
            ctx.fillRect(padX, padY, drawW, drawH);
            ctx.globalCompositeOperation = 'source-over'; // restore
        }

        // Draw sprocket holes (side margins)
        ctx.filter = 'none';
        const numHoles = 7;
        const holeW = canvas.width * 0.035;
        const holeH = canvas.height * 0.055;
        const gapY = drawH / (numHoles - 1);

        for (let i = 0; i < numHoles; i++) {
            const hY = padY + (i * gapY) - (holeH / 2);
            // Left Sprocket Hole
            drawRoundedRect(ctx, canvas.width * 0.04, hY, holeW, holeH, 6, '#18181b');
            // Right Sprocket Hole
            drawRoundedRect(ctx, canvas.width * 0.925, hY, holeW, holeH, 6, '#18181b');
        }

        // Draw vintage yellow Kodak-style labels
        ctx.fillStyle = '#e69900';
        const fontSizeHeader = Math.round(canvas.height * 0.032);
        const fontSizeFooter = Math.round(canvas.height * 0.026);
        ctx.font = `bold ${fontSizeHeader}px monospace`;
        ctx.textAlign = 'left';
        
        ctx.fillText('RABOSETA CAM 400', canvas.width * 0.16, canvas.height * 0.048);
        
        ctx.font = `bold ${fontSizeFooter}px monospace`;
        ctx.fillText('24A', canvas.width * 0.16, canvas.height * 0.965);
        ctx.textAlign = 'right';
        ctx.fillText('▲ 25', canvas.width * 0.84, canvas.height * 0.965);
        
    } else if (currentFrame === "instant") {
        // Polaroid off-white card
        ctx.filter = 'none';
        ctx.fillStyle = '#f6f5f0';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Polaroid image borders
        const padX = imgW * 0.075;
        const padY = imgH * 0.075;
        const drawW = imgW;
        const drawH = imgH;

        // Draw shadow inner border
        ctx.fillStyle = '#e0dfd5';
        ctx.fillRect(padX - 2, padY - 2, drawW + 4, drawH + 4);

        // Draw image inside white container
        ctx.filter = filterString;
        ctx.drawImage(sourceImage, padX, padY, drawW, drawH);

        // Apply photo tint
        if (preset.tint) {
            ctx.filter = 'none';
            ctx.globalCompositeOperation = 'multiply';
            ctx.fillStyle = preset.tint;
            ctx.fillRect(padX, padY, drawW, drawH);
            ctx.globalCompositeOperation = 'source-over';
        }

        // Write filename or tag at the bottom white space
        ctx.filter = 'none';
        ctx.fillStyle = '#424246';
        ctx.textAlign = 'center';
        
        // Font size relative to size
        const fontSize = Math.round(canvas.height * 0.036);
        ctx.font = `${fontSize}px 'Courier New', Courier, monospace`;
        
        const photoLabel = currentPhotoName ? currentPhotoName.toUpperCase() : "RABOSETA CAM";
        ctx.fillText(photoLabel, canvas.width / 2, canvas.height - (canvas.height * 0.065));
    }
}

// Rounded rectangles helper (used for film sprocket holes)
function drawRoundedRect(cContext, x, y, width, height, radius, fillStyle) {
    cContext.beginPath();
    cContext.moveTo(x + radius, y);
    cContext.lineTo(x + width - radius, y);
    cContext.quadraticCurveTo(x + width, y, x + width, y + radius);
    cContext.lineTo(x + width, y + height - radius);
    cContext.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
    cContext.lineTo(x + radius, y + height);
    cContext.quadraticCurveTo(x, y + height, x, y + height - radius);
    cContext.lineTo(x, y + radius);
    cContext.quadraticCurveTo(x, y, x + radius, y);
    cContext.closePath();
    cContext.fillStyle = fillStyle;
    cContext.fill();
}

// ============================================================================
// ADJUSTMENTS CONTROLLERS & SLIDERS
// ============================================================================
function updateSliderProgress(sliderId) {
    const s = sliders[sliderId];
    const valIndicator = sliderVals[sliderId];
    if (!s || !valIndicator) return;
    
    const val = parseInt(s.value);
    
    // Format text like "+12", "-08", "00"
    if (val > 0) {
        valIndicator.textContent = `+${String(val).padStart(2, '0')}`;
    } else if (val < 0) {
        valIndicator.textContent = `-${String(Math.abs(val)).padStart(2, '0')}`;
    } else {
        valIndicator.textContent = '00';
    }
    
    // CSS variable percentage calculation (min is -100, max is 100, span is 200)
    const pct = ((val + 100) / 200) * 100;
    s.style.setProperty('--value-percent', `${pct}%`);
}

function resetSliders() {
    Object.keys(sliders).forEach(key => {
        sliders[key].value = 0;
        updateSliderProgress(key);
    });
}

// Initialize sliders events
Object.keys(sliders).forEach(key => {
    sliders[key].addEventListener('input', () => {
        updateSliderProgress(key);
        renderCanvas();
    });
    // Set initial track fills
    updateSliderProgress(key);
});

// Presets selectors
presetBtns.forEach(btn => {
    btn.addEventListener('click', () => {
        currentPreset = btn.dataset.preset;
        updatePresetsUI();
        renderCanvas();
    });
});

function updatePresetsUI() {
    presetBtns.forEach(btn => {
        btn.classList.toggle('active', btn.dataset.preset === currentPreset);
    });
}

// Borders/Frames selectors
frameBtns.forEach(btn => {
    btn.addEventListener('click', () => {
        currentFrame = btn.dataset.frame;
        updateFramesUI();
        renderCanvas();
    });
});

function updateFramesUI() {
    frameBtns.forEach(btn => {
        btn.classList.toggle('active', btn.dataset.frame === currentFrame);
    });
}

// Back to gallery
btnBack.addEventListener('click', () => {
    switchView('gallery');
    if (!isOfflineMode) {
        fetchPhotoList();
    }
});

// Refresh list
btnRefresh.addEventListener('click', () => {
    if (isOfflineMode) {
        showOfflineDemoMode();
    } else {
        fetchPhotoList();
    }
});

// Reset command
btnReset.addEventListener('click', () => {
    resetSliders();
    currentPreset = "natural";
    currentFrame = "none";
    updatePresetsUI();
    updateFramesUI();
    renderCanvas();
});

// ============================================================================
// ACTIONS: SAVE & SHARE
// ============================================================================
btnDownload.addEventListener('click', () => {
    try {
        const quality = 0.92;
        const dataUrl = canvas.toDataURL('image/jpeg', quality);
        const link = document.createElement('a');
        
        // Prepend modified name
        const cleanName = currentPhotoName ? currentPhotoName.replace('.jpg', '') : 'RABOSETA_CAM';
        const frameTag = currentFrame !== 'none' ? `_framed_${currentFrame}` : '';
        const presetTag = currentPreset !== 'natural' ? `_${currentPreset}` : '';
        
        link.download = `${cleanName}${presetTag}${frameTag}.jpg`;
        link.href = dataUrl;
        link.click();
    } catch (e) {
        console.error("Fallo al descargar la imagen:", e);
        alert("Error al descargar la imagen en este navegador. Revisa los permisos.");
    }
});

btnShare.addEventListener('click', async () => {
    try {
        const quality = 0.92;
        canvas.toBlob(async (blob) => {
            if (!blob) {
                alert("Error generando el archivo para compartir.");
                return;
            }
            
            const cleanName = currentPhotoName ? currentPhotoName.replace('.jpg', '') : 'RABOSETA_CAM';
            const file = new File([blob], `${cleanName}.jpg`, { type: 'image/jpeg' });
            
            // Trigger browser Web Share API if supported
            if (navigator.canShare && navigator.canShare({ files: [file] })) {
                await navigator.share({
                    files: [file],
                    title: 'Raboseta Cam Studio',
                    text: 'Mira esta foto que edité con mi Raboseta Cam'
                });
            } else {
                // Fallback copy or display error
                alert("La opción de compartir archivos directamente no es soportada en este navegador. Descarga la foto y compártela manualmente.");
            }
        }, 'image/jpeg', quality);
    } catch (e) {
        console.error("Sharing error:", e);
    }
});

// ============================================================================
// INITIALIZATION
// ============================================================================
async function syncTime() {
    if (isOfflineMode) return;
    try {
        const timestamp = Math.floor(Date.now() / 1000).toString();
        await fetch('/api/time', {
            method: 'POST',
            body: timestamp
        });
        console.log("Hora sincronizada con la Raboseta Cam");
    } catch (e) {
        console.warn("Time sync omitted (local/offline mode)");
    }
}

// Carousel global arrow keys, clicks and swipe/drag events setup
function setupCarouselListeners() {
    btnPrev.addEventListener('click', () => {
        if (photosList.length === 0) return;
        activePhotoIndex = (activePhotoIndex - 1 + photosList.length) % photosList.length;
        updateCarousel();
    });

    btnNext.addEventListener('click', () => {
        if (photosList.length === 0) return;
        activePhotoIndex = (activePhotoIndex + 1) % photosList.length;
        updateCarousel();
    });

    // Soporte para gestos táctiles (Móviles)
    let touchStartX = 0;
    let touchEndX = 0;
    photoCarousel.addEventListener('touchstart', (e) => {
        touchStartX = e.changedTouches[0].screenX;
    }, { passive: true });

    photoCarousel.addEventListener('touchend', (e) => {
        touchEndX = e.changedTouches[0].screenX;
        if (photosList.length === 0) return;
        const threshold = 50;
        if (touchStartX - touchEndX > threshold) {
            // Deslizar izquierda -> Siguiente foto
            activePhotoIndex = (activePhotoIndex + 1) % photosList.length;
            updateCarousel();
        } else if (touchEndX - touchStartX > threshold) {
            // Deslizar derecha -> Anterior foto
            activePhotoIndex = (activePhotoIndex - 1 + photosList.length) % photosList.length;
            updateCarousel();
        }
    }, { passive: true });

    // Soporte para gestos de arrastre con ratón (Ordenadores)
    let mouseStartX = 0;
    let mouseEndX = 0;
    let isMouseDown = false;

    photoCarousel.addEventListener('mousedown', (e) => {
        mouseStartX = e.screenX;
        isMouseDown = true;
    });

    photoCarousel.addEventListener('mouseup', (e) => {
        if (!isMouseDown) return;
        isMouseDown = false;
        mouseEndX = e.screenX;
        if (photosList.length === 0) return;
        const threshold = 50;
        if (mouseStartX - mouseEndX > threshold) {
            // Arrastrar izquierda -> Siguiente foto
            activePhotoIndex = (activePhotoIndex + 1) % photosList.length;
            updateCarousel();
        } else if (mouseEndX - mouseStartX > threshold) {
            // Arrastrar derecha -> Anterior foto
            activePhotoIndex = (activePhotoIndex - 1 + photosList.length) % photosList.length;
            updateCarousel();
        }
    });

    photoCarousel.addEventListener('mouseleave', () => {
        isMouseDown = false;
    });
}

window.onload = () => {
    // Setup carousel actions
    setupCarouselListeners();
    // Try to sync clock and check connectivity
    syncTime();
    checkAuth();
};
