// =============================================================================
// web_page.h — Interface Web FPV (HTML + CSS + JavaScript)
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Uso: #include "web_page.h"
//      httpd_resp_send(req, PAGINA_WEB, HTTPD_RESP_USE_STRLEN);
//
// Funcionalidades da interface:
//   - Stream de vídeo MJPEG ao vivo (src="/stream")
//   - Botões de controle touch (Frente, Parar, Trás)
//   - Controle por teclado (W/S/Espaço ou ↑↓)
//   - Envio de comandos via fetch() GET /cmd?dir=X
//   - Indicador de status de conexão Wi-Fi
//   - Design responsivo (desktop e mobile)
//
// A string usa raw literal R"HTML(...)HTML" do C++11 para evitar
// escapes de aspas e barras dentro do HTML.
// =============================================================================

#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

static const char PAGINA_WEB[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Rover FPV — IHS/UFS</title>
  <style>
    /* ── Reset e base ── */
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    :root {
      --verde:    #00ff88;
      --vermelho: #ff3355;
      --azul:     #0af;
      --fundo:    #0a0c0f;
      --painel:   #111418;
      --borda:    #1e2530;
      --texto:    #c8d4e0;
    }

    body {
      background: var(--fundo);
      color: var(--texto);
      font-family: 'Courier New', monospace;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 12px;
      gap: 12px;
    }

    /* ── Cabeçalho ── */
    header {
      width: 100%;
      max-width: 760px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 14px;
      background: var(--painel);
      border: 1px solid var(--borda);
      border-radius: 6px;
    }
    header h1 {
      font-size: 0.85rem;
      letter-spacing: 0.12em;
      color: var(--verde);
      text-transform: uppercase;
    }
    #status {
      font-size: 0.75rem;
      display: flex;
      align-items: center;
      gap: 6px;
    }
    #status-dot {
      width: 8px; height: 8px;
      border-radius: 50%;
      background: var(--vermelho);
      box-shadow: 0 0 6px var(--vermelho);
      transition: background 0.3s, box-shadow 0.3s;
    }
    #status-dot.online {
      background: var(--verde);
      box-shadow: 0 0 6px var(--verde);
    }

    /* ── Visor da câmera ── */
    .visor {
      width: 100%;
      max-width: 760px;
      aspect-ratio: 4/3;
      background: #050608;
      border: 1px solid var(--borda);
      border-radius: 8px;
      overflow: hidden;
      position: relative;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .visor img {
      width: 100%;
      height: 100%;
      object-fit: cover;
      display: block;
    }
    /* Overlay de scanlines (estética FPV) */
    .visor::after {
      content: '';
      position: absolute;
      inset: 0;
      background: repeating-linear-gradient(
        0deg,
        transparent,
        transparent 2px,
        rgba(0,0,0,0.08) 2px,
        rgba(0,0,0,0.08) 4px
      );
      pointer-events: none;
    }
    /* Canto com indicador REC */
    .visor-rec {
      position: absolute;
      top: 10px; right: 14px;
      font-size: 0.65rem;
      color: var(--vermelho);
      letter-spacing: 0.1em;
      opacity: 0;
      transition: opacity 0.3s;
    }
    .visor-rec.ativo { opacity: 1; animation: pisca 1s step-end infinite; }
    @keyframes pisca { 0%, 100% { opacity: 1; } 50% { opacity: 0; } }

    /* Sem câmera */
    .sem-camera {
      text-align: center;
      padding: 20px;
      color: #445;
    }
    .sem-camera .icone { font-size: 3rem; margin-bottom: 8px; }
    .sem-camera p { font-size: 0.8rem; line-height: 1.6; }

    /* ── Painel de controle ── */
    .controles {
      width: 100%;
      max-width: 760px;
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      grid-template-rows: auto auto;
      gap: 8px;
    }

    .btn {
      padding: 18px 0;
      border: 1px solid var(--borda);
      border-radius: 8px;
      background: var(--painel);
      color: var(--texto);
      font-family: inherit;
      font-size: 1.4rem;
      cursor: pointer;
      user-select: none;
      -webkit-tap-highlight-color: transparent;
      transition: background 0.1s, border-color 0.1s, transform 0.08s;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 4px;
      position: relative;
      overflow: hidden;
    }
    .btn span.label {
      font-size: 0.6rem;
      letter-spacing: 0.1em;
      text-transform: uppercase;
      color: #566;
    }

    /* Estados ativos por cor */
    .btn-frente:active,  .btn-frente.pressionado  {
      background: #0a1f14; border-color: var(--verde);
      color: var(--verde);
      transform: scale(0.96);
    }
    .btn-tras:active,   .btn-tras.pressionado   {
      background: #1f0a0f; border-color: var(--vermelho);
      color: var(--vermelho);
      transform: scale(0.96);
    }
    .btn-parar:active,  .btn-parar.pressionado  {
      background: #0e0e18; border-color: var(--azul);
      color: var(--azul);
      transform: scale(0.96);
    }

    /* Layout dos botões: ▲ no centro topo, ■ no centro baixo, ▼ no centro baixo */
    .btn-frente { grid-column: 2; grid-row: 1; }
    .btn-parar  { grid-column: 2; grid-row: 2; }
    .btn-tras   { grid-column: 2; grid-row: 3; }

    /* ── Legenda do teclado ── */
    .teclado-hint {
      width: 100%;
      max-width: 760px;
      text-align: center;
      font-size: 0.68rem;
      color: #334;
      letter-spacing: 0.05em;
    }
    kbd {
      background: #1a1e25;
      border: 1px solid #2a3040;
      border-radius: 3px;
      padding: 1px 5px;
      font-family: inherit;
      font-size: 0.68rem;
      color: #778;
    }

    /* ── Direção atual (feedback visual) ── */
    #direcao-atual {
      font-size: 0.72rem;
      color: #445;
      min-height: 1.1em;
      text-align: center;
      letter-spacing: 0.08em;
    }
  </style>
