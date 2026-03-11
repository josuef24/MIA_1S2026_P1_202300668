// ===== DOM Elements =====
const inputArea = document.getElementById('inputArea');
const outputArea = document.getElementById('outputArea');
const executeBtn = document.getElementById('executeBtn');
const fileInput = document.getElementById('fileInput');
const clearInputBtn = document.getElementById('clearInputBtn');
const clearOutputBtn = document.getElementById('clearOutputBtn');
const lineCounter = document.getElementById('lineCounter');
const loadingOverlay = document.getElementById('loadingOverlay');
const statusIndicator = document.getElementById('statusIndicator');

const API_URL = '/api/execute';

// ===== Line Counter =====
function updateLineCounter() {
    const lines = inputArea.value.split('\n').length;
    lineCounter.textContent = `${lines} línea${lines !== 1 ? 's' : ''}`;
}
inputArea.addEventListener('input', updateLineCounter);
updateLineCounter();

// ===== File Loading =====
fileInput.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (evt) => {
        inputArea.value = evt.target.result;
        updateLineCounter();
        appendOutput(`✓ Archivo "${file.name}" cargado exitosamente (${file.size} bytes)\n`, 'info');
    };
    reader.onerror = () => {
        appendOutput(`✗ Error al leer el archivo "${file.name}"\n`, 'error');
    };
    reader.readAsText(file);
    fileInput.value = ''; // Reset for re-upload
});

// ===== Output Formatting =====
function classifyLine(line) {
    const trimmed = line.trim();
    if (trimmed.startsWith('ERROR') || trimmed.startsWith('error')) return 'error';
    if (trimmed.startsWith('#')) return 'comment';
    if (trimmed.startsWith('MKDISK:') || trimmed.startsWith('RMDISK:') ||
        trimmed.startsWith('FDISK:') || trimmed.startsWith('MOUNT:') ||
        trimmed.startsWith('MOUNTED:') || trimmed.startsWith('MKFS:') ||
        trimmed.startsWith('LOGIN:') || trimmed.startsWith('LOGOUT:') ||
        trimmed.startsWith('MKGRP:') || trimmed.startsWith('RMGRP:') ||
        trimmed.startsWith('MKUSR:') || trimmed.startsWith('RMUSR:') ||
        trimmed.startsWith('CHGRP:') || trimmed.startsWith('MKDIR:') ||
        trimmed.startsWith('MKFILE:') || trimmed.startsWith('REP:')) return 'success';
    if (trimmed.startsWith('CAT ')) return 'info';
    if (trimmed.startsWith('===')) return 'info';
    return '';
}

function appendOutput(text, forceClass) {
    const lines = text.split('\n');
    for (const line of lines) {
        if (line === '' && forceClass === undefined) {
            outputArea.appendChild(document.createTextNode('\n'));
            continue;
        }
        const span = document.createElement('span');
        const cls = forceClass || classifyLine(line);
        if (cls) span.className = `line-${cls}`;
        span.textContent = line + '\n';
        outputArea.appendChild(span);
    }
    // Auto-scroll to bottom
    outputArea.parentElement.scrollTop = outputArea.parentElement.scrollHeight;
}

function clearOutput() {
    outputArea.innerHTML = '';
}

// ===== Execute Commands =====
async function executeCommands() {
    const script = inputArea.value.trim();
    if (!script) {
        appendOutput('⚠ No hay comandos para ejecutar.\n', 'warning');
        return;
    }

    executeBtn.disabled = true;
    loadingOverlay.classList.add('active');

    const separator = '─'.repeat(50);
    appendOutput(`${separator}\n`, 'info');
    appendOutput(`▶ Ejecutando script (${new Date().toLocaleTimeString()})...\n\n`, 'info');

    try {
        const response = await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: script
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const result = await response.text();
        appendOutput(result);
        appendOutput(`\n✓ Ejecución completada.\n`, 'success');

    } catch (err) {
        appendOutput(`✗ Error de conexión: ${err.message}\n`, 'error');
        appendOutput('  Asegúrate de que el servidor esté corriendo.\n', 'error');
        setStatus(false);
    } finally {
        executeBtn.disabled = false;
        loadingOverlay.classList.remove('active');
    }
}

executeBtn.addEventListener('click', executeCommands);

// Ctrl+Enter to execute
inputArea.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        e.preventDefault();
        executeCommands();
    }
    // Tab support
    if (e.key === 'Tab') {
        e.preventDefault();
        const start = inputArea.selectionStart;
        const end = inputArea.selectionEnd;
        inputArea.value = inputArea.value.substring(0, start) + '    ' + inputArea.value.substring(end);
        inputArea.selectionStart = inputArea.selectionEnd = start + 4;
        updateLineCounter();
    }
});

// ===== Clear Buttons =====
clearInputBtn.addEventListener('click', () => {
    inputArea.value = '';
    updateLineCounter();
});

clearOutputBtn.addEventListener('click', clearOutput);

// ===== Status Check =====
function setStatus(connected) {
    const dot = statusIndicator.querySelector('.status-dot');
    const text = statusIndicator.querySelector('.status-text');
    if (connected) {
        dot.className = 'status-dot';
        text.textContent = 'Conectado';
        text.style.color = 'var(--success)';
        statusIndicator.style.background = 'rgba(34, 197, 94, 0.08)';
        statusIndicator.style.borderColor = 'rgba(34, 197, 94, 0.2)';
    } else {
        dot.className = 'status-dot error';
        text.textContent = 'Desconectado';
        text.style.color = 'var(--error)';
        statusIndicator.style.background = 'rgba(239, 68, 68, 0.08)';
        statusIndicator.style.borderColor = 'rgba(239, 68, 68, 0.2)';
    }
}

async function checkStatus() {
    try {
        const res = await fetch('/api/status');
        setStatus(res.ok);
    } catch {
        setStatus(false);
    }
}

// Check status on load and periodically
checkStatus();
setInterval(checkStatus, 10000);

// ===== Welcome message =====
appendOutput('═══════════════════════════════════════════════════\n', 'info');
appendOutput(' MIA Proyecto 1 — EXT2 File System Simulator\n', 'info');
appendOutput('═══════════════════════════════════════════════════\n', 'info');
appendOutput(' Usa "Cargar Script" para abrir un archivo .txt\n', 'comment');
appendOutput(' o escribe comandos directamente.\n', 'comment');
appendOutput(' Presiona Ctrl+Enter para ejecutar.\n\n', 'comment');
