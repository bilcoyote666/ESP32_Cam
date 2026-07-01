// ============================================================================
// Referencias UI
// ============================================================================
const views = {
    gallery: document.getElementById('gallery-view'),
    editor:  document.getElementById('editor-view'),
    auth:    document.getElementById('auth-view')
};

const btnAuthSubmit = document.getElementById('btn-auth-submit');
const authPassword = document.getElementById('auth-password');
const authTitle = document.getElementById('auth-title');
const authDesc = document.getElementById('auth-desc');
const authError = document.getElementById('auth-error');

let isSetupMode = false;

const photoCarousel = document.getElementById('photo-carousel');
const carouselIndicators = document.getElementById('carousel-indicators');
const btnRefresh = document.getElementById('btn-refresh');
const btnBack = document.getElementById('btn-back');
const btnReset = document.getElementById('btn-reset');
const btnDownload = document.getElementById('btn-download');

// Editor
const canvas = document.getElementById('photo-canvas');
const ctx = canvas.getContext('2d');
const sourceImage = document.getElementById('source-image');

// Filtros
const filters = {
    brightness: document.getElementById('filter-brightness'),
    contrast:   document.getElementById('filter-contrast'),
    saturation: document.getElementById('filter-saturation'),
    sepia:      document.getElementById('filter-sepia')
};

let currentPhotoName = "";

// ============================================================================
// Lógica de red (Fetch)
// ============================================================================

async function fetchPhotoList() {
    try {
        photoCarousel.innerHTML = '<div class="empty-state">Cargando fotografías exclusivas...</div>';
        carouselIndicators.innerHTML = '';
        
        const response = await fetch('/list?t=' + Date.now());
        if (!response.ok) throw new Error("Error en la respuesta del servidor");
        
        const files = await response.json();
        renderPhotoList(files);
    } catch (error) {
        console.error("Error al obtener lista:", error);
        photoCarousel.innerHTML = `<div class="empty-state">Error al cargar: ${error.message}</div>`;
    }
}

// ============================================================================
// Interfaz de Usuario
// ============================================================================

function switchView(viewName) {
    Object.values(views).forEach(v => v.classList.remove('active'));
    views[viewName].classList.add('active');
}

function renderPhotoList(files) {
    if (!files || files.length === 0) {
        photoCarousel.innerHTML = '<div class="empty-state">No hay fotos en la tarjeta SD.</div>';
        return;
    }

    // Ordenar de más nueva a más vieja
    files.sort((a, b) => b.name.localeCompare(a.name));

    photoCarousel.innerHTML = '';
    carouselIndicators.innerHTML = '';

    files.forEach((f, index) => {
        // Slide del carrusel
        const slide = document.createElement('div');
        slide.className = 'carousel-slide';
        
        const photoUrl = `/photo?name=${f.name}&t=${Date.now()}`;
        let sizeStr = f.size > 1024*1024 ? 
            (f.size / (1024*1024)).toFixed(1) + ' MB' : 
            (f.size / 1024).toFixed(0) + ' KB';

        slide.innerHTML = `
            <img src="${photoUrl}" loading="lazy" alt="${f.name}">
            <div class="slide-info">
                <div class="name">${f.name}</div>
                <div class="details">${sizeStr} • ${f.date}</div>
            </div>
        `;

        slide.addEventListener('click', () => openEditor(f.name, photoUrl));
        photoCarousel.appendChild(slide);

        // Indicador (puntito)
        const dot = document.createElement('div');
        dot.className = 'indicator-dot' + (index === 0 ? ' active' : '');
        dot.addEventListener('click', () => {
            slide.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'start' });
        });
        carouselIndicators.appendChild(dot);
    });

    // Actualizar indicador activo en scroll
    photoCarousel.addEventListener('scroll', () => {
        const scrollLeft = photoCarousel.scrollLeft;
        const slideWidth = photoCarousel.clientWidth;
        const activeIndex = Math.round(scrollLeft / slideWidth);
        
        const dots = carouselIndicators.querySelectorAll('.indicator-dot');
        dots.forEach((d, i) => {
            d.classList.toggle('active', i === activeIndex);
        });
    });
}

// ============================================================================
// Editor de Fotos
// ============================================================================

