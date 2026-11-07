# Validacao Manual: Canal HTTP de Comandos (LAN)

**Projeto:** cyberdeck5 (M5Stack Tab5)
**Escopo:** rotas `/control/challenge`, `/control/type` e `/control/terminal` na
porta 80 e o cliente host `tools/cyberdeck_http_cli.py`, exercitando
provisionamento USB da chave dedicada, autenticação HMAC/nonce, limites de
texto, fila de injeção, snapshot do terminal e recusa com a proteção de tela.
**Estado sob validação:** firmware `1.15.0`; serviço `cyberdeck.screenshot`
1.1.0; fila bounded de 4 comandos; task `ctl_input`; chave em NVS
(`ctl_auth/key`) exposta apenas por `control.key` na ponte USB.
**Duração estimada:** 20-30 min.
**Objetivo principal:** enviar comandos sem ocupar a USB Serial-JTAG, mantendo o
`idf.py monitor` aberto para observar logs, panic e reboot.

> Este plano NAO substitui codigo de producao nem altera firmware. Qualquer
> falha deve ser registrada abaixo e reportada ao developer/reviewer.

---

## 1. Pre-condicoes

| # | Item | Procedimento |
|---|------|--------------|
| P1 | Firmware atual | `source ~/esp/esp-idf/export.sh && idf.py build && idf.py -p /dev/ttyACM0 flash` |
| P2 | Monitor | `idf.py monitor` aberto em um terminal e mantido durante todo o roteiro |
| P3 | Wi-Fi com IP | `sys.info` (pela ponte, antes de abrir o monitor) ou o header da TUI |
| P4 | Chave provisionada | `python3 tools/cyberdeck_http_cli.py --port /dev/ttyACM0 provision` **com o monitor fechado**; o comando recusa sobrescrever |
| P5 | Permissao da chave | `ls -l ~/.config/cyberdeck5/control.key` deve mostrar `-rw-------` (0600) |
| P6 | Cliente | `python3 tools/cyberdeck_http_cli.py --help` responde sem erro |

Registre em cada execução: `device_id`, IP, commit/hash do firmware, versão,
data e observador. **Nunca** cole o conteúdo de `control.key`.

---

## 2. Provisionamento e autenticação

| ID | Ação | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| A1 | `provision` sem chave existente | Mensagem `Control key saved ... (0600)`; arquivo com 64 hex |  |  |
| A2 | Repetir `provision` | Erro "Refusing to overwrite existing key"; arquivo intacto |  |  |
| A3 | `--host <IP> status` | JSON `{"reachable":true,...}` com `ttl_ms` 60000 |  |  |
| A4 | `status` com arquivo de chave em 0644 | Erro local exigindo `chmod 600`; nenhuma requisição enviada |  |  |
| A5 | Requisição com MAC adulterado (script manual) | HTTP 401 `unauthorized`; o desafio continua utilizável |  |  |
| A6 | Reutilizar o mesmo nonce/MAC de A5 após sucesso | HTTP 401 `no_challenge` (replay recusado) |  |  |
| A7 | Requisição após 60 s do challenge | HTTP 401 `expired` |  |  |

## 3. Injeção de comandos (`type`)

| ID | Ação | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| T1 | `type "help"` seguido de `terminal` | `accepted ... 1 line`; o snapshot mostra a saída do `help` |  |  |
| T2 | `type "pwd"` | Saída `/` no snapshot; `/$ ` de volta |  |  |
| T3 | `type "help\nlog"` | Duas linhas executadas em ordem |  |  |
| T4 | `type "a"*1025` | Erro local: limite de 1024 bytes; nenhuma requisição |  |  |
| T5 | Corpo vazio | Erro local recusando texto vazio |  |  |
| T6 | `type` com acentuação (`"echo ação"`) | UTF-8 preservado no terminal |  |  |
| T7 | Enviar 4 comandos em sequência rápida e, em seguida, mais 4 | Os 4 primeiros executam um por linha (entrega confirmada); o excedente pode receber `429 busy` sem corromper a linha |  |  |
| T8 | Com sessão SSH ativa | O texto vai para o PTY remoto, como `ui.type` |  |  |
| T9 | Durante o monitor com logs | Nenhuma resposta NDJSON da ponte é necessária; logs continuam |  |  |

## 4. Snapshot e proteção de tela

| ID | Ação | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| S1 | `terminal` no menu | JSON com `text`, `bytes`, `truncated`; conteúdo igual ao `term.dump` |  |  |
| S2 | `terminal` com terminal longo | `truncated:true` e no máximo 2048 bytes |  |  |
| S3 | `screen off` (pela LAN) e depois `type "help"` | `409 screen_off`, nada injetado |  |  |
| S4 | `screen on` e `type "help"` | Volta a funcionar; nenhuma tecla perdida |  |  |
| S5 | `terminal` com a tela apagada | `409 screen_off` |  |  |
| S6 | `type "screen timeout 9999"` | Rejeitado no parser do shell (erro exato), sem reboot |  |  |

## 5. Robustez e isolamento

| ID | Ação | Resultado esperado | Pass/Fail | Obs |
|----|------|--------------------|-----------|-----|
| R1 | `type` com o Wi-Fi caindo no meio | Erro de conexão local; após reconectar, `status` volta a responder |  |  |
| R2 | Requisição de peer fora da faixa local | HTTP 403 (testar de outra sub-rede/VPN) |  |  |
| R3 | `curl -X GET /control/type` | HTTP 405 |  |  |
| R4 | Corpo > 1024 bytes enviado por script cru | HTTP 413 `too_large`; conexão não é reutilizada |  |  |
| R5 | `sys.info` após todo o roteiro | Uptime monotônico, sem panic/reboot; `safe_mode=false` |  |  |
| R6 | Monitor serial durante todo o roteiro | Sem panic, WDT ou reboot; logs coerentes |  |  |
| R7 | Reiniciar o Tab5 e repetir T1 | Chave persistiu (NVS); não é necessário reprovisionar |  |  |

---

## 6. Evidências a registrar

- Versão/SHA-256 do binário e slot em execução.
- Saídas de `status`, `type` e `terminal` (sem a chave).
- Trecho do monitor serial durante T1/T7/R1, comprovando ausência de reboot.
- `sys.info` antes e depois (uptime, heap, `safe_mode`).
- Resultado de R2/R4 (403/413) e o método usado.

## 7. Limites conhecidos

- HTTP sem TLS: o texto do comando e a resposta do terminal são visíveis na LAN.
- O HMAC autentica quem possui a chave, não identifica a pessoa; o filtro de
  peer local não autoriza usuário.
- `terminal` reflete o terminal inteiro (shell, SSH, apps, teclado físico), não
  apenas o último comando enviado.
- `202 Accepted` não significa execução concluída; confirme com `terminal`.
- O canal tem o mesmo alcance de `ui.type`; comandos destrutivos devem ser
  evitados durante a validação.