</head>
<body>

  <!-- Cabeçalho -->
  <header>
    <h1>⬡ Rover FPV &mdash; IHS/UFS</h1>
    <div id="status">
      <div id="status-dot"></div>
      <span id="status-texto">Conectando...</span>
    </div>
  </header>

  <!-- Visor da câmera -->
  <div class="visor">
    <div class="visor-rec ativo" id="rec">● REC</div>
    <img id="stream-img"
         src="/stream"
         alt="Stream de vídeo"
         onerror="erroStream()"
         onload="streamOK()">
    <!-- Conteúdo mostrado se a câmera não estiver disponível -->
    <div class="sem-camera" id="sem-camera" style="display:none">
      <div class="icone">📷</div>
      <p>Câmera não disponível<br>
         Verifique a alimentação (fonte 5V/2A)<br>
         Controle de direção ainda funciona</p>
    </div>
  </div>

  <!-- Direção atual -->
  <div id="direcao-atual">— PARADO —</div>

  <!-- Botões de controle -->
  <div class="controles">
    <button class="btn btn-frente"
            id="btn-F"
            onpointerdown="iniciarCmd('F')"
            onpointerup="pararCmd()"
            onpointerleave="pararCmd()">
      ▲<span class="label">Frente</span>
    </button>

    <button class="btn btn-parar"
            id="btn-P"
            onpointerdown="iniciarCmd('P')"
            onpointerup="pararCmd()">
      ■<span class="label">Parar</span>
    </button>

    <button class="btn btn-tras"
            id="btn-T"
            onpointerdown="iniciarCmd('T')"
            onpointerup="pararCmd()"
            onpointerleave="pararCmd()">
      ▼<span class="label">Ré</span>
    </button>
  </div>

  <!-- Dica de teclado -->
  <p class="teclado-hint">
    Teclado: <kbd>W</kbd> / <kbd>↑</kbd> frente &nbsp;·&nbsp;
             <kbd>S</kbd> / <kbd>↓</kbd> ré &nbsp;·&nbsp;
             <kbd>Espaço</kbd> parar
  </p>