function openEditor(filename, url) {
    currentPhotoName = filename;
    
    // Mostramos un estado de carga mientras bajamos la imagen en alta calidad
    sourceImage.onload = () => {
        // Ajustamos el tamaño del canvas a la imagen real
        canvas.width = sourceImage.width;
        canvas.height = sourceImage.height;
        
        // Reseteamos filtros
        Object.values(filters).forEach(f => {
            f.value = f.defaultValue;
        });
        
        applyFilters();
        switchView('editor');
    };
    
    sourceImage.onerror = () => {
        alert("Error al cargar la imagen para editar");
    };
    
    sourceImage.src = url;
}

function applyFilters() {
    const b = filters.brightness.value;
    const c = filters.contrast.value;
    const s = filters.saturation.value;
    const sep = filters.sepia.value;

    const filterString = `brightness(${b}%) contrast(${c}%) saturate(${s}%) sepia(${sep}%)`;
    
    ctx.filter = filterString;
    ctx.drawImage(sourceImage, 0, 0, canvas.width, canvas.height);
}

// Configuración de presets VIP
const presetConfig = {
    natural:  { b: 100, c: 100, s: 100, sep: 0 },
    bw:       { b: 100, c: 120, s: 0,   sep: 0 },
    sepia:    { b: 100, c: 100, s: 100, sep: 100 },
    contrast: { b: 110, c: 150, s: 110, sep: 0 },
    vintage:  { b: 120, c: 90,  s: 70,  sep: 40 }
};

const presetBtns = document.querySelectorAll('.preset-btn');

function applyPreset(presetName) {
    const config = presetConfig[presetName];
    if (config) {
        filters.brightness.value = config.b;
        filters.contrast.value = config.c;
        filters.saturation.value = config.s;
        filters.sepia.value = config.sep;
        applyFilters();
    }
}

presetBtns.forEach(btn => {
    btn.addEventListener('click', () => {
        // Actualizar UI
        presetBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        // Aplicar
        applyPreset(btn.dataset.preset);
    });
});

Object.values(filters).forEach(f => {
    f.addEventListener('input', () => {
        // Si el usuario toca los deslizadores manuales, quitamos la marca de preset activo
        presetBtns.forEach(b => b.classList.remove('active'));
        applyFilters();
    });
});

btnReset.addEventListener('click', () => {
    Object.values(filters).forEach(f => f.value = f.defaultValue);
    presetBtns.forEach(b => b.classList.remove('active'));
    document.querySelector('[data-preset="natural"]').classList.add('active');
    applyFilters();
});

btnDownload.addEventListener('click', () => {
    const dataUrl = canvas.toDataURL('image/jpeg', 0.9);
    const link = document.createElement('a');
    link.download = `edited_${currentPhotoName}`;
    link.href = dataUrl;
    link.click();
});

btnBack.addEventListener('click', () => {
    switchView('gallery');
});

btnRefresh.addEventListener('click', () => {
    fetchPhotoList();
});

// ============================================================================
// Autenticación
// ============================================================================
async function checkAuth() {
    try {
        const res = await fetch('/api/status');
        const status = await res.json();
        
        if (!status.pwd_set) {
            isSetupMode = true;
            authTitle.textContent = "Bienvenido a Raboseta Cam";
            authDesc.textContent = "Configura una contraseña para proteger tus fotos.";
            btnAuthSubmit.textContent = "Guardar Contraseña";
            switchView('auth');
        } else if (!status.auth) {
            isSetupMode = false;
            authTitle.textContent = "Acceso Protegido";
            authDesc.textContent = "Introduce la contraseña para entrar.";
            btnAuthSubmit.textContent = "Entrar";
            switchView('auth');
        } else {
            switchView('gallery');
            fetchPhotoList();
        }
    } catch (e) {
        console.error("Error comprobando auth:", e);
    }
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
            checkAuth(); // re-evaluar estado
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

async function syncTime() {
    try {
        const timestamp = Math.floor(Date.now() / 1000).toString();
        await fetch('/api/time', {
            method: 'POST',
            body: timestamp
        });
        console.log("Hora sincronizada con la cámara");
    } catch (e) {
        console.error("Error sincronizando hora:", e);
    }
}

// Arrancar obteniendo estado y sincronizando hora
window.onload = () => {
    syncTime();
    checkAuth();
};
