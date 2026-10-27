# Validação manual do player de música

**Dispositivo:** M5Stack Tab5 / ESP32-P4
**Porta usual:** `/dev/ttyACM0`
**Firmware integrado atual:** `version.txt` (confirmar hash da imagem no registro)

Este roteiro não autoriza repetir recuperação sem limite. Registre horário,
versão/hash, porta, resposta NDJSON, logs seriais e `sys.info` antes/depois.
Qualquer safe mode, ausência de resposta, reboot inesperado, panic ou WDT encerra
o cenário; não repetir clear/reset/playback automaticamente.

## 1. Recovery e boot normal — gate obrigatório

`events.read` também exporta o journal NVS de recovery como um frame
`recovery_log` (além do log textual do SD). Capture-o sem abrir um monitor
serial concorrente:

```bash
python3 tools/cyberdeck_cli.py --port /dev/ttyACM0 events.read
```

O journal registra `load` e as transições `boot_begin`, `clear` e `checkpoint`,
com estado anterior/posterior e resultado de persistência. Ele é diagnóstico
best-effort e não substitui `sys.info` nem altera o latch.

1. Grave a imagem construída para o snapshot atual e capture o boot serial
   completo. Registre as linhas `HOSTED_DIAG recovery`, `hosted`, `wifi`, SD e
   bateria.
2. Consulte `sys.info`. Se `safe_mode=true`, consulte o estado e os logs, envie
   `sys.safe_mode.clear` no máximo uma vez e confirme `cleared=true`. Espere o
   boot atual terminar; então faça no máximo um reset controlado e capture o
   próximo boot. O clear explícito também conclui o checkpoint pendente e zera
   `interrupted_boots`, para que reset provocado pelo fechamento/reabertura da
   Serial-JTAG não conte o mesmo boot de recovery como nova interrupção. Não
   faça clear adicional.
3. Só prossiga após observar no log serial `HOSTED_DIAG recovery
   stage=checkpoint result=ESP_OK persisted=1 safe_mode=0 pending=0` e
   `CYBERDECK5 iniciado`. Depois confirme em duas consultas `sys.info`, separadas
   por alguns segundos, `safe_mode=false`, `interrupted_boots=0` e uptime
   crescente. Um `sys.info` antecipado pode retornar valores aparentemente
   normais enquanto `app_main` ainda inicializa serviços; não envie comandos de
   app/player antes do checkpoint. O boot normal também deve apresentar
   `HOSTED_DIAG hosted stage=power_enable`, `esp_hosted_init`, `connect_to_slave`,
   montagem SD e init/start do Wi-Fi.
4. Se os marcadores Hosted não surgirem, houver RPC timeout/CP não pronto, safe
   mode reaparecer ou `sys.info` não responder, pare. Não iniciar o player nem
   atribuir a falha ao decoder/áudio; preserve a captura para diagnóstico.

## 2. App, scan e uma tentativa de reprodução

Pré-condição: boot normal comprovado e sem reboot/WDT durante a captura.

```text
sys.info
ui.type "app start cyberdeck.music"
term.dump
ui.type "app info cyberdeck.music"
term.dump
ui.type "music queue /musica"
term.dump
ui.type "music status"
term.dump
ui.type "music list"
term.dump
```

`cyberdeck.music` tem `auto_start=false`, portanto o start explícito é
necessário. Confirme `state=running`, scan concluído e a faixa real
`01 - The Kid (Kid Cosmic Theme).mp3`. Se o app ou a faixa não forem
confirmados, não enviar play.

Envie exatamente uma vez:

```text
ui.type "music play 1"
term.dump
ui.type "music status"
term.dump
sys.info
```

Registre estado/progresso/`error_stage`, saída do codec e uptime. `playing` ou
PCM escrito não provam áudio audível. Se surgir erro, WDT, reboot, serviço
indisponível ou uptime reiniciado, pare; sem replay/recovery adicional.

## 3. Aceite acústico e controles (observador humano)

Somente após a tentativa estável e com confirmação humana:

- verificar áudio audível no speaker integrado;
- verificar inserção/remoção de fone e rota pelo jack de 3,5 mm;
- testar pause/resume/seek/volume/stop e observar status/progresso;
- confirmar amplificador desligado quando ocioso, sem ruído persistente;
- observar aquecimento/consumo anormal, coexistência com Wi-Fi/BLE e stop;
- verificar que o MP3 original permanece byte-idêntico;
- poweroff durante playback só em cenário separado e explicitamente aprovado.

Não marcar aceite físico completo sem registrar confirmação humana de speaker e
jack. Host tests/build não substituem este roteiro.