<script>
  // ==========================================================================
  // Estado e controle de comandos
  // ==========================================================================
  let cmdAtual = null;
  let intervaloCmd = null;
  const INTERVALO_MS = 150; // Envia o mesmo comando a cada 150ms enquanto pressionado

  const rotulos = { 'F': '▲ FRENTE', 'T': '▼ RÉ', 'P': '■ PARADO', null: '— PARADO —' };
  const mapaBotao = { 'F': 'btn-F', 'T': 'btn-T', 'P': 'btn-P' };

  function enviarCmd(dir) {
    fetch('/cmd?dir=' + dir)
      .then(r => {
        document.getElementById('status-dot').classList.add('online');
        document.getElementById('status-texto').textContent = 'Online';
      })
      .catch(() => {
        document.getElementById('status-dot').classList.remove('online');
        document.getElementById('status-texto').textContent = 'Sem conexão';
      });
    document.getElementById('direcao-atual').textContent = rotulos[dir] || '—';
  }

  function iniciarCmd(dir) {
    if (cmdAtual === dir) return;
    pararCmd();
    cmdAtual = dir;
    document.getElementById(mapaBotao[dir])?.classList.add('pressionado');
    enviarCmd(dir);
    // Repete o comando enquanto o botão estiver pressionado (evita timeout)
    if (dir !== 'P') {
      intervaloCmd = setInterval(() => enviarCmd(dir), INTERVALO_MS);
    }
  }

  function pararCmd() {
    if (intervaloCmd) { clearInterval(intervaloCmd); intervaloCmd = null; }
    if (cmdAtual && cmdAtual !== 'P') {
      document.getElementById(mapaBotao[cmdAtual])?.classList.remove('pressionado');
      cmdAtual = null;
      enviarCmd('P'); // Ao soltar, para o rover
    } else if (cmdAtual === 'P') {
      document.getElementById('btn-P')?.classList.remove('pressionado');
      cmdAtual = null;
    }
  }

  // ==========================================================================
  // Controle por teclado
  // ==========================================================================
  const mapaTecla = {
    'w': 'F', 'W': 'F', 'ArrowUp': 'F',
    's': 'T', 'S': 'T', 'ArrowDown': 'T',
    ' ': 'P'
  };

  document.addEventListener('keydown', e => {
    const dir = mapaTecla[e.key];
    if (dir && !e.repeat) {
      e.preventDefault();
      iniciarCmd(dir);
    }
  });
  document.addEventListener('keyup', e => {
    const dir = mapaTecla[e.key];
    if (dir && dir !== 'P') pararCmd();
  });

  // ==========================================================================
  // Gerenciamento do stream de vídeo
  // ==========================================================================
  let tentativasStream = 0;

  function streamOK() {
    tentativasStream = 0;
    document.getElementById('sem-camera').style.display = 'none';
    document.getElementById('stream-img').style.display = 'block';
    document.getElementById('rec').classList.add('ativo');
  }

  function erroStream() {
    tentativasStream++;
    document.getElementById('rec').classList.remove('ativo');
    if (tentativasStream >= 3) {
      // Após 3 tentativas falhas, mostra o painel de erro
      document.getElementById('stream-img').style.display = 'none';
      document.getElementById('sem-camera').style.display = 'flex';
    } else {
      // Tenta reconectar com delay crescente
      setTimeout(() => {
        const img = document.getElementById('stream-img');
        img.src = '/stream?t=' + Date.now(); // Quebra o cache
      }, tentativasStream * 2000);
    }
  }

  // ==========================================================================
  // Verificação de conectividade com o rover
  // ==========================================================================
  function verificarConexao() {
    fetch('/cmd?dir=P', { signal: AbortSignal.timeout(2000) })
      .then(() => {
        document.getElementById('status-dot').classList.add('online');
        document.getElementById('status-texto').textContent = 'Online';
      })
      .catch(() => {
        document.getElementById('status-dot').classList.remove('online');
        document.getElementById('status-texto').textContent = 'Sem conexão';
      });
  }
  verificarConexao();
  setInterval(verificarConexao, 5000);
</script>
</body>
</html>
)HTML";

#endif // WEB_PAGE_H